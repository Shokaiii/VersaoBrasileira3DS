#pragma once

#include "catalog_updater.hpp"

#include <string>

// Tela 1 — Atualização do catálogo (Especificação 03, §1).
namespace screens {

struct UpdateOutcome {
    bool quit = false;         // o usuário escolheu Fechar ou o app fechou
    std::string remoteVersion; // "" quando não foi possível saber
    bool appUpdate = false;    // há versão nova do aplicativo: ver app
    appupdate::Release app;
};

// Com mode.catalog falso, só procura versão nova do aplicativo e volta sem
// mostrar erros nem esperar.
UpdateOutcome runUpdateScreen(const std::string &versionUrl,
                              repository::CheckMode mode = {});

} // namespace screens
