#include "queue_screen.hpp"

#include "image_cache.hpp"
#include "jobs.hpp"
#include "sound.hpp"
#include "ui.hpp"
#include "units.hpp"

#include <algorithm>
#include <cmath>

namespace screens {
namespace {

constexpr float RowX = 4;
constexpr float RowWidth = 388;
constexpr float RowHeight = 55;
constexpr float RowPitch = 59;
constexpr int VisibleRows = 4;

const u32 CardFill = ui::color::rgb(0xFFFDF2);
const u32 CardEdge = ui::color::rgb(0xCFC795);

const ui::Rect WideButton = {8, 168, 304, 34};
const ui::Rect LeftButton = {8, 168, 148, 34};
const ui::Rect RightButton = {164, 168, 148, 34};

struct Badge {
    const char *label;
    u32 color;
};

Badge badgeFor(jobs::State state)
{
    switch (state) {
    case jobs::State::Queued:
        return {"Na fila", ui::color::Neutral};
    case jobs::State::Running:
        return {"Em andamento", ui::color::IconBlue};
    case jobs::State::Done:
        return {"Concluído", ui::color::Good};
    case jobs::State::Failed:
        return {"Falhou", ui::color::Caution};
    case jobs::State::Cancelled:
        break;
    }
    return {"Cancelado", ui::color::Neutral};
}

std::string stepLine(const jobs::JobView &job)
{
    const int total = static_cast<int>(job.steps.size());
    if (job.state == jobs::State::Done) {
        return jobs::kindName(job.kind) + std::string(" concluída");
    }
    if (job.state == jobs::State::Queued) {
        return "Aguardando · " + std::to_string(total) + " etapas";
    }
    int index = job.current;
    if (index < 0) {
        // Falhou/cancelou: mostra a etapa onde parou.
        for (int i = 0; i < total; ++i) {
            if (job.steps[i].state == jobs::StepState::Failed ||
                job.steps[i].state == jobs::StepState::Cancelled) {
                index = i;
            }
        }
    }
    if (index < 0 || index >= total) {
        return std::to_string(total) + " etapas";
    }
    return "Etapa " + std::to_string(index + 1) + " de " + std::to_string(total) +
           " · " + job.steps[index].label;
}

void drawIcon(const jobs::JobView &job, float x, float y)
{
    const C2D_Image *image =
        job.iconUrl.empty() ? nullptr : imagecache::icon(job.icon, job.iconUrl);
    if (image != nullptr) {
        ui::drawImage(*image, x, y);
    } else {
        ui::fillRound({x, y, 48, 48}, 4, ui::color::Recess);
        ui::drawIcon(ui::Icon::Package, x + 12, y + 12, 24, ui::color::Neutral);
    }
}

void drawRow(const jobs::JobView &job, const ui::Rect &rect, bool selected)
{
    ui::fillRound(rect, 6, CardEdge);
    ui::fillRound(rect.inset(1), 5, CardFill);
    drawIcon(job, rect.x + 4, rect.y + 4);
    const Badge badge = badgeFor(job.state);
    const float badgeWidth = ui::drawBadge(rect.x + rect.w - 6, rect.y + 5,
                                           badge.label, badge.color);
    const float textX = rect.x + 58;
    ui::drawMarquee(job.gameName, textX, rect.y + 4, 0.48f,
                    ui::color::InkStrong, rect.w - 58 - badgeWidth - 14);
    ui::drawMarquee(job.translationName, textX, rect.y + 21, 0.36f,
                    ui::color::InkSoft, rect.w - 58 - 8);
    ui::drawMarquee(stepLine(job), textX, rect.y + 34, 0.38f,
                    job.state == jobs::State::Failed ? ui::color::Caution
                                                     : ui::color::Ink,
                    rect.w - 58 - 8);
    if (job.state == jobs::State::Running) {
        const ui::Rect bar = {textX, rect.y + rect.h - 6, rect.w - 58 - 8, 3};
        ui::fillRound(bar, 1, ui::color::Recess);
        ui::fillRound({bar.x, bar.y, bar.w * job.overall(), bar.h}, 1,
                      ui::color::Accent);
    }
    if (selected) {
        ui::drawFocus(rect, 6);
    }
}

std::string stepAmount(const jobs::StepView &step)
{
    if (step.total == 0) {
        return "";
    }
    std::string text = units::bytes(step.done) + " de " + units::bytes(step.total);
    if (step.download && step.rate > 0) {
        text += " · " + units::rate(step.rate);
    }
    if (step.download && step.secondsLeft >= 0) {
        text += " · " + units::timeLeft(static_cast<uint64_t>(step.secondsLeft));
    }
    return text;
}

void drawDetails(const jobs::JobView &job)
{
    float y = 6;
    ui::drawMarquee(job.translationName, 8, y, 0.5f, ui::color::InkStrong, 304);
    y += 20;
    const int total = static_cast<int>(job.steps.size());
    int current = job.current;
    if (current < 0) {
        for (int i = 0; i < total; ++i) {
            if (job.steps[i].state == jobs::StepState::Failed ||
                job.steps[i].state == jobs::StepState::Cancelled) {
                current = i;
            }
        }
    }
    const std::string where =
        job.state == jobs::State::Done
            ? std::to_string(total) + " de " + std::to_string(total)
        : current >= 0 ? std::to_string(current + 1) + " de " + std::to_string(total)
                       : "— de " + std::to_string(total);
    ui::drawTagRow(8, y, 80, "Operação", jobs::kindName(job.kind));
    y += 22;
    ui::drawTagRow(8, y, 80, "Etapas", where);
    y += 26;

    // Barra geral: texto 18 px acima da barra.
    const float overall = job.overall();
    constexpr float FirstBar = 92;
    ui::drawText("Geral", 8, FirstBar - 18, 0.38f, ui::color::InkSoft);
    ui::drawText(std::to_string(static_cast<int>(std::round(overall * 100))) + "%",
                 312, FirstBar - 18, 0.38f, ui::color::InkSoft, C2D_AlignRight);
    ui::drawProgressBar({8, FirstBar, 304, 10}, overall);

    // Barra da etapa atual, com duas linhas de texto acima: o nome da etapa
    // e, abaixo dele, os números (ou o erro, se falhou).
    constexpr float SecondBar = 148;
    constexpr float LineOne = SecondBar - 32;
    constexpr float LineTwo = SecondBar - 18;
    if (job.state == jobs::State::Failed && !job.error.empty()) {
        ui::drawMarquee(job.error, 8, LineOne, 0.4f, ui::color::Caution, 304);
        ui::drawMarquee(job.errorDetail, 8, LineTwo, 0.34f, ui::color::InkSoft, 304);
    } else if (current >= 0 && current < total) {
        const jobs::StepView &step = job.steps[current];
        ui::drawMarquee(step.label, 8, LineOne, 0.4f, ui::color::Ink, 304);
        ui::drawMarquee(stepAmount(step), 8, LineTwo, 0.36f, ui::color::InkSoft, 304);
    }
    if (current >= 0 && current < total) {
        const jobs::StepView &step = job.steps[current];
        const float fraction =
            step.state == jobs::StepState::Done ? 1.0f
            : step.total > 0 ? static_cast<float>(step.done) / step.total
            : (step.state == jobs::StepState::Running ? -1.0f : 0.0f);
        ui::drawProgressBar({8, SecondBar, 304, 10}, fraction);
    } else {
        ui::drawProgressBar({8, SecondBar, 304, 10}, 0.0f);
    }
}

bool released(const ui::Input &input, const ui::Rect &rect)
{
    return (input.up & KEY_TOUCH) && rect.contains(input.touch);
}

bool held(const ui::Input &input, const ui::Rect &rect)
{
    return (input.held & KEY_TOUCH) && rect.contains(input.touch);
}

} // namespace

void runQueueScreen()
{
    int selectedId = -1;
    int selection = 0;
    int topRow = 0;
    float scroll = 0;
    ui::enterScreen("queue");

    while (ui::keepRunning()) {
        imagecache::update();
        const ui::Input input = ui::readInput();
        const std::vector<jobs::JobView> list = jobs::snapshot();
        const int count = static_cast<int>(list.size());

        // Mantém a seleção no mesmo processo enquanto a lista muda.
        for (int i = 0; i < count; ++i) {
            if (list[i].id == selectedId) {
                selection = i;
            }
        }
        if (input.repeat & KEY_DUP) {
            selection = std::max(0, selection - 1);
            sound::play(sound::Effect::Move);
        }
        if (input.repeat & KEY_DDOWN) {
            selection = std::min(std::max(0, count - 1), selection + 1);
            sound::play(sound::Effect::Move);
        }
        selection = std::max(0, std::min(selection, count - 1));
        selectedId = count > 0 ? list[selection].id : -1;
        if (selection < topRow) {
            topRow = selection;
        }
        if (selection > topRow + VisibleRows - 1) {
            topRow = selection - (VisibleRows - 1);
        }

        const ui::Rect backTab = ui::tabRect(ui::Tab::Left);
        const ui::Rect clearTab = ui::tabRect(ui::Tab::Right);
        if ((input.down & KEY_B) || released(input, backTab)) {
            sound::play(sound::Effect::Back);
            return;
        }
        if ((input.down & KEY_X) || released(input, clearTab)) {
            jobs::clearFinished();
            sound::play(sound::Effect::Select);
        }

        // Ações do processo selecionado.
        if (count > 0) {
            const jobs::JobView &job = list[selection];
            const bool a = (input.down & KEY_A) != 0;
            switch (job.state) {
            case jobs::State::Running:
                if (job.cancellable() && (a || released(input, WideButton))) {
                    ui::Dialog dialog;
                    dialog.header = "Fila";
                    dialog.title = "Cancelar o download de " + job.gameName + "?";
                    dialog.buttons = {"Cancelar Download", "Continuar"};
                    dialog.dangerous = 0;
                    dialog.cancelButton = 1;
                    if (ui::choose(dialog) == 0) {
                        jobs::cancel(job.id);
                    }
                    ui::enterScreen("queue");
                    continue;
                }
                break;
            case jobs::State::Failed:
                if (a || released(input, LeftButton)) {
                    jobs::retry(job.id);
                    sound::play(sound::Effect::Select);
                } else if ((input.down & KEY_Y) || released(input, RightButton)) {
                    jobs::remove(job.id);
                    sound::play(sound::Effect::Select);
                }
                break;
            case jobs::State::Queued:
            case jobs::State::Done:
            case jobs::State::Cancelled:
                if (a || released(input, WideButton)) {
                    jobs::remove(job.id);
                    sound::play(sound::Effect::Select);
                }
                break;
            }
        }

        // --- Desenho ---
        ui::beginFrame();
        if (count == 0) {
            ui::drawCentered("Nenhum processo na fila.", {20, 0, 360, 240},
                             ui::type::Body, ui::color::InkSoft);
        } else {
            const float target = topRow * RowPitch;
            scroll += (target - scroll) * 0.35f;
            if (std::fabs(target - scroll) < 0.5f) {
                scroll = target;
            }
            for (int i = 0; i < count; ++i) {
                const float y = 4 + i * RowPitch - scroll;
                if (y + RowHeight < 0 || y > 240) {
                    continue;
                }
                drawRow(list[i], {RowX, y, RowWidth, RowHeight}, i == selection);
            }
            if (count > VisibleRows) {
                const float track = 232;
                const float thumb = std::max(20.0f, track * VisibleRows / count);
                const float maxScroll = (count - VisibleRows) * RowPitch;
                const float ty = 4 + (track - thumb) * std::min(1.0f, scroll / maxScroll);
                ui::fillRound({396, ty, 2.5f, thumb}, 1, ui::color::Dot);
            }
        }

        ui::beginBottom();
        if (count == 0) {
            ui::drawWrapped("Instale uma tradução pelo Catálogo para ela aparecer aqui.",
                            160, 80, ui::type::Small, ui::color::InkSoft, 280, 3,
                            C2D_AlignCenter);
        } else {
            const jobs::JobView &job = list[selection];
            drawDetails(job);
            switch (job.state) {
            case jobs::State::Running:
                ui::drawButton(WideButton,
                               job.cancellable() ? "Cancelar"
                                                 : "Não é possível cancelar nesta etapa",
                               job.cancellable() ? ui::Style::Normal
                                                 : ui::Style::Disabled,
                               job.cancellable() ? 0.5f : 0.4f, ui::Icon::None, 0,
                               held(input, WideButton));
                break;
            case jobs::State::Failed:
                ui::drawButton(LeftButton, "Repetir", ui::Style::Normal, 0.5f,
                               ui::Icon::Refresh, ui::color::IconGreen,
                               held(input, LeftButton));
                ui::drawButton(RightButton, "Remover", ui::Style::Normal, 0.5f,
                               ui::Icon::Trash, ui::color::IconGrey,
                               held(input, RightButton));
                break;
            case jobs::State::Queued:
                ui::drawButton(WideButton, "Remover da Fila", ui::Style::Normal, 0.5f,
                               ui::Icon::Trash, ui::color::IconGrey,
                               held(input, WideButton));
                break;
            case jobs::State::Done:
            case jobs::State::Cancelled:
                ui::drawButton(WideButton, "Remover", ui::Style::Normal, 0.5f,
                               ui::Icon::Trash, ui::color::IconGrey,
                               held(input, WideButton));
                break;
            }
        }
        ui::drawTab(ui::Tab::Left, "Voltar", "B", held(input, backTab));
        ui::drawTab(ui::Tab::Right, "Limpar", "X", held(input, clearTab));
        ui::endFrame();
    }
}

} // namespace screens
