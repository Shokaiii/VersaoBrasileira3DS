#pragma once

#include "catalog.hpp"
#include "config.hpp"
#include "favorite_games.hpp"

#include <string>

// Inicialização e navegação entre as telas.
class App {
public:
    void run();

private:
    void prepareFolders();
    void loadCatalog();
    void comingSoon(const std::string &screen);

    config::Config config_;
    store::Catalog catalog_;
    FavoriteGames favorites_;
    std::string localVersion_;
    std::string remoteVersion_;
    int homeFocus_ = 0;
    std::string searchTerm_;
};
