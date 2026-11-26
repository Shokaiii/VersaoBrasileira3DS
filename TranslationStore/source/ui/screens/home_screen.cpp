#include "home_screen.hpp"

#include "image.hpp"
#include "stb_image.h"
#include "ui.hpp"

#include <citro2d.h>

namespace screens {
namespace {

// Logo da Tela inicial (romfs:/ui/home_logo.png), colorido e com
// transparência, carregado uma vez numa textura RGBA8.
struct Logo {
    bool tried = false;
    bool ready = false;
    C3D_Tex texture {};
    Tex3DS_SubTexture sub {};
    C2D_Image image {};
    int width = 0;
    int height = 0;
};
Logo logo;

unsigned nextPowerOfTwo(unsigned value)
{
    unsigned out = 8;
    while (out < value) {
        out <<= 1;
    }
    return out;
}

bool loadLogo()
{
    if (logo.tried) {
        return logo.ready;
    }
    logo.tried = true;
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc *pixels =
        stbi_load("romfs:/ui/home_logo.png", &width, &height, &channels, 4);
    if (pixels == nullptr) {
        return false;
    }
    const unsigned texWidth = nextPowerOfTwo(static_cast<unsigned>(width));
    const unsigned texHeight = nextPowerOfTwo(static_cast<unsigned>(height));
    if (texWidth <= 1024 && texHeight <= 1024 &&
        C3D_TexInit(&logo.texture, texWidth, texHeight, GPU_RGBA8)) {
        u32 *texels = static_cast<u32 *>(logo.texture.data);
        for (unsigned i = 0; i < texWidth * texHeight; ++i) {
            texels[i] = 0;
        }
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const stbi_uc *p = pixels + (y * width + x) * 4;
                texels[image::tiledIndex(x, y, texWidth)] =
                    (static_cast<u32>(p[0]) << 24) |
                    (static_cast<u32>(p[1]) << 16) |
                    (static_cast<u32>(p[2]) << 8) | p[3];
            }
        }
        C3D_TexFlush(&logo.texture);
        C3D_TexSetFilter(&logo.texture, GPU_LINEAR, GPU_LINEAR);
        logo.sub = {static_cast<u16>(width), static_cast<u16>(height), 0.0f,
                    1.0f, static_cast<float>(width) / texWidth,
                    1.0f - static_cast<float>(height) / texHeight};
        logo.image = {&logo.texture, &logo.sub};
        logo.width = width;
        logo.height = height;
        logo.ready = true;
    }
    stbi_image_free(pixels);
    return logo.ready;
}

} // namespace

HomeChoice runHomeScreen(const HomeInfo &info, int &focus)
{
    ui::ScreenSpec spec;
    spec.closeBar = true; // START Fechar
    const auto add = [&](ui::Rect rect, const char *label, ui::Icon icon,
                         u32 iconColor) {
        ui::Control control;
        control.rect = rect;
        control.label = label;
        control.icon = icon;
        control.iconColor = iconColor;
        control.tile = true;
        control.tileScale = 0.47f;
        spec.controls.push_back(control);
    };
    // Dois botões largos no alto (+Baixados e Contato) e, abaixo, 3 colunas
    // × 2 linhas de blocos de 96×76 com 8 px entre eles. Os blocos vêm
    // primeiro na lista para manter os índices (selos e foco) de antes.
    add({8, 42, 96, 76}, "Catálogo", ui::Icon::Catalog, ui::color::IconGreen);
    add({112, 42, 96, 76}, "Busca", ui::Icon::Search, ui::color::IconBlue);
    add({216, 42, 96, 76}, "Favoritos", ui::Icon::Star, ui::color::IconGold);
    add({8, 126, 96, 76}, "Biblioteca", ui::Icon::Library,
        ui::color::IconOrange);
    add({112, 126, 96, 76}, "Fila", ui::Icon::Processes,
        ui::color::IconTeal);
    // Selos: atualizações na Biblioteca; processos (ou falha) na Fila.
    if (info.updates > 0) {
        spec.controls[3].badge = std::to_string(info.updates);
        spec.controls[3].badgeColor = ui::color::IconOrange;
    }
    if (info.failed) {
        spec.controls[4].badge = "!";
        spec.controls[4].badgeColor = ui::color::Caution;
    } else if (info.queued > 0) {
        spec.controls[4].badge = std::to_string(info.queued);
        spec.controls[4].badgeColor = ui::color::IconBlue;
    }
    add({216, 126, 96, 76}, "Ajustes", ui::Icon::Gear, ui::color::IconGrey);
    const auto wide = [&](ui::Rect rect, const char *label, ui::Icon icon,
                          u32 iconColor) {
        ui::Control control;
        control.rect = rect;
        control.label = label;
        control.icon = icon;
        control.iconColor = iconColor;
        control.scale = 0.5f;
        spec.controls.push_back(control);
    };
    wide({8, 6, 148, 30}, "+Baixados", ui::Icon::Trophy, ui::color::IconGold);
    wide({164, 6, 148, 30}, "Contato", ui::Icon::Chat, ui::color::rgb(0x2A86D1));

    spec.drawTop = [&]() {
        ui::nameScreen("Inicio");
        ui::drawTopBackground();
        ui::drawStatusBar();
        ui::drawDots(41);
        const ui::Rect panel = {17, 53, 366, 172};
        ui::drawPanel(panel);
        // Logo, nome, versões dos catálogos e versão do app, de cima para
        // baixo, com espaço entre cada um.
        if (loadLogo()) {
            ui::drawImage(logo.image, 200 - logo.width / 2.0f, 58);
        } else {
            ui::drawIcon(ui::Icon::AppMark, 174, 58, 52, ui::color::Accent);
        }
        ui::drawText("Versão Brasileira", 200, 115, 0.85f, ui::color::Ink,
                     C2D_AlignCenter);
        ui::drawFacts(
            {{"Catálogo local",
              info.localVersion.empty() ? "Nenhum" : info.localVersion},
             {"Catálogo online",
              info.remoteVersion.empty() ? "—" : info.remoteVersion}},
            {17, 168, 366, 46}, 120);
        ui::drawText("v" APP_VERSION, panel.x + panel.w - 8,
                     panel.y + panel.h - 22, ui::type::Tiny,
                     ui::color::InkSoft, C2D_AlignRight);
    };

    const ui::ScreenResult result = ui::runScreen(spec, focus);
    switch (result.choice) {
    case 0:
        return HomeChoice::Catalog;
    case 1:
        return HomeChoice::Search;
    case 2:
        return HomeChoice::Favorites;
    case 3:
        return HomeChoice::Library;
    case 4:
        return HomeChoice::Processes;
    case 5:
        return HomeChoice::Settings;
    case 6:
        return HomeChoice::TopDownloads;
    case 7:
        return HomeChoice::Contact;
    default:
        break;
    }
    return HomeChoice::Close;
}

} // namespace screens
