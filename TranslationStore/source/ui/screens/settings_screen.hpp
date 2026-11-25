#pragma once

#include "config.hpp"

#include <string>

// Tela 8 — Ajustes (Especificação 03, §8): lista de opções na tela inferior,
// valor e descrição da selecionada na superior. Grava no config.json.
namespace screens {

void runSettings(config::Config &config, const std::string &catalogVersion);

} // namespace screens
