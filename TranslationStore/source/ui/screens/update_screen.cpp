#include "update_screen.hpp"

#include "catalog_updater.hpp"
#include "sound.hpp"
#include "ui.hpp"

#include <string>

namespace screens {
namespace {

using repository::UpdateState;
using repository::UpdateStatus;

// Depois de concluir sem erro, a tela fica visível por este tempo antes de
// abrir a Tela inicial.
constexpr uint64_t SuccessDelayMs = 1000;

const ui::Rect TopBox = {17, 146, 366, 80};
const ui::Rect Buttons[2] = {{16, 150, 140, 38}, {164, 150, 140, 38}};

std::string headline(const UpdateStatus &status)
{
    switch (status.state) {
    case UpdateState::Checking:
        return "Verificando atualizações";
    case UpdateState::Downloading:
        return "Baixando novo catálogo";
    case UpdateState::UpToDate:
        return "O catálogo já está atualizado";
    case UpdateState::Updated:
        return "Catálogo atualizado (v" + status.localVersion + ")";
    case UpdateState::Offline:
        return status.hasLocal ? "Sem conexão: usando catálogo local"
                               : "Sem conexão com a internet";
    case UpdateState::AppUpdate:
        return "Nova versão do aplicativo";
    case UpdateState::Failed:
        break;
    }
    return "Falha ao verificar o catálogo";
}

bool succeeded(const UpdateStatus &status)
{
    return status.finished && (status.state == UpdateState::UpToDate ||
                               status.state == UpdateState::Updated);
}

void drawTop(const UpdateStatus &status)
{
    ui::drawIcon(ui::Icon::AppMark, 168, 20, 64, ui::color::Accent);
    ui::drawText("Versão Brasileira", 200, 94, ui::type::Hero, ui::color::Ink,
                 C2D_AlignCenter);
    ui::drawDots(134);

    ui::drawPanel(TopBox);
    const bool failed = status.finished && !succeeded(status);
    ui::drawCenteredMarquee(headline(status),
                            {TopBox.x + 10, TopBox.y + 6, TopBox.w - 20, 28},
                            0.52f, failed ? ui::color::Caution : ui::color::Ink);

    const float middle = TopBox.y + 52;
    if (!status.finished && status.state == UpdateState::Checking) {
        ui::drawSpinner(200, middle, 12);
    } else if (!status.finished && status.state == UpdateState::Downloading) {
        // O servidor nem sempre informa o tamanho: barra em movimento.
        ui::drawProgressBar({TopBox.x + 30, middle - 6, TopBox.w - 60, 12},
                            -1);
    } else if (failed && !status.detail.empty()) {
        ui::drawWrapped(status.detail, 200, TopBox.y + 36, ui::type::Tiny,
                        ui::color::InkSoft, TopBox.w - 24, 3, C2D_AlignCenter);
    } else if (succeeded(status)) {
        ui::drawIcon(ui::Icon::Check, 188, middle - 12, 24, ui::color::Good);
    }
}

void drawBottom(const UpdateStatus &status, bool showButtons, int focus,
                const ui::Input &input)
{
    std::string remote = status.remoteVersion;
    if (remote.empty()) {
        remote = status.finished ? "Indisponível" : "—";
    }
    ui::drawFacts({{"Catálogo local",
                    status.hasLocal ? status.localVersion : "Nenhum"},
                   {"Catálogo online", remote}},
                  {10, 24, 300, 90}, 120);

    if (!showButtons) {
        return;
    }
    const char *labels[2] = {"Repetir",
                             status.hasLocal ? "Continuar" : "Fechar"};
    for (int index = 0; index < 2; ++index) {
        const bool pressed = (input.held & KEY_TOUCH) &&
                             Buttons[index].contains(input.touch);
        ui::drawButton(Buttons[index], labels[index], ui::Style::Normal,
                       ui::type::Small + 0.04f, ui::Icon::None, 0, pressed);
    }
    ui::drawFocus(Buttons[focus], 9);
}

} // namespace

UpdateOutcome runUpdateScreen(const std::string &versionUrl,
                              repository::CheckMode mode)
{
    UpdateOutcome outcome;
    repository::CatalogUpdater updater;
    updater.start(versionUrl, mode);
    ui::enterScreen("update");

    int focus = 0;
    uint64_t succeededAt = 0;
    bool errorSoundPlayed = false;

    while (ui::keepRunning()) {
        const ui::Input input = ui::readInput();
        const UpdateStatus status = updater.status();
        const bool success = succeeded(status);
        const bool showButtons = status.finished && !success;
        outcome.remoteVersion = status.remoteVersion;

        if (status.finished && status.state == UpdateState::AppUpdate) {
            outcome.appUpdate = true;
            outcome.app = status.app;
            return outcome;
        }
        if (status.finished && !mode.catalog) {
            return outcome; // só o aplicativo: sem erros nem espera
        }

        if (success) {
            if (succeededAt == 0) {
                succeededAt = ui::now();
            } else if (ui::now() - succeededAt >= SuccessDelayMs) {
                return outcome;
            }
        }

        int choice = -1;
        if (showButtons) {
            if (!errorSoundPlayed) {
                sound::play(sound::Effect::Error);
                errorSoundPlayed = true;
            }
            if (input.down & (KEY_LEFT | KEY_RIGHT)) {
                focus = 1 - focus;
                sound::play(sound::Effect::Move);
            }
            if (input.down & KEY_A) {
                choice = focus;
            }
            if (input.down & KEY_B) {
                choice = 1;
            }
            // Toque: age ao soltar, como o sistema.
            if (input.up & KEY_TOUCH) {
                for (int index = 0; index < 2; ++index) {
                    if (Buttons[index].contains(input.touch)) {
                        focus = index;
                        choice = index;
                    }
                }
            }
        }

        if (choice == 0) {
            sound::play(sound::Effect::Select);
            updater.start(versionUrl, mode);
            focus = 0;
            errorSoundPlayed = false;
            continue;
        }
        if (choice == 1) {
            sound::play(sound::Effect::Select);
            outcome.quit = !status.hasLocal;
            return outcome;
        }

        ui::beginFrame();
        drawTop(status);
        ui::beginBottom();
        drawBottom(status, showButtons, focus, input);
        ui::endFrame();
    }
    updater.stop();
    outcome.quit = true;
    return outcome;
}

} // namespace screens
