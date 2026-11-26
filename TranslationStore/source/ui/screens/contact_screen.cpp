#include "contact_screen.hpp"

#include "api.hpp"
#include "platform.hpp"
#include "sound.hpp"
#include "ui.hpp"

#include <3ds.h>

#include <algorithm>
#include <atomic>
#include <string>

namespace screens {
namespace {

struct Kind {
    const char *id;    // enviado ao servidor
    const char *label; // botão
    const char *title; // tela de cima
    const char *gameHint;
};

const Kind Kinds[] = {
    {"erro", "Erro", "Erro numa tradução", "Em qual jogo está o erro?"},
    {"pedido", "Pedido", "Pedido de tradução", "Qual jogo você quer traduzido?"},
    {"outro", "Outro", "Outro assunto", "Jogo (opcional)"},
};
constexpr int KindCount = 3;

// Rascunho: continua lá ao sair e voltar, até ser enviado.
struct Draft {
    int kind = 0;
    std::string game;
    std::string message;
    std::string contact;
};
Draft draft;

// Linhas da tela de baixo, de cima para baixo (o D-pad anda entre elas).
enum Row { RowKind, RowGame, RowMessage, RowContact, RowSend, RowCount };

ui::Rect kindRect(int index)
{
    return {8 + index * 104.0f, 8, 96, 30};
}
const ui::Rect GameRect = {8, 46, 304, 34};
const ui::Rect MessageRect = {8, 86, 304, 34};
const ui::Rect ContactRect = {8, 126, 304, 34};
const ui::Rect SendRect = {8, 166, 304, 36};

ui::Rect rowRect(int row, int kind)
{
    switch (row) {
    case RowKind:
        return kindRect(kind);
    case RowGame:
        return GameRect;
    case RowMessage:
        return MessageRect;
    case RowContact:
        return ContactRect;
    default:
        return SendRect;
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

// Primeira linha do texto, para caber no botão.
std::string firstLine(const std::string &text)
{
    const size_t end = text.find('\n');
    return end == std::string::npos ? text : text.substr(0, end) + " ...";
}

// Botão de campo: nome à esquerda, valor (ou dica em cinza) à direita.
void drawField(const ui::Rect &rect, const std::string &name, const std::string &value,
               const std::string &placeholder, bool pressed)
{
    ui::drawButton(rect, "", ui::Style::Normal, ui::type::Button, ui::Icon::None, 0,
                   pressed);
    const float lift = pressed ? 1 : 0;
    ui::drawText(name, rect.x + 10, rect.y + 10 + lift, 0.46f, ui::color::InkStrong);
    const ui::Rect valueArea = {rect.x + 86, rect.y + 9 + lift, rect.w - 96, 16};
    if (value.empty()) {
        ui::drawCenteredMarquee(placeholder, valueArea, 0.42f, ui::color::InkSoft, true);
    } else {
        ui::drawCenteredMarquee(firstLine(value), valueArea, 0.44f, ui::color::Ink, true);
    }
}

void drawTop()
{
    const Kind &kind = Kinds[draft.kind];
    ui::drawTopChrome("Contato", ui::Icon::Chat, ui::color::rgb(0x2A86D1));
    ui::drawPanel();
    const ui::Rect panel = ui::TopPanel;
    float y = panel.y + 8;
    y = ui::drawTagRow(panel.x + 10, y, 80, "Assunto", kind.title);
    y = ui::drawTagRow(panel.x + 10, y, 80, "Jogo", draft.game.empty() ? "—" : draft.game);
    y = ui::drawTagRow(panel.x + 10, y, 80, "Contato",
                       draft.contact.empty() ? "—" : draft.contact);
    y += 4;
    if (draft.message.empty()) {
        ui::drawText("Escreva sua mensagem na tela de baixo.", panel.x + 12, y, 0.44f,
                     ui::color::InkSoft);
    } else {
        ui::drawWrapped(draft.message, panel.x + 12, y, 0.44f, ui::color::Ink,
                        panel.w - 24, 5);
    }
}

void drawBottom(const ui::Input &input, int row, bool showFocus)
{
    for (int index = 0; index < KindCount; ++index) {
        const ui::Rect rect = kindRect(index);
        const bool current = draft.kind == index;
        ui::drawButton(rect, Kinds[index].label,
                       current ? ui::Style::Chosen : ui::Style::Normal, 0.5f,
                       ui::Icon::None, 0, held(input, rect));
    }
    drawField(GameRect, "Jogo", draft.game, Kinds[draft.kind].gameHint,
              held(input, GameRect));
    drawField(MessageRect, "Mensagem", draft.message, "Toque para escrever",
              held(input, MessageRect));
    drawField(ContactRect, "Contato", draft.contact, "Discord ou e-mail (opcional)",
              held(input, ContactRect));
    ui::drawButton(SendRect, "Enviar", ui::Style::Normal, 0.52f, ui::Icon::Chat,
                   ui::color::rgb(0x2A86D1), held(input, SendRect));
    if (showFocus) {
        ui::drawFocus(rowRect(row, draft.kind), 8);
    }
    ui::drawTab(ui::Tab::Left, "Voltar", "B", held(input, ui::tabRect(ui::Tab::Left)));
}

void editGame()
{
    std::string value;
    if (platform::keyboard(platform::KeyboardKind::Line, Kinds[draft.kind].gameHint,
                           draft.game, value)) {
        draft.game = value;
    }
}

void editMessage()
{
    std::string value;
    if (platform::keyboard(platform::KeyboardKind::Text,
                           "Descreva o erro ou o pedido", draft.message, value)) {
        draft.message = value;
    }
}

void editContact()
{
    std::string value;
    if (platform::keyboard(platform::KeyboardKind::Line, "Discord ou e-mail (opcional)",
                           draft.contact, value)) {
        draft.contact = value;
    }
}

// Envio numa thread, com a tela de "Enviando..." enquanto isso.
struct Sender {
    Thread thread = nullptr;
    std::atomic<bool> done {false};
    api::ContactMessage message;
    api::SendResult result;

    static void entry(void *self)
    {
        Sender *sender = static_cast<Sender *>(self);
        sender->result = api::sendContact(sender->message);
        sender->done.store(true);
    }
};

api::SendResult send()
{
    Sender sender;
    sender.message.kind = Kinds[draft.kind].id;
    sender.message.message = draft.message;
    sender.message.contact = draft.contact;
    sender.message.gameName = draft.game;
    s32 priority = 0x30;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    sender.thread = threadCreate(Sender::entry, &sender, 64 * 1024,
                                 std::min(0x3f, static_cast<int>(priority) + 1), -2, false);
    if (sender.thread == nullptr) {
        api::SendResult failed;
        failed.message = "Não foi possível iniciar o envio.";
        return failed;
    }
    while (!sender.done.load() && ui::keepRunning()) {
        ui::readInput();
        ui::beginFrame();
        drawTop();
        ui::beginBottom();
        ui::drawSpinner(160, 96);
        ui::drawCentered("Enviando...", {0, 120, 320, 24}, 0.52f, ui::color::InkSoft);
        ui::endFrame();
    }
    threadJoin(sender.thread, U64_MAX);
    threadFree(sender.thread);
    return sender.result;
}

// true quando a mensagem foi enviada (a tela fecha).
bool trySend()
{
    std::string trimmed = draft.message;
    trimmed.erase(0, trimmed.find_first_not_of(" \n\t"));
    if (trimmed.size() < 3) {
        sound::play(sound::Effect::Error);
        ui::message("Contato", "Escreva a mensagem antes de enviar.", ui::Tone::Warning);
        return false;
    }
    sound::play(sound::Effect::Select);
    const api::SendResult result = send();
    if (!result.ok) {
        sound::play(sound::Effect::Error);
        ui::message("Não foi possível enviar",
                    result.message + " Sua mensagem continua guardada.", ui::Tone::Error);
        return false;
    }
    ui::message("Mensagem enviada", "Obrigado! Vamos ler sua mensagem.", ui::Tone::Success);
    draft = Draft {};
    return true;
}

} // namespace

void runContact()
{
    int row = RowKind;
    bool showFocus = true;
    ui::enterScreen("contact");

    while (ui::keepRunning()) {
        const ui::Input input = ui::readInput();

        if ((input.down & KEY_B) || released(input, ui::tabRect(ui::Tab::Left))) {
            sound::play(sound::Effect::Back);
            return;
        }

        // D-pad: cima/baixo entre as linhas; esquerda/direita troca o assunto.
        if (input.repeat & (KEY_DUP | KEY_DDOWN | KEY_DLEFT | KEY_DRIGHT)) {
            showFocus = true;
            if ((input.repeat & KEY_DUP) && row > 0) {
                --row;
                sound::play(sound::Effect::Move);
            }
            if ((input.repeat & KEY_DDOWN) && row + 1 < RowCount) {
                ++row;
                sound::play(sound::Effect::Move);
            }
            if (row == RowKind && (input.repeat & KEY_DLEFT) && draft.kind > 0) {
                --draft.kind;
                sound::play(sound::Effect::Move);
            }
            if (row == RowKind && (input.repeat & KEY_DRIGHT) && draft.kind + 1 < KindCount) {
                ++draft.kind;
                sound::play(sound::Effect::Move);
            }
        }

        // O que foi ativado: pela tecla A na linha com foco ou por toque.
        int activated = -1;
        if (input.down & KEY_A) {
            activated = row;
        }
        if (input.up & KEY_TOUCH) {
            for (int index = 0; index < KindCount; ++index) {
                if (released(input, kindRect(index))) {
                    draft.kind = index;
                    row = RowKind;
                    sound::play(sound::Effect::Select);
                }
            }
            for (int candidate = RowGame; candidate < RowCount; ++candidate) {
                if (released(input, rowRect(candidate, draft.kind))) {
                    row = candidate;
                    activated = candidate;
                }
            }
            showFocus = false;
        }
        if (input.down & KEY_START) {
            activated = RowSend;
        }

        switch (activated) {
        case RowGame:
            sound::play(sound::Effect::Select);
            editGame();
            ui::enterScreen("contact");
            continue;
        case RowMessage:
            sound::play(sound::Effect::Select);
            editMessage();
            ui::enterScreen("contact");
            continue;
        case RowContact:
            sound::play(sound::Effect::Select);
            editContact();
            ui::enterScreen("contact");
            continue;
        case RowSend:
            if (trySend()) {
                return;
            }
            ui::enterScreen("contact");
            continue;
        default:
            break;
        }

        ui::beginFrame();
        drawTop();
        ui::beginBottom();
        drawBottom(input, row, showFocus);
        ui::endFrame();
    }
}

} // namespace screens
