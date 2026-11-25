#include "app_update_screen.hpp"

#include "sound.hpp"
#include "ui.hpp"
#include "units.hpp"

#include <string>
#include <vector>

namespace screens {
namespace {

using appupdate::Phase;
using appupdate::Progress;

const ui::Rect TopBox = {17, 146, 366, 80};
const ui::Rect CancelButton = {90, 150, 140, 38};

std::string amount(const Progress &progress)
{
    if (progress.total == 0) {
        return "";
    }
    std::string text =
        units::bytes(progress.done) + " de " + units::bytes(progress.total);
    if (progress.phase == Phase::Downloading && progress.rate > 0) {
        text += " · " + units::rate(progress.rate);
    }
    if (progress.phase == Phase::Downloading && progress.secondsLeft >= 0) {
        text += " · " + units::timeLeft(static_cast<uint64_t>(progress.secondsLeft));
    }
    return text;
}

void drawTop(const appupdate::Release &release, const Progress &progress)
{
    ui::drawIcon(ui::Icon::AppMark, 168, 20, 64, ui::color::Accent);
    ui::drawText("Versão Brasileira", 200, 94, ui::type::Hero, ui::color::Ink,
                 C2D_AlignCenter);
    ui::drawDots(134);

    ui::drawPanel(TopBox);
    const std::string headline =
        (progress.phase == Phase::Installing ? "Instalando a versão "
                                             : "Baixando a versão ") +
        release.version;
    ui::drawCenteredMarquee(headline, {TopBox.x + 10, TopBox.y + 6, TopBox.w - 20, 28},
                            0.52f, ui::color::Ink);
    const float fraction =
        progress.total > 0 ? static_cast<float>(progress.done) / progress.total : -1;
    ui::drawProgressBar({TopBox.x + 30, TopBox.y + 40, TopBox.w - 60, 12}, fraction);
    ui::drawCenteredMarquee(amount(progress),
                            {TopBox.x + 10, TopBox.y + 56, TopBox.w - 20, 18},
                            ui::type::Tiny, ui::color::InkSoft);
}

void drawBottom(const appupdate::Release &release, const Progress &progress,
                const ui::Input &input)
{
    ui::drawFacts({{"Versão atual", APP_VERSION},
                   {"Nova versão", release.version},
                   {"Formato", appupdate::formatName(appupdate::runningFormat())}},
                  {10, 24, 300, 110}, 120);
    if (progress.phase != Phase::Downloading) {
        return;
    }
    const bool pressed =
        (input.held & KEY_TOUCH) && CancelButton.contains(input.touch);
    ui::drawButton(CancelButton, "Cancelar", ui::Style::Normal,
                   ui::type::Small + 0.04f, ui::Icon::None, 0, pressed);
    ui::drawFocus(CancelButton, 9);
}

// Baixa e instala, com a tela de progresso. Devolve o estado final.
Progress work(const appupdate::Release &release)
{
    appupdate::Updater updater;
    updater.start(release);
    ui::enterScreen("app_update");
    while (ui::keepRunning()) {
        const ui::Input input = ui::readInput();
        const Progress progress = updater.progress();
        if (progress.phase == Phase::Ready || progress.phase == Phase::Failed ||
            progress.phase == Phase::Cancelled) {
            updater.join();
            return progress;
        }
        if (progress.phase == Phase::Downloading &&
            ((input.down & (KEY_A | KEY_B)) ||
             ((input.up & KEY_TOUCH) && CancelButton.contains(input.touch)))) {
            sound::play(sound::Effect::Back);
            updater.cancel();
        }
        ui::beginFrame();
        drawTop(release, progress);
        ui::beginBottom();
        drawBottom(release, progress, input);
        ui::endFrame();
    }
    updater.cancel();
    updater.join();
    Progress closing;
    closing.phase = Phase::Cancelled;
    return closing;
}

} // namespace

AppUpdateResult runAppUpdate(const appupdate::Release &release)
{
    const char *skipLabel = release.required ? "Fechar" : "Abrir sem atualizar";

    ui::Dialog ask;
    ask.header = "Atualização do aplicativo";
    ask.title = "Nova versão disponível: " + release.version;
    ask.body.push_back({"Versão instalada: " APP_VERSION});
    for (size_t index = 0; index < release.notes.size() && index < 4; ++index) {
        ask.body.push_back({"• " + release.notes[index], ui::color::Text, 0.5f, 2});
    }
    if (release.required) {
        ask.body.push_back({"Esta atualização é obrigatória: o aplicativo só "
                            "abre depois dela.",
                            ui::color::Caution, 0.5f, 2});
    }
    ask.buttons = {"Atualizar", skipLabel};
    ask.cancelButton = 1;

    bool askFirst = true;
    while (ui::keepRunning()) {
        if (askFirst && ui::choose(ask) != 0) {
            return release.required ? AppUpdateResult::Quit : AppUpdateResult::Open;
        }
        const Progress result = work(release);
        if (!ui::keepRunning()) {
            break;
        }
        if (result.phase == Phase::Ready) {
            sound::play(sound::Effect::Select);
            if (appupdate::runningFormat() == appupdate::Format::Cia) {
                appupdate::relaunchOnExit();
                ui::message("Atualização instalada",
                            "O aplicativo vai fechar e abrir de novo na "
                            "versão " + release.version + ".",
                            ui::Tone::Info);
            } else {
                ui::message("Atualização baixada",
                            "O aplicativo vai fechar para concluir. Abra-o de "
                            "novo pelo Homebrew Launcher.",
                            ui::Tone::Info);
            }
            return AppUpdateResult::Quit;
        }
        if (result.phase == Phase::Cancelled) {
            askFirst = true; // volta à pergunta
            continue;
        }
        ui::Dialog failed;
        failed.header = "Atualização do aplicativo";
        failed.title = result.message.empty() ? "A atualização falhou."
                                              : result.message;
        failed.tone = ui::Tone::Error;
        failed.body.push_back({"Nada foi alterado: a versão atual continua "
                               "instalada."});
        if (!result.detail.empty()) {
            failed.body.push_back({result.detail, ui::color::InkSoft, 0.44f, 3});
        }
        failed.buttons = {"Repetir", skipLabel};
        failed.cancelButton = 1;
        sound::play(sound::Effect::Error);
        if (ui::choose(failed) != 0) {
            return release.required ? AppUpdateResult::Quit : AppUpdateResult::Open;
        }
        askFirst = false; // Repetir: volta direto ao download
    }
    return AppUpdateResult::Quit;
}

} // namespace screens
