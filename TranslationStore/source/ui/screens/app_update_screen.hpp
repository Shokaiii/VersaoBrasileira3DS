#pragma once

#include "app_update.hpp"

// Janela de atualização do aplicativo, mostrada ao abrir quando o
// version.json traz uma versão nova (Especificação 03, §1.1).
namespace screens {

enum class AppUpdateResult {
    Open, // seguir para o app sem atualizar
    Quit, // fechar o app (atualização concluída, ou recusada se obrigatória)
};

AppUpdateResult runAppUpdate(const appupdate::Release &release);

} // namespace screens
