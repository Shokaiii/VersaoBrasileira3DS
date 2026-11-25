#pragma once

#include <string>

// Tela 2 — Tela inicial (Especificação 03, §2).
namespace screens {

enum class HomeChoice { Catalog, Search, Favorites, Library, Processes, Settings, Close };

struct HomeInfo {
    std::string localVersion;  // "" quando não há catálogo
    std::string remoteVersion; // "" quando não se sabe
    int updates = 0;           // traduções instaladas com versão nova
    int queued = 0;            // processos na fila ou rodando
    bool failed = false;       // algum processo falhou
};

HomeChoice runHomeScreen(const HomeInfo &info, int &focus);

} // namespace screens
