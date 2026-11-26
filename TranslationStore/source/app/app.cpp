#include "app.hpp"

#include "app_update_screen.hpp"
#include "catalog_updater.hpp"
#include "fs_util.hpp"
#include "game_browser.hpp"
#include "image_cache.hpp"
#include "awake.hpp"
#include "installed.hpp"
#include "jobs.hpp"
#include "operations.hpp"
#include "queue_screen.hpp"
#include "semver.hpp"
#include "settings_screen.hpp"

#include <memory>
#include "home_screen.hpp"
#include "logging.hpp"
#include "paths.hpp"
#include "sound.hpp"
#include "ui.hpp"
#include "update_screen.hpp"

void App::prepareFolders()
{
    for (const char *folder :
         {paths::Root, paths::CatalogFolder, paths::ImageCache,
          paths::Installed, paths::Temporary, paths::Logs}) {
        fs::makeDirectories(folder);
    }
}

void App::loadCatalog()
{
    catalog_ = store::Catalog {};
    std::string text;
    std::string error;
    if (fs::readFile(paths::Catalog, text) &&
        !store::parseCatalog(text, catalog_, error)) {
        logging::warn("catalog.load_failed", {{"error", error}});
        catalog_ = store::Catalog {};
    }
    localVersion_ = catalog_.catalogVersion;
}

void App::comingSoon(const std::string &screen)
{
    ui::message(screen, "Esta tela ainda será desenvolvida.",
                ui::Tone::Info);
}

void App::run()
{
    prepareFolders();
    config_ = config::load();
    sound::setEnabled(config_.sounds);

    ops::setConnections(config_.connections);

    // Tela 1: atualização do aplicativo (sempre) e do catálogo (se a opção
    // "Verificar ao abrir" estiver ligada). Havendo versão nova do app, ela
    // é oferecida antes; o catálogo é verificado depois, se o app abrir.
    repository::CheckMode mode;
    mode.catalog = config_.checkOnStart;
    while (true) {
        const screens::UpdateOutcome outcome =
            screens::runUpdateScreen(config_.versionUrl, mode);
        if (outcome.quit) {
            return;
        }
        if (outcome.appUpdate) {
            if (screens::runAppUpdate(outcome.app) == screens::AppUpdateResult::Quit) {
                return;
            }
            mode.app = false;
            if (mode.catalog) {
                continue;
            }
            break;
        }
        if (mode.catalog) {
            remoteVersion_ = outcome.remoteVersion;
        }
        break;
    }
    loadCatalog();
    favorites_.load();
    imagecache::init();
    jobs::init();

    // A cada quadro, em qualquer tela: avisos da fila e console acordado
    // enquanto houver processo.
    std::unique_ptr<awake::Hold> keepAwake;
    ui::setFrameHook([&keepAwake]() {
        for (const jobs::Notice &notice : jobs::takeNotices()) {
            installed::invalidate();
            ui::notify({notice.title, notice.text,
                        notice.ok ? ui::Icon::Check : ui::Icon::Warning,
                        notice.ok ? ui::color::Good : ui::color::Caution});
        }
        awake::update(); // tampa fechada: apaga as telas e mantém o Wi-Fi
        const bool busy = jobs::busy();
        if (busy && !keepAwake) {
            keepAwake.reset(new awake::Hold());
        } else if (!busy && keepAwake) {
            keepAwake.reset();
        }
    });

    // Tela 2: tela inicial.
    while (ui::keepRunning()) {
        screens::HomeInfo info;
        info.localVersion = localVersion_;
        info.remoteVersion = remoteVersion_;
        info.queued = jobs::count();
        info.failed = jobs::anyFailed();
        for (const installed::Manifest &m : installed::all()) {
            for (const store::Game &game : catalog_.games) {
                for (const store::Translation &t : game.translations) {
                    if (t.id == m.translationId &&
                        semver::compare(t.version, m.version) > 0) {
                        ++info.updates;
                    }
                }
            }
        }
        switch (screens::runHomeScreen(info, homeFocus_)) {
        case screens::HomeChoice::Catalog:
            screens::runGameBrowser(catalog_, favorites_,
                                    screens::BrowserMode::Catalog);
            break;
        case screens::HomeChoice::Search:
            screens::runSearch(catalog_, favorites_, searchTerm_);
            break;
        case screens::HomeChoice::Favorites:
            screens::runGameBrowser(catalog_, favorites_,
                                    screens::BrowserMode::Favorites);
            break;
        case screens::HomeChoice::Library:
            screens::runLibrary(catalog_, favorites_);
            break;
        case screens::HomeChoice::Processes:
            screens::runQueueScreen();
            break;
        case screens::HomeChoice::Settings:
            screens::runSettings(config_, localVersion_);
            break;
        case screens::HomeChoice::TopDownloads:
            ui::message("+Baixados", "Em breve: as traduções mais baixadas.",
                        ui::Tone::Info);
            break;
        case screens::HomeChoice::Contact:
            ui::message("Contato", "Em breve: envie erros e pedidos de tradução.",
                        ui::Tone::Info);
            break;
        case screens::HomeChoice::Close:
            if (jobs::busy()) {
                ui::Dialog dialog;
                dialog.header = "Fechar";
                dialog.title = "Há um processo em andamento. Fechar mesmo assim?";
                dialog.body.push_back({"O processo será interrompido e os arquivos "
                                       "já gravados serão apagados."});
                dialog.buttons = {"Fechar", "Continuar"};
                dialog.dangerous = 0;
                dialog.cancelButton = 1;
                if (ui::choose(dialog) != 0) {
                    break;
                }
            }
            ui::setFrameHook(nullptr);
            jobs::shutdown();
            imagecache::shutdown();
            return;
        }
    }
    ui::setFrameHook(nullptr);
    jobs::shutdown();
    imagecache::shutdown();
}
