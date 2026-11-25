#include "game_browser.hpp"

#include "image.hpp"
#include "image_cache.hpp"
#include "stb_image.h"
#include "installed.hpp"
#include "jobs.hpp"
#include "semver.hpp"
#include "sound.hpp"
#include "ui.hpp"
#include "units.hpp"
#include "fs_util.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <deque>
#include <functional>
#include <map>
#include <cstdlib>
#include <vector>

namespace screens {
namespace {

// --- Grade (tela superior) ------------------------------------------------------

constexpr float CardWidth = 193;
constexpr float CardHeight = 55;
constexpr float RowPitch = 59;
constexpr float GridTop = 4;
constexpr float ColumnX[2] = {4, 203};
constexpr int VisibleRows = 4;
constexpr float NameScale = 0.38f;

const u32 CardFill = ui::color::rgb(0xFFFDF2);
const u32 CardEdge = ui::color::rgb(0xCFC795);

std::string platformName(const std::string &platform)
{
    if (platform == "3ds") {
        return "Nintendo 3DS";
    }
    if (platform == "nds") {
        return "Nintendo DS";
    }
    if (platform == "dsiware") {
        return "DSiWare";
    }
    if (platform == "gba") {
        return "Game Boy Advance";
    }
    if (platform == "virtual-console") {
        return "Virtual Console";
    }
    return platform.empty() ? "—" : platform;
}

std::string languageName(const std::string &language)
{
    if (language == "pt-BR") {
        return "Português (Brasil)";
    }
    if (language == "pt-PT" || language == "pt") {
        return "Português";
    }
    return language.empty() ? "—" : language;
}

std::string requirementName(const std::string &requirement)
{
    if (requirement == "luma3ds") {
        return "Luma3DS";
    }
    if (requirement == "layeredfs") {
        return "LayeredFS";
    }
    if (requirement == "twilightmenu") {
        return "TWiLight Menu++";
    }
    return requirement;
}

std::string join(const std::vector<std::string> &items, const char *separator)
{
    std::string out;
    for (const std::string &item : items) {
        if (!out.empty()) {
            out += separator;
        }
        out += item;
    }
    return out;
}

// "2026-10-01" -> "01/10/2026"
std::string brazilianDate(const std::string &iso)
{
    if (iso.size() >= 10 && iso[4] == '-' && iso[7] == '-') {
        return iso.substr(8, 2) + "/" + iso.substr(5, 2) + "/" + iso.substr(0, 4);
    }
    return iso;
}

// Regiões da primeira tradução; sem tradução, as dos Title IDs.
std::vector<std::string> regionsOf(const store::Game &game)
{
    if (!game.translations.empty() &&
        !game.translations.front().compatibility.regions.empty()) {
        return game.translations.front().compatibility.regions;
    }
    std::vector<std::string> out;
    for (const store::TitleId &title : game.titleIds) {
        if (std::find(out.begin(), out.end(), title.region) == out.end()) {
            out.push_back(title.region);
        }
    }
    return out;
}

// Ícones de região e de status (romfs:/ui/badges.png): 16x16, em células
// de 18 px, na ordem USA, EUR, JPN, KOR, ALL, INSTALADA, ATUALIZAÇÃO.
constexpr int BadgeSize = 16;
constexpr int BadgePitch = 18;
enum BadgeIcon { BadgeUSA, BadgeEUR, BadgeJPN, BadgeKOR, BadgeALL, BadgeInstalled,
                 BadgeUpdate, BadgeCount };

struct BadgeAtlas {
    bool tried = false;
    bool ready = false;
    C3D_Tex texture {};
    Tex3DS_SubTexture subs[BadgeCount] {};
    C2D_Image images[BadgeCount] {};
};
BadgeAtlas badges;

bool loadBadges()
{
    if (badges.tried) {
        return badges.ready;
    }
    badges.tried = true;
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc *pixels = stbi_load("romfs:/ui/badges.png", &width, &height, &channels, 4);
    if (pixels == nullptr) {
        return false;
    }
    constexpr int TexWidth = 128;
    constexpr int TexHeight = 16;
    if (width <= TexWidth && height <= TexHeight &&
        C3D_TexInit(&badges.texture, TexWidth, TexHeight, GPU_RGBA8)) {
        u32 *texels = static_cast<u32 *>(badges.texture.data);
        for (int i = 0; i < TexWidth * TexHeight; ++i) {
            texels[i] = 0;
        }
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const stbi_uc *p = pixels + (y * width + x) * 4;
                texels[image::tiledIndex(x, y, TexWidth)] =
                    (static_cast<u32>(p[0]) << 24) | (static_cast<u32>(p[1]) << 16) |
                    (static_cast<u32>(p[2]) << 8) | p[3];
            }
        }
        C3D_TexFlush(&badges.texture);
        C3D_TexSetFilter(&badges.texture, GPU_NEAREST, GPU_NEAREST);
        for (int i = 0; i < BadgeCount; ++i) {
            const float left = static_cast<float>(i * BadgePitch) / TexWidth;
            badges.subs[i] = {BadgeSize, BadgeSize, left, 1.0f,
                              left + static_cast<float>(BadgeSize) / TexWidth,
                              1.0f - static_cast<float>(BadgeSize) / TexHeight};
            badges.images[i] = {&badges.texture, &badges.subs[i]};
        }
        badges.ready = true;
    }
    stbi_image_free(pixels);
    return badges.ready;
}

int regionBadge(const std::string &region)
{
    if (region == "USA") {
        return BadgeUSA;
    }
    if (region == "EUR") {
        return BadgeEUR;
    }
    if (region == "JPN") {
        return BadgeJPN;
    }
    if (region == "KOR") {
        return BadgeKOR;
    }
    if (region == "ALL") {
        return BadgeALL;
    }
    return -1; // CHN, TWN...: selo de texto
}

// Desenha, da direita para a esquerda a partir de right: as regiões (até 2,
// em ordem de leitura) e, à esquerda delas, o status (1 instalada,
// 2 atualização). Retorna a posição x mais à esquerda usada.
float drawCardBadges(const std::vector<std::string> &regions, int status,
                     float right, float y)
{
    constexpr float Gap = 3;
    constexpr float TagScale = 0.34f;
    const bool icons = loadBadges();
    const size_t shown = std::min<size_t>(regions.size(), 2);
    float x = right;
    if (regions.size() > shown) {
        const std::string more = "+" + std::to_string(regions.size() - shown);
        x -= ui::textWidth(more, TagScale);
        ui::drawText(more, x, y + 2, TagScale, ui::color::InkSoft);
        x -= Gap;
    }
    for (size_t index = shown; index-- > 0;) {
        const int icon = regionBadge(regions[index]);
        if (icons && icon >= 0) {
            x -= BadgeSize;
            ui::drawImage(badges.images[icon], x, y);
        } else {
            const float width = ui::textWidth(regions[index], TagScale) + 8;
            x -= width;
            const ui::Rect tag = {x, y + 1.5f, width, 13};
            ui::fillRound(tag, 3, ui::color::Tag);
            ui::drawCentered(regions[index], tag, TagScale, ui::color::rgb(0xFFFFFF));
        }
        x -= Gap;
    }
    if (status != 0) {
        if (icons) {
            x -= BadgeSize;
            ui::drawImage(badges.images[status == 2 ? BadgeUpdate : BadgeInstalled], x, y);
        } else {
            x -= ui::drawBadge(x, y, status == 2 ? "Atualização" : "Instalada",
                               status == 2 ? ui::color::IconOrange : ui::color::Good);
        }
    }
    return x;
}

void drawIconOrPlaceholder(const store::Catalog &catalog,
                           const store::Image &picture, float x, float y)
{
    const C2D_Image *image =
        picture.empty() ? nullptr
                        : imagecache::icon(picture, catalog.resolveUrl(picture.url));
    if (image != nullptr) {
        ui::drawImage(*image, x, y);
        return;
    }
    ui::fillRound({x, y, 48, 48}, 4, ui::color::Recess);
    ui::drawIcon(ui::Icon::Picture, x + 12, y + 12, 24, ui::color::Neutral);
}

// Selo de status do cartão: 0 nenhum, 1 instalada, 2 atualização disponível.
using StatusFn = std::function<int(const store::Game &)>;

int catalogStatus(const store::Game &game)
{
    int status = 0;
    for (const store::Translation &t : game.translations) {
        const std::string current = installed::version(t.id);
        if (current.empty()) {
            continue;
        }
        status = std::max(status, semver::compare(t.version, current) > 0 ? 2 : 1);
    }
    return status;
}

class Grid {
public:
    int selection = 0;
    StatusFn status = catalogStatus;

    // Move a seleção; retorna true se mudou.
    bool navigate(const ui::Input &input, int count)
    {
        if (count == 0) {
            return false;
        }
        const int before = selection;
        if (input.repeat & KEY_DLEFT) {
            if (selection % 2 == 1) {
                --selection;
            }
        }
        if (input.repeat & KEY_DRIGHT) {
            if (selection % 2 == 0 && selection + 1 < count) {
                ++selection;
            }
        }
        if (input.repeat & KEY_DUP) {
            if (selection >= 2) {
                selection -= 2;
            }
        }
        if (input.repeat & KEY_DDOWN) {
            if (selection + 2 < count) {
                selection += 2;
            } else if (selection / 2 < (count - 1) / 2) {
                selection = count - 1; // última linha incompleta
            }
        }
        clamp(count);
        return selection != before;
    }

    void clamp(int count)
    {
        selection = std::max(0, std::min(selection, count - 1));
        const int row = selection / 2;
        if (row < topRow_) {
            topRow_ = row;
        }
        if (row > topRow_ + VisibleRows - 1) {
            topRow_ = row - (VisibleRows - 1);
        }
    }

    void draw(const store::Catalog &catalog,
              const std::vector<const store::Game *> &games,
              const FavoriteGames &favorites, const std::string &emptyText)
    {
        if (games.empty()) {
            ui::drawCentered(emptyText, {20, 0, 360, 240}, ui::type::Body,
                             ui::color::InkSoft);
            return;
        }
        // Rolagem suave até a linha de cima desejada.
        const float target = topRow_ * RowPitch;
        scroll_ += (target - scroll_) * 0.35f;
        if (std::fabs(target - scroll_) < 0.5f) {
            scroll_ = target;
        }

        const int rows = (static_cast<int>(games.size()) + 1) / 2;
        const int firstRow = std::max(0, static_cast<int>(scroll_ / RowPitch) - 1);
        // Linhas visíveis e uma a mais em cada lado (ícones já vão carregando).
        for (int row = firstRow; row < std::min(rows, firstRow + VisibleRows + 3);
             ++row) {
            for (int column = 0; column < 2; ++column) {
                const int index = row * 2 + column;
                if (index >= static_cast<int>(games.size())) {
                    break;
                }
                const float y = GridTop + row * RowPitch - scroll_;
                drawCard(catalog, *games[index], favorites,
                         {ColumnX[column], y, CardWidth, CardHeight},
                         index == selection);
            }
        }
        // Barra de rolagem fina à direita.
        if (rows > VisibleRows) {
            const float track = 240 - 8;
            const float thumb = std::max(20.0f, track * VisibleRows / rows);
            const float maxScroll = (rows - VisibleRows) * RowPitch;
            const float y = 4 + (track - thumb) * std::min(1.0f, scroll_ / maxScroll);
            ui::fillRound({397, y, 2.5f, thumb}, 1, ui::color::Dot);
        }
    }

private:
    void drawCard(const store::Catalog &catalog, const store::Game &game,
                  const FavoriteGames &favorites, const ui::Rect &rect,
                  bool selected)
    {
        if (rect.y + rect.h < 0 || rect.y > 240) {
            // Fora da tela: só pede o ícone para já ir carregando.
            if (!game.icon.empty()) {
                imagecache::icon(game.icon, catalog.resolveUrl(game.icon.url));
            }
            return;
        }
        ui::fillRound(rect, 6, CardEdge);
        ui::fillRound(rect.inset(1), 5, CardFill);
        drawIconOrPlaceholder(catalog, game.icon, rect.x + 4, rect.y + 4);

        const float textX = rect.x + 58;
        const float textWidth = rect.w - 58 - 6;
        const std::vector<std::string> lines =
            ui::wrap(game.name, NameScale, textWidth - (favorites.contains(game.id) ? 14 : 0), 2);
        float y = rect.y + 4;
        for (const std::string &line : lines) {
            ui::drawText(line, textX, y, NameScale, ui::color::InkStrong);
            y += ui::lineHeight(NameScale) * 0.82f;
        }
        drawCardBadges(regionsOf(game), status ? status(game) : 0,
                       rect.x + rect.w - 6, rect.y + rect.h - BadgeSize - 4);
        if (favorites.contains(game.id)) {
            ui::drawIcon(ui::Icon::Star, rect.x + rect.w - 18, rect.y + 4, 14,
                         ui::color::IconGold);
        }
        if (selected) {
            ui::drawFocus(rect, 6);
        }
    }

    int topRow_ = 0;
    float scroll_ = 0;
};

// --- Detalhes (tela inferior) ---------------------------------------------------

const ui::Rect DetailArea = {0, 0, 320, 162};
const ui::Rect FavoriteButton = {8, 166, 148, 36};
const ui::Rect InstallButton = {164, 166, 148, 36};

constexpr float TagScale = 0.36f;
constexpr float ValueScale = 0.44f;
constexpr float LineStep = 18;

// Etiqueta azul + valor numa linha. Retorna o y da próxima linha.
float tagLine(float x, float y, float tagWidth, float totalWidth,
              const std::string &tag, const std::string &value)
{
    if (!tag.empty()) {
        ui::fillRound({x, y, tagWidth, 15}, 4, ui::color::Tag);
        ui::drawCentered(tag, {x, y, tagWidth, 15}, TagScale,
                         ui::color::rgb(0xFFFFFF));
    }
    ui::drawCenteredMarquee(value, {x + tagWidth + 6, y, totalWidth - tagWidth - 6, 15},
                            ValueScale, ui::color::Ink, true);
    return y + LineStep;
}

float paragraph(const std::string &text, float x, float y, float width)
{
    if (text.empty()) {
        return y;
    }
    return ui::drawWrapped(text, x, y, ValueScale, ui::color::Ink, width, 60);
}

class Details {
public:
    int translation = 0;

    void show(const std::string &gameId)
    {
        if (gameId != shown_) {
            shown_ = gameId;
            translation = 0;
            scroller_.setOffset(0);
        }
    }

    void scroll(const ui::Input &input)
    {
        scroller_.update(input, DetailArea, content_, DetailArea.h);
        // Circle Pad rola os detalhes (o direcional fica com a grade).
        circlePosition circle;
        hidCircleRead(&circle);
        constexpr int DeadZone = 20;
        if (std::abs(circle.dy) > DeadZone) {
            const float maximum = std::max(0.0f, content_ - DetailArea.h);
            const float speed = circle.dy / 156.0f * 7.0f;
            scroller_.setOffset(std::max(
                0.0f, std::min(maximum, scroller_.offset() - speed)));
        }
    }

    void draw(const store::Catalog &catalog, const store::Game &game)
    {
        ui::pushClip(DetailArea);
        const float top = 6 - scroller_.offset();
        float y = top;

        // Capa à esquerda, dados do jogo à direita.
        const C2D_Image *cover =
            game.cover.empty()
                ? nullptr
                : imagecache::cover(game.cover, catalog.resolveUrl(game.cover.url));
        if (cover != nullptr) {
            ui::drawImage(*cover, 8, y);
        } else {
            ui::fillRound({8, y, 112, 128}, 5, ui::color::Recess);
            ui::drawIcon(ui::Icon::Picture, 48, y + 48, 32, ui::color::Neutral);
        }
        constexpr float RightX = 128;
        constexpr float RightWidth = 184;
        float right = ui::drawWrapped(game.name, RightX, y, 0.56f,
                                      ui::color::InkStrong, RightWidth, 3) + 4;
        // Balões 10% maiores que antes (62 -> 68), para "Plataforma" caber.
        constexpr float GameTag = 68;
        right = tagLine(RightX, right, GameTag, RightWidth, "Plataforma",
                        platformName(game.platform));
        right = tagLine(RightX, right, GameTag, RightWidth, "ID", game.id);
        for (const store::TitleId &title : game.titleIds) {
            right = tagLine(RightX, right, GameTag, RightWidth, "Title ID",
                            title.region + " " + title.id);
        }
        // Versão exigida do jogo, da tradução selecionada.
        if (!game.translations.empty()) {
            const int index = std::max(
                0, std::min(translation,
                            static_cast<int>(game.translations.size()) - 1));
            const std::vector<std::string> &versions =
                game.translations[index].compatibility.gameVersions;
            right = tagLine(RightX, right, GameTag, RightWidth, "Versão",
                            versions.empty() ? "Qualquer" : join(versions, ", "));
        }
        y = std::max(y + 128, right) + 8;
        y = paragraph(game.description, 8, y, 304) + 6;

        // Tradução selecionada.
        ui::fillRound({8, y, 304, 1.5f}, 0, ui::color::Dot);
        y += 8;
        if (game.translations.empty()) {
            ui::drawText("Nenhuma tradução disponível.", 8, y, ValueScale,
                         ui::color::InkSoft);
            y += LineStep;
        } else {
            const int count = static_cast<int>(game.translations.size());
            translation = std::max(0, std::min(translation, count - 1));
            const store::Translation &t = game.translations[translation];
            if (count > 1) {
                ui::drawText("Tradução " + std::to_string(translation + 1) + "/" +
                                 std::to_string(count),
                             8, y, 0.42f, ui::color::Accent);
                ui::drawText("L / R para trocar", 312, y, 0.38f,
                             ui::color::InkSoft, C2D_AlignRight);
                y += 16;
            }
            y = ui::drawWrapped(t.name, 8, y, 0.52f, ui::color::InkStrong, 304, 3) + 4;
            constexpr float TagWidth = 110;
            y = tagLine(8, y, TagWidth, 304, "Idioma", languageName(t.language));
            y = tagLine(8, y, TagWidth, 304, "Versão", t.version);
            if (!t.releaseDate.empty()) {
                y = tagLine(8, y, TagWidth, 304, "Lançamento",
                            brazilianDate(t.releaseDate));
            }
            for (size_t index = 0; index < t.authors.size(); ++index) {
                const store::Author &author = t.authors[index];
                y = tagLine(8, y, TagWidth, 304, index == 0 ? "Equipe" : "",
                            author.role.empty() ? author.name
                                                : author.name + " — " + author.role);
            }
            if (!t.compatibility.regions.empty()) {
                y = tagLine(8, y, TagWidth, 304, "Regiões",
                            join(t.compatibility.regions, ", "));
            }
            y = tagLine(8, y, TagWidth, 304, "Consoles",
                        t.compatibility.consoles == "new3ds"
                            ? "Somente New 3DS / New 2DS XL"
                            : "Todos os modelos");
            if (!t.compatibility.requirements.empty()) {
                std::vector<std::string> names;
                for (const std::string &item : t.compatibility.requirements) {
                    names.push_back(requirementName(item));
                }
                y = tagLine(8, y, TagWidth, 304, "Requisitos", join(names, ", "));
            }
            if (t.downloadSize > 0) {
                y = tagLine(8, y, TagWidth, 304, "Download",
                            units::bytes(t.downloadSize));
            }
            if (!t.compatibility.notes.empty()) {
                y = tagLine(8, y, TagWidth, 304, "Observações", "");
                y = paragraph(t.compatibility.notes, 8, y, 304);
            }
            y = paragraph(t.description, 8, y + 4, 304);
        }
        content_ = (y - top) + 10;
        ui::popClip();

        // Barra de rolagem.
        if (content_ > DetailArea.h) {
            const float thumb = std::max(16.0f, DetailArea.h * DetailArea.h / content_);
            const float maxOffset = content_ - DetailArea.h;
            const float ty = (DetailArea.h - thumb) *
                             std::min(1.0f, std::max(0.0f, scroller_.offset() / maxOffset));
            ui::fillRound({315, ty + 2, 2.5f, thumb - 4}, 1, ui::color::Dot);
        }
    }

private:
    std::string shown_;
    ui::Scroller scroller_;
    float content_ = 0;
};

std::vector<const store::Game *> listFor(const store::Catalog &catalog,
                                         const FavoriteGames &favorites,
                                         BrowserMode mode)
{
    std::vector<const store::Game *> out;
    for (const store::Game &game : catalog.games) {
        if (mode == BrowserMode::Catalog || favorites.contains(game.id)) {
            out.push_back(&game);
        }
    }
    return out;
}

bool released(const ui::Input &input, const ui::Rect &rect)
{
    return (input.up & KEY_TOUCH) && rect.contains(input.touch);
}

bool held(const ui::Input &input, const ui::Rect &rect)
{
    return (input.held & KEY_TOUCH) && rect.contains(input.touch);
}

// --- Detalhes + botões, comum a Catálogo, Favoritos e Busca -------------------

enum class InstallState { Unavailable, NeedsNewerApp, Pending, Install, Update, Reinstall };

InstallState installState(const store::Translation &t)
{
    if (t.needsNewerApp()) {
        return InstallState::NeedsNewerApp;
    }
    if (jobs::pending(t.id)) {
        return InstallState::Pending;
    }
    const std::string current = installed::version(t.id);
    if (current.empty()) {
        return InstallState::Install;
    }
    return semver::compare(t.version, current) > 0 ? InstallState::Update
                                                   : InstallState::Reinstall;
}

const store::Translation *selectedTranslation(const store::Game &game,
                                              const Details &details)
{
    if (game.translations.empty()) {
        return nullptr;
    }
    const int index = std::max(
        0, std::min(details.translation, static_cast<int>(game.translations.size()) - 1));
    return &game.translations[index];
}

// Confirma e coloca a tradução na fila.
void requestInstall(const store::Catalog &catalog, const store::Game &game,
                    const store::Translation &t, InstallState state)
{
    const jobs::Kind kind = state == InstallState::Update ? jobs::Kind::Update
                                                          : jobs::Kind::Install;
    const char *verb = state == InstallState::Update      ? "Atualizar"
                       : state == InstallState::Reinstall ? "Reinstalar"
                                                          : "Instalar";
    ui::Dialog dialog;
    dialog.header = verb;
    dialog.title = std::string(verb) + " a tradução de " + game.name + "?";
    dialog.body.push_back({t.name, ui::color::Text, 0.5f, 2});
    std::string facts = "Versão " + t.version;
    if (t.downloadSize > 0) {
        facts += "  ·  Download " + units::bytes(t.downloadSize);
    }
    facts += "  ·  " + std::to_string(t.install.size()) + " etapas";
    dialog.body.push_back({facts, ui::color::InkSoft, 0.42f, 2});
    if (t.compatibility.consoles == "new3ds") {
        dialog.body.push_back({"Esta tradução funciona só no New 3DS / New 2DS XL.",
                               ui::color::Caution, 0.42f, 2});
    }
    dialog.icon = game.icon.empty()
                      ? nullptr
                      : imagecache::icon(game.icon, catalog.resolveUrl(game.icon.url));
    dialog.buttons = {verb, "Cancelar"};
    dialog.cancelButton = 1;
    if (ui::choose(dialog) != 0) {
        return;
    }
    std::string error;
    if (jobs::enqueue(catalog, game, t, kind, error) < 0) {
        ui::message("Não foi possível instalar", error, ui::Tone::Error);
        return;
    }
    ui::notify({"Adicionado à fila", game.name, ui::Icon::Download,
                ui::color::IconGreen});
}

enum class DetailResult { None, Favorited, Unfavorited, Dialog };

// Teclas e toques da tela inferior quando ela mostra os detalhes.
DetailResult detailsInput(const ui::Input &input, const store::Catalog &catalog,
                          const store::Game &game, Details &details,
                          FavoriteGames &favorites)
{
    details.show(game.id);
    const int translations = static_cast<int>(game.translations.size());
    if ((input.down & KEY_L) && details.translation > 0) {
        --details.translation;
        sound::play(sound::Effect::Move);
    }
    if ((input.down & KEY_R) && details.translation + 1 < translations) {
        ++details.translation;
        sound::play(sound::Effect::Move);
    }
    details.scroll(input);

    if ((input.down & KEY_X) || released(input, FavoriteButton)) {
        const bool now = favorites.toggle(game.id);
        sound::play(sound::Effect::Select);
        ui::notify({now ? "Adicionado aos favoritos" : "Removido dos favoritos",
                    game.name, ui::Icon::Star, ui::color::IconGold});
        return now ? DetailResult::Favorited : DetailResult::Unfavorited;
    }
    const store::Translation *t = selectedTranslation(game, details);
    if (t != nullptr && ((input.down & KEY_A) || released(input, InstallButton))) {
        const InstallState state = installState(*t);
        if (state == InstallState::Install || state == InstallState::Update ||
            state == InstallState::Reinstall) {
            sound::play(sound::Effect::Select);
            requestInstall(catalog, game, *t, state);
            return DetailResult::Dialog;
        }
    }
    return DetailResult::None;
}

void drawDetailsPane(const store::Catalog &catalog, const store::Game &game,
                     Details &details, const FavoriteGames &favorites,
                     const ui::Input &input)
{
    details.draw(catalog, game);
    const bool favorite = favorites.contains(game.id);
    ui::drawButton(FavoriteButton, favorite ? "Desfavoritar" : "Favoritar",
                   ui::Style::Normal, 0.5f,
                   favorite ? ui::Icon::Star : ui::Icon::StarOutline,
                   ui::color::IconGold, held(input, FavoriteButton));
    const store::Translation *t = selectedTranslation(game, details);
    const InstallState state = t == nullptr ? InstallState::Unavailable : installState(*t);
    switch (state) {
    case InstallState::Unavailable:
        ui::drawButton(InstallButton, "Instalar", ui::Style::Disabled, 0.5f);
        break;
    case InstallState::NeedsNewerApp:
        ui::drawButton(InstallButton, "Requer app novo", ui::Style::Disabled, 0.44f);
        break;
    case InstallState::Pending:
        ui::drawButton(InstallButton, "Na fila", ui::Style::Disabled, 0.5f);
        break;
    case InstallState::Install:
    case InstallState::Update:
    case InstallState::Reinstall:
        ui::drawButton(InstallButton,
                       state == InstallState::Update      ? "Atualizar"
                       : state == InstallState::Reinstall ? "Reinstalar"
                                                          : "Instalar",
                       ui::Style::Normal, 0.5f,
                       state == InstallState::Install ? ui::Icon::Download
                                                      : ui::Icon::Refresh,
                       ui::color::IconGreen, held(input, InstallButton));
        break;
    }
}

// --- Teclado da Busca -----------------------------------------------------------

constexpr size_t MaxTerm = 32;
const ui::Rect SearchField = {8, 6, 304, 28};
const ui::Rect ClearButton = {282, 8, 28, 24};

struct Key {
    ui::Rect rect;
    std::string label;
    std::string text; // o que a tecla digita; vazio = apagar
};

std::vector<Key> buildKeyboard()
{
    std::vector<Key> keys;
    constexpr float Size = 28;
    constexpr float Gap = 3;
    constexpr float Pitch = Size + Gap;
    constexpr float Left = 6.5f;
    const char *rows[4] = {"1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
    for (int row = 0; row < 4; ++row) {
        const float y = 42 + row * 32;
        const std::string letters = rows[row];
        for (size_t column = 0; column < letters.size(); ++column) {
            const std::string label(1, letters[column]);
            std::string text = label;
            text[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(text[0])));
            keys.push_back({{Left + column * Pitch, y, Size, Size}, label, text});
        }
    }
    // Apagar ao fim da 3ª linha; espaço ao fim da 4ª.
    keys.push_back({{Left + 9 * Pitch, 42 + 2 * 32, Size, Size}, "<", ""});
    keys.push_back({{Left + 7 * Pitch, 42 + 3 * 32, 3 * Pitch - Gap, Size},
                    "Espaço", " "});
    return keys;
}

void drawKeyboard(const std::vector<Key> &keys, const std::string &term,
                  const ui::Input &input)
{
    ui::drawRecess(SearchField);
    ui::drawIcon(ui::Icon::Search, SearchField.x + 6, SearchField.y + 5, 18,
                 ui::color::Neutral);
    const float textX = SearchField.x + 30;
    if (term.empty()) {
        ui::drawText("Nome do jogo", textX, SearchField.y + 6, 0.5f,
                     ui::color::InkSoft);
    } else {
        ui::drawText(term, textX, SearchField.y + 6, 0.5f, ui::color::Ink);
    }
    // Cursor piscando.
    if ((ui::now() / 500) % 2 == 0) {
        const float caret = textX + (term.empty() ? 0 : ui::textWidth(term, 0.5f)) + 1;
        ui::fillRound({caret, SearchField.y + 6, 1.5f, 16}, 0, ui::color::Ink);
    }
    if (!term.empty()) {
        ui::drawButton(ClearButton, "", ui::Style::Normal, 0.4f, ui::Icon::Trash,
                       ui::color::IconGrey, held(input, ClearButton));
    }
    for (const Key &key : keys) {
        ui::drawButton(key.rect, key.label, ui::Style::Normal,
                       key.label.size() > 1 ? 0.42f : 0.52f, ui::Icon::None,
                       0, held(input, key.rect));
    }
    ui::drawText("Y apaga  ·  A abre o jogo selecionado", 160, 172, 0.36f,
                 ui::color::InkSoft, C2D_AlignCenter);
}

std::vector<const store::Game *> searchFor(const store::Catalog &catalog,
                                           const std::string &term)
{
    std::vector<const store::Game *> out;
    std::string wanted = store::fold(term);
    while (!wanted.empty() && wanted.back() == ' ') {
        wanted.pop_back();
    }
    while (!wanted.empty() && wanted.front() == ' ') {
        wanted.erase(wanted.begin());
    }
    if (wanted.empty()) {
        return out;
    }
    for (const store::Game &game : catalog.games) {
        if (store::fold(game.name).find(wanted) != std::string::npos) {
            out.push_back(&game);
        }
    }
    return out;
}

} // namespace

void runGameBrowser(const store::Catalog &catalog, FavoriteGames &favorites,
                    BrowserMode mode)
{
    const bool favoritesOnly = mode == BrowserMode::Favorites;
    const char *screenId = favoritesOnly ? "favorites" : "catalog";
    std::vector<const store::Game *> games = listFor(catalog, favorites, mode);
    Grid grid;
    Details details;
    ui::enterScreen(screenId);
    const std::string emptyText =
        favoritesOnly ? "Nenhum favorito." : "Nenhum jogo no catálogo.";

    while (ui::keepRunning()) {
        imagecache::update();
        const ui::Input input = ui::readInput();
        const int count = static_cast<int>(games.size());

        if ((input.down & KEY_B) || released(input, ui::tabRect(ui::Tab::Left))) {
            sound::play(sound::Effect::Back);
            return;
        }
        if (grid.navigate(input, count)) {
            sound::play(sound::Effect::Move);
        }
        const store::Game *game = count > 0 ? games[grid.selection] : nullptr;
        if (game != nullptr) {
            const DetailResult result = detailsInput(input, catalog, *game, details, favorites);
            if (result == DetailResult::Unfavorited && favoritesOnly) {
                games = listFor(catalog, favorites, mode);
                grid.clamp(static_cast<int>(games.size()));
                continue;
            }
            if (result == DetailResult::Dialog) {
                ui::enterScreen(screenId);
                continue;
            }
        }

        ui::beginFrame();
        grid.draw(catalog, games, favorites, emptyText);
        ui::beginBottom();
        if (game != nullptr) {
            drawDetailsPane(catalog, *game, details, favorites, input);
        } else {
            ui::drawWrapped(favoritesOnly ? "Use X no Catálogo para favoritar um jogo."
                                          : "Atualize o catálogo na tela inicial.",
                            160, 80, ui::type::Small, ui::color::InkSoft, 280, 3,
                            C2D_AlignCenter);
        }
        ui::drawTab(ui::Tab::Left, "Voltar", "B",
                    held(input, ui::tabRect(ui::Tab::Left)));
        ui::endFrame();
    }
}

void runSearch(const store::Catalog &catalog, FavoriteGames &favorites,
               std::string &term)
{
    const std::vector<Key> keys = buildKeyboard();
    std::vector<const store::Game *> games = searchFor(catalog, term);
    Grid grid;
    Details details;
    bool showingDetails = false;
    ui::enterScreen("search");

    while (ui::keepRunning()) {
        imagecache::update();
        const ui::Input input = ui::readInput();
        const int count = static_cast<int>(games.size());
        const ui::Rect backTab = ui::tabRect(ui::Tab::Left);

        if ((input.down & KEY_B) || released(input, backTab)) {
            sound::play(sound::Effect::Back);
            if (!showingDetails) {
                return;
            }
            showingDetails = false; // volta ao teclado com o termo mantido
            continue;
        }
        if (grid.navigate(input, count)) {
            sound::play(sound::Effect::Move);
        }
        const store::Game *game = count > 0 ? games[grid.selection] : nullptr;

        if (showingDetails && game != nullptr) {
            if (detailsInput(input, catalog, *game, details, favorites) == DetailResult::Dialog) {
                ui::enterScreen("search");
                continue;
            }
        } else {
            showingDetails = false;
            std::string before = term;
            if ((input.down & KEY_A) && game != nullptr) {
                sound::play(sound::Effect::Select);
                details.show(game->id);
                showingDetails = true;
                continue;
            }
            if ((input.repeat & KEY_Y) && !term.empty()) {
                term.pop_back();
            }
            if (!term.empty() && released(input, ClearButton)) {
                term.clear();
            } else if (input.up & KEY_TOUCH) {
                for (const Key &key : keys) {
                    if (!key.rect.contains(input.touch)) {
                        continue;
                    }
                    if (key.text.empty()) {
                        if (!term.empty()) {
                            term.pop_back();
                        }
                    } else if (term.size() < MaxTerm &&
                               !(key.text == " " && (term.empty() || term.back() == ' '))) {
                        term += key.text;
                    }
                    break;
                }
            }
            if (term != before) {
                sound::play(sound::Effect::Move);
                games = searchFor(catalog, term);
                grid.selection = 0;
                grid.clamp(static_cast<int>(games.size()));
            }
        }

        ui::beginFrame();
        grid.draw(catalog, games, favorites,
                  term.empty() ? "Digite o nome do jogo" : "Nenhum jogo encontrado");
        ui::beginBottom();
        if (showingDetails && game != nullptr) {
            drawDetailsPane(catalog, *game, details, favorites, input);
            ui::drawTab(ui::Tab::Left, "Teclado", "B", held(input, backTab));
        } else {
            drawKeyboard(keys, term, input);
            ui::drawTab(ui::Tab::Left, "Voltar", "B", held(input, backTab));
        }
        ui::endFrame();
    }
}

// --- Biblioteca -----------------------------------------------------------------

namespace {

struct LibraryEntry {
    const store::Game *game = nullptr;           // do catálogo, ou criado do manifesto
    std::vector<installed::Manifest> manifests;  // traduções instaladas deste jogo
};

struct LibraryList {
    std::deque<store::Game> extras; // jogos que saíram do catálogo
    std::vector<LibraryEntry> entries;
    std::vector<const store::Game *> games;
};

const store::Translation *findTranslation(const store::Catalog &catalog,
                                          const std::string &id,
                                          const store::Game **owner = nullptr)
{
    for (const store::Game &game : catalog.games) {
        for (const store::Translation &t : game.translations) {
            if (t.id == id) {
                if (owner != nullptr) {
                    *owner = &game;
                }
                return &t;
            }
        }
    }
    return nullptr;
}

void buildLibrary(const store::Catalog &catalog, LibraryList &out)
{
    out = LibraryList {};
    std::map<std::string, size_t> byGame;
    for (installed::Manifest &manifest : installed::all()) {
        const store::Game *game = catalog.findGame(manifest.gameId);
        if (game == nullptr) {
            findTranslation(catalog, manifest.translationId, &game);
        }
        const std::string key = game != nullptr ? game->id : "#" + manifest.gameId;
        auto found = byGame.find(key);
        if (found == byGame.end()) {
            if (game == nullptr) {
                store::Game extra;
                extra.id = manifest.gameId;
                extra.name = manifest.gameName.empty() ? manifest.translationName
                                                       : manifest.gameName;
                extra.icon.url = manifest.iconUrl;
                extra.icon.sha256 = manifest.iconSha256;
                out.extras.push_back(extra);
                game = &out.extras.back();
            }
            byGame[key] = out.entries.size();
            out.entries.push_back({game, {}});
            found = byGame.find(key);
        }
        out.entries[found->second].manifests.push_back(manifest);
    }
    std::sort(out.entries.begin(), out.entries.end(),
              [](const LibraryEntry &a, const LibraryEntry &b) {
                  return store::fold(a.game->name) < store::fold(b.game->name);
              });
    for (const LibraryEntry &entry : out.entries) {
        out.games.push_back(entry.game);
    }
}

std::string isoToBrazilian(const std::string &iso)
{
    return iso.size() >= 10 ? brazilianDate(iso.substr(0, 10)) : iso;
}

const ui::Rect UpdateButton = {8, 166, 148, 36};
const ui::Rect UninstallButton = {164, 166, 148, 36};

class LibraryDetails {
public:
    int translation = 0;

    void show(const std::string &gameId)
    {
        if (gameId != shown_) {
            shown_ = gameId;
            translation = 0;
            scroller_.setOffset(0);
        }
    }

    void scroll(const ui::Input &input)
    {
        scroller_.update(input, DetailArea, content_, DetailArea.h);
        circlePosition circle;
        hidCircleRead(&circle);
        if (std::abs(circle.dy) > 20) {
            const float maximum = std::max(0.0f, content_ - DetailArea.h);
            scroller_.setOffset(std::max(
                0.0f, std::min(maximum, scroller_.offset() - circle.dy / 156.0f * 7.0f)));
        }
    }

    void draw(const store::Catalog &catalog, const LibraryEntry &entry)
    {
        const store::Game &game = *entry.game;
        const int count = static_cast<int>(entry.manifests.size());
        translation = std::max(0, std::min(translation, count - 1));
        const installed::Manifest &m = entry.manifests[translation];
        const store::Translation *t = findTranslation(catalog, m.translationId);

        ui::pushClip(DetailArea);
        const float top = 6 - scroller_.offset();
        float y = top;
        const C2D_Image *cover =
            game.cover.empty()
                ? nullptr
                : imagecache::cover(game.cover, catalog.resolveUrl(game.cover.url));
        if (cover != nullptr) {
            ui::drawImage(*cover, 8, y);
        } else {
            ui::fillRound({8, y, 112, 128}, 5, ui::color::Recess);
            ui::drawIcon(ui::Icon::Picture, 48, y + 48, 32, ui::color::Neutral);
        }
        constexpr float RightX = 128;
        constexpr float RightWidth = 184;
        constexpr float GameTag = 68;
        float right = ui::drawWrapped(game.name, RightX, y, 0.56f,
                                      ui::color::InkStrong, RightWidth, 3) + 4;
        right = tagLine(RightX, right, GameTag, RightWidth, "Plataforma",
                        platformName(game.platform));
        for (const store::TitleId &title : game.titleIds) {
            right = tagLine(RightX, right, GameTag, RightWidth, "Title ID",
                            title.region + " " + title.id);
        }
        y = std::max(y + 128, right) + 8;

        ui::fillRound({8, y, 304, 1.5f}, 0, ui::color::Dot);
        y += 8;
        if (count > 1) {
            ui::drawText("Tradução " + std::to_string(translation + 1) + "/" +
                             std::to_string(count),
                         8, y, 0.42f, ui::color::Accent);
            ui::drawText("L / R para trocar", 312, y, 0.38f, ui::color::InkSoft,
                         C2D_AlignRight);
            y += 16;
        }
        constexpr float TagWidth = 110;
        y = ui::drawWrapped(m.translationName, 8, y, 0.52f, ui::color::InkStrong,
                            304, 3) + 4;
        y = tagLine(8, y, TagWidth, 304, "Versão instalada", m.version);
        if (t != nullptr) {
            y = tagLine(8, y, TagWidth, 304, "Versão do jogo",
                        t->compatibility.gameVersions.empty()
                            ? "Qualquer"
                            : join(t->compatibility.gameVersions, ", "));
        }
        y = tagLine(8, y, TagWidth, 304, "Instalada em", isoToBrazilian(m.installedAt));
        y = tagLine(8, y, TagWidth, 304, "Versão disponível",
                    t == nullptr ? "Fora do catálogo"
                    : semver::compare(t->version, m.version) > 0
                        ? t->version + " (atualização)"
                        : t->version + " (atual)");
        y = tagLine(8, y, TagWidth, 304, "Arquivos", filesText(m));
        if (!m.cias.empty()) {
            y = tagLine(8, y, TagWidth, 304, "CIA", join(m.cias, ", "));
        }
        content_ = (y - top) + 10;
        ui::popClip();
    }

private:
    std::string filesText(const installed::Manifest &m)
    {
        const auto found = sizes_.find(m.translationId);
        uint64_t bytes = 0;
        if (found != sizes_.end()) {
            bytes = found->second;
        } else {
            for (const std::string &file : m.files) {
                const int64_t size = fs::fileSize(file);
                if (size > 0) {
                    bytes += static_cast<uint64_t>(size);
                }
            }
            sizes_[m.translationId] = bytes;
        }
        return std::to_string(m.files.size()) +
               (m.files.size() == 1 ? " arquivo · " : " arquivos · ") +
               units::bytes(bytes);
    }

    std::string shown_;
    ui::Scroller scroller_;
    float content_ = 0;
    std::map<std::string, uint64_t> sizes_;
};

void requestUninstall(const store::Catalog &catalog, const store::Game &game,
                      const installed::Manifest &m)
{
    ui::Dialog dialog;
    dialog.header = "Desinstalar";
    dialog.title = "Desinstalar a tradução de " + game.name + "?";
    dialog.tone = ui::Tone::Warning;
    dialog.body.push_back({m.translationName, ui::color::Text, 0.5f, 2});
    dialog.body.push_back({"Os arquivos da tradução serão apagados do cartão SD.",
                           ui::color::InkSoft, 0.42f, 2});
    dialog.icon = game.icon.empty()
                      ? nullptr
                      : imagecache::icon(game.icon, catalog.resolveUrl(game.icon.url));
    dialog.buttons = {"Desinstalar", "Cancelar"};
    dialog.dangerous = 0;
    dialog.cancelButton = 1;
    if (ui::choose(dialog) != 0) {
        return;
    }
    std::string error;
    if (jobs::enqueueUninstall(m.translationId,
                               findTranslation(catalog, m.translationId), error) < 0) {
        ui::message("Não foi possível desinstalar", error, ui::Tone::Error);
        return;
    }
    ui::notify({"Adicionado à fila", game.name, ui::Icon::Trash, ui::color::IconGrey});
}

} // namespace

void runLibrary(const store::Catalog &catalog, FavoriteGames &favorites)
{
    LibraryList list;
    buildLibrary(catalog, list);
    unsigned generation = installed::generation();
    Grid grid;
    // Na Biblioteca todo cartão tem selo: instalada ou atualização.
    grid.status = [&catalog, &list](const store::Game &game) {
        for (const LibraryEntry &entry : list.entries) {
            if (entry.game != &game) {
                continue;
            }
            for (const installed::Manifest &m : entry.manifests) {
                const store::Translation *t = findTranslation(catalog, m.translationId);
                if (t != nullptr && semver::compare(t->version, m.version) > 0) {
                    return 2;
                }
            }
        }
        return 1;
    };
    LibraryDetails details;
    ui::enterScreen("library");

    while (ui::keepRunning()) {
        imagecache::update();
        const ui::Input input = ui::readInput();
        if (installed::generation() != generation) {
            generation = installed::generation();
            buildLibrary(catalog, list);
            grid.clamp(static_cast<int>(list.games.size()));
        }
        const int count = static_cast<int>(list.entries.size());
        const ui::Rect backTab = ui::tabRect(ui::Tab::Left);
        if ((input.down & KEY_B) || released(input, backTab)) {
            sound::play(sound::Effect::Back);
            return;
        }
        if (grid.navigate(input, count)) {
            sound::play(sound::Effect::Move);
        }

        const LibraryEntry *entry = count > 0 ? &list.entries[grid.selection] : nullptr;
        bool updatable = false;
        bool pending = false;
        if (entry != nullptr) {
            details.show(entry->game->id);
            const int translations = static_cast<int>(entry->manifests.size());
            if ((input.down & KEY_L) && details.translation > 0) {
                --details.translation;
                sound::play(sound::Effect::Move);
            }
            if ((input.down & KEY_R) && details.translation + 1 < translations) {
                ++details.translation;
                sound::play(sound::Effect::Move);
            }
            details.scroll(input);
            const installed::Manifest &m =
                entry->manifests[std::max(0, std::min(details.translation, translations - 1))];
            const store::Game *owner = nullptr;
            const store::Translation *t = findTranslation(catalog, m.translationId, &owner);
            pending = jobs::pending(m.translationId);
            updatable = !pending && t != nullptr && owner != nullptr &&
                        !t->needsNewerApp() && semver::compare(t->version, m.version) > 0;

            if (updatable && ((input.down & KEY_A) || released(input, UpdateButton))) {
                sound::play(sound::Effect::Select);
                requestInstall(catalog, *owner, *t, InstallState::Update);
                ui::enterScreen("library");
                continue;
            }
            if (!pending && ((input.down & KEY_Y) || released(input, UninstallButton))) {
                sound::play(sound::Effect::Select);
                requestUninstall(catalog, *entry->game, m);
                ui::enterScreen("library");
                continue;
            }
        }

        ui::beginFrame();
        grid.draw(catalog, list.games, favorites, "Nenhuma tradução instalada.");
        ui::beginBottom();
        if (entry != nullptr) {
            details.draw(catalog, *entry);
            if (pending) {
                ui::drawButton(UpdateButton, "Na fila", ui::Style::Disabled, 0.5f);
                ui::drawButton(UninstallButton, "Desinstalar", ui::Style::Disabled, 0.5f);
            } else {
                ui::drawButton(UpdateButton, "Atualizar",
                               updatable ? ui::Style::Normal : ui::Style::Disabled, 0.5f,
                               ui::Icon::Refresh, ui::color::IconGreen,
                               updatable && held(input, UpdateButton));
                ui::drawButton(UninstallButton, "Desinstalar", ui::Style::Danger, 0.5f,
                               ui::Icon::Trash, 0, held(input, UninstallButton));
            }
        } else {
            ui::drawWrapped("Instale traduções pelo Catálogo para elas aparecerem aqui.",
                            160, 80, ui::type::Small, ui::color::InkSoft, 280, 3,
                            C2D_AlignCenter);
        }
        ui::drawTab(ui::Tab::Left, "Voltar", "B", held(input, backTab));
        ui::endFrame();
    }
}

} // namespace screens
