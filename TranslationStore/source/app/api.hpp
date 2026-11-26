#pragma once

#include <string>
#include <vector>

// API do servidor (Especificação 05). O endereço é a pasta do version.json
// + "api/": https://.../versaobrasileirahb/version.json →
// https://.../versaobrasileirahb/api/.
namespace api {

// Chame quando o endereço do version.json for definido ou mudar.
void setVersionUrl(const std::string &versionUrl);
std::string endpoint(const std::string &name);

struct RankItem {
    int position = 0;
    std::string translationId;
    std::string gameId;
    int downloads = 0;
};

struct Ranking {
    bool ok = false;
    std::vector<RankItem> items;
    std::string message; // quando !ok
};

// Bloqueia até a resposta (use fora da thread da interface).
Ranking ranking();

// +1 para a tradução (primeira instalação no console). Bloqueia; falhas
// só vão para o registro.
void reportDownload(const std::string &translationId);

struct ContactMessage {
    std::string kind; // "erro", "pedido" ou "outro"
    std::string message;
    std::string contact;
    std::string gameId;
    std::string translationId;
    std::string gameName;
};

struct SendResult {
    bool ok = false;
    std::string message; // quando !ok
};

// Bloqueia até a resposta.
SendResult sendContact(const ContactMessage &contact);

} // namespace api
