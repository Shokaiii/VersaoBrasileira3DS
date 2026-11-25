#include "settings_screen.hpp"

#include "fs_util.hpp"
#include "jobs.hpp"
#include "operations.hpp"
#include "paths.hpp"
#include "sound.hpp"
#include "ui.hpp"
#include "units.hpp"

#include <3ds.h>

#include <cstring>
#include <vector>

namespace screens {
namespace {

enum Item { Server, CheckOnStart, Connections, Sounds, ClearImages, ClearTemp, About, Count };

uint64_t folderBytes(const std::string &folder)
{
    uint64_t bytes = 0;
    for (const fs::Entry &entry : fs::listEntries(folder, nullptr, true, false)) {
        if (entry.folder) {
            bytes += folderBytes(folder + entry.name + "/");
        } else if (entry.size > 0) {
            bytes += static_cast<uint64_t>(entry.size);
        }
    }
    return bytes;
}

// Apaga o conteúdo de uma pasta, mantendo a própria pasta.
void emptyFolder(const std::string &folder)
{
    for (const fs::Entry &entry : fs::listEntries(folder, nullptr, true, false)) {
        if (entry.folder) {
            ops::removeFolder(folder + entry.name + "/");
        } else {
            fs::removeFile(folder + entry.name);
        }
    }
}

// "https://servidor.com.br/pasta/version.json" -> "servidor.com.br"
std::string hostOf(const std::string &url)
{
    size_t start = url.find("://");
    start = start == std::string::npos ? 0 : start + 3;
    const size_t end = url.find('/', start);
    return url.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

const char *yesNo(bool value)
{
    return value ? "Sim" : "Não";
}

bool confirm(const std::string &title, const std::string &text,
             const std::string &action)
{
    ui::Dialog dialog;
    dialog.header = "Ajustes";
    dialog.title = title;
    if (!text.empty()) {
        dialog.body.push_back({text, ui::color::InkSoft, 0.45f, 3});
    }
    dialog.buttons = {action, "Cancelar"};
    dialog.dangerous = 0;
    dialog.cancelButton = 1;
    return ui::choose(dialog) == 0;
}

// Teclado do sistema para editar a URL. Retorna false se cancelado.
bool editUrl(std::string &url)
{
    SwkbdState keyboard;
    swkbdInit(&keyboard, SWKBD_TYPE_NORMAL, 2, 255);
    swkbdSetHintText(&keyboard, "https://servidor/version.json");
    swkbdSetInitialText(&keyboard, url.c_str());
    swkbdSetValidation(&keyboard, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
    swkbdSetButton(&keyboard, SWKBD_BUTTON_LEFT, "Cancelar", false);
    swkbdSetButton(&keyboard, SWKBD_BUTTON_RIGHT, "OK", true);
    char buffer[256];
    std::memset(buffer, 0, sizeof(buffer));
    const SwkbdButton button = swkbdInputText(&keyboard, buffer, sizeof(buffer));
    if (button != SWKBD_BUTTON_RIGHT) {
        return false;
    }
    url = buffer;
    return true;
}

} // namespace

void runSettings(config::Config &config, const std::string &catalogVersion)
{
    int cursor = 0;
    float offset = -1;
    uint64_t imageBytes = folderBytes(paths::ImageCache);
    uint64_t tempBytes = folderBytes(paths::Temporary);

    while (ui::keepRunning()) {
        ui::ListSpec spec;
        spec.title = "Ajustes";
        const auto add = [&](const std::string &label, const std::string &detail,
                             ui::Icon icon, u32 color) {
            ui::ListItem item;
            item.label = label;
            item.detail = detail;
            item.icon = icon;
            item.iconColor = color;
            spec.items.push_back(item);
        };
        add("Servidor do Catálogo", hostOf(config.versionUrl), ui::Icon::Globe,
            ui::color::IconBlue);
        add("Verificar ao Iniciar", yesNo(config.checkOnStart), ui::Icon::Refresh,
            ui::color::IconGreen);
        add("Conexões Simultâneas", std::to_string(config.connections),
            ui::Icon::Download, ui::color::IconTeal);
        add("Sons", config.sounds ? "Ligado" : "Desligado", ui::Icon::Music,
            ui::color::IconGold);
        add("Limpar Cache de Imagens", units::bytes(imageBytes), ui::Icon::Picture,
            ui::color::IconOrange);
        add("Limpar Arquivos Temporários", units::bytes(tempBytes), ui::Icon::Trash,
            ui::color::IconGrey);
        add("Sobre", "v" APP_VERSION, ui::Icon::Info, ui::color::Accent);

        spec.drawTop = [&](int index) {
            ui::drawTopChrome("Ajustes", ui::Icon::Gear, ui::color::Accent);
            ui::drawPanel();
            const ui::Rect panel = ui::TopPanel;
            std::string title;
            std::string value;
            std::string about;
            switch (index) {
            case Server:
                title = "Servidor do Catálogo";
                value = config.versionUrl;
                about = "Endereço do version.json. A mudança vale a partir da "
                        "próxima vez que o aplicativo abrir.";
                break;
            case CheckOnStart:
                title = "Verificar ao Iniciar";
                value = yesNo(config.checkOnStart);
                about = "Procura um catálogo novo sempre que o aplicativo abre.";
                break;
            case Connections:
                title = "Conexões Simultâneas";
                value = std::to_string(config.connections);
                about = "Quantas conexões cada download usa. Mais conexões "
                        "costumam baixar mais rápido.";
                break;
            case Sounds:
                title = "Sons";
                value = config.sounds ? "Ligado" : "Desligado";
                about = "Sons ao navegar, escolher e voltar.";
                break;
            case ClearImages:
                title = "Cache de Imagens";
                value = units::bytes(imageBytes);
                about = "Ícones e capas guardados no cartão SD. Eles são "
                        "baixados de novo quando aparecerem.";
                break;
            case ClearTemp:
                title = "Arquivos Temporários";
                value = units::bytes(tempBytes);
                about = "Downloads de instalações que falharam ou foram "
                        "interrompidas.";
                break;
            default:
                title = "Versão Brasileira";
                value = "v" APP_VERSION;
                about = "Catálogo " +
                        (catalogVersion.empty() ? std::string("—") : catalogVersion) +
                        ". Baseado no 3dsDL e no Universal Updater. Licença GPL-3.0.";
                break;
            }
            float y = panel.y + 14;
            ui::drawText(title, panel.x + 16, y, 0.6f, ui::color::InkStrong);
            y += 28;
            y = ui::drawWrapped(value, panel.x + 16, y, 0.52f, ui::color::Accent,
                                panel.w - 32, 3) + 10;
            ui::drawWrapped(about, panel.x + 16, y, 0.44f, ui::color::InkSoft,
                            panel.w - 32, 4);
        };

        const ui::ListResult result = ui::runList(spec, cursor, &offset);
        if (result.key != KEY_A) {
            return; // B ou fechando
        }
        sound::play(sound::Effect::Select);
        switch (result.index) {
        case Server: {
            const int choice = ui::chooseOption(
                "Servidor do Catálogo", {"Editar Endereço", "Restaurar Padrão"}, -1,
                ui::Icon::Globe, ui::color::IconBlue);
            std::string url = config.versionUrl;
            if (choice == 0 && editUrl(url)) {
                if (url.rfind("https://", 0) != 0 && url.rfind("http://", 0) != 0) {
                    ui::message("Endereço inválido",
                                "O endereço deve começar com https:// ou http://.",
                                ui::Tone::Error);
                    break;
                }
                config.versionUrl = url;
                config::save(config);
            } else if (choice == 1) {
                config.versionUrl = config::DefaultVersionUrl;
                config::save(config);
            }
            break;
        }
        case CheckOnStart:
            config.checkOnStart = !config.checkOnStart;
            config::save(config);
            break;
        case Connections: {
            const int choice = ui::chooseOption("Conexões Simultâneas",
                                                {"1", "2", "3", "4"},
                                                config.connections - 1,
                                                ui::Icon::Download,
                                                ui::color::IconTeal);
            if (choice >= 0) {
                config.connections = choice + 1;
                ops::setConnections(config.connections);
                config::save(config);
            }
            break;
        }
        case Sounds:
            config.sounds = !config.sounds;
            sound::setEnabled(config.sounds);
            config::save(config);
            break;
        case ClearImages:
            if (confirm("Limpar o cache de imagens?",
                        "Ocupa " + units::bytes(imageBytes) + " no cartão SD.",
                        "Limpar")) {
                emptyFolder(paths::ImageCache);
                imageBytes = folderBytes(paths::ImageCache);
                ui::notify({"Cache de imagens limpo", "", ui::Icon::Check,
                            ui::color::Good});
            }
            break;
        case ClearTemp:
            if (jobs::busy()) {
                ui::message("Fila em andamento",
                            "Espere a Fila terminar para limpar os arquivos "
                            "temporários.",
                            ui::Tone::Warning);
                break;
            }
            if (confirm("Limpar os arquivos temporários?",
                        "Ocupam " + units::bytes(tempBytes) + " no cartão SD.",
                        "Limpar")) {
                emptyFolder(paths::Temporary);
                tempBytes = folderBytes(paths::Temporary);
                ui::notify({"Arquivos temporários apagados", "", ui::Icon::Check,
                            ui::color::Good});
            }
            break;
        default: {
            ui::Dialog dialog;
            dialog.header = "Sobre";
            dialog.title = "Versão Brasileira v" APP_VERSION;
            dialog.body.push_back({"Traduções de fãs para o Nintendo 3DS.",
                                   ui::color::Text, 0.5f, 2});
            dialog.body.push_back({"Catálogo " + (catalogVersion.empty()
                                                       ? std::string("—")
                                                       : catalogVersion),
                                   ui::color::InkSoft, 0.45f, 1});
            dialog.body.push_back({"Baseado no 3dsDL (ankushKun) e no Universal "
                                   "Updater (Universal-Team). Licença GPL-3.0.",
                                   ui::color::InkSoft, 0.42f, 3});
            dialog.buttons = {"OK"};
            dialog.cancelButton = 0;
            ui::choose(dialog);
            break;
        }
        }
    }
}

} // namespace screens
