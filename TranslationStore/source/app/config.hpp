#pragma once

#include <string>

// Preferências salvas em sdmc:/3ds/VersaoBrasileira/config.json.
namespace config {

// Endereço padrão do version.json, gravado no config.json na primeira execução.
// Para usar outro servidor, edite "version_url" no config.json do cartão SD.
constexpr const char *DefaultVersionUrl =
    "https://versaobrasileira3ds.com.br/versaobrasileirahb/version.json";

struct Config {
    std::string versionUrl = DefaultVersionUrl;
    bool checkOnStart = true;
    int connections = 4; // 1 a 4
    bool sounds = true;
};

// Lê o config.json. Se ele não existir, cria um com os valores padrão, para
// que possa ser editado no computador.
Config load();
bool save(const Config &value);

// Texto JSON com aspas, barras e controles escapados.
std::string quote(const std::string &value);

} // namespace config
