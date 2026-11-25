#pragma once

#include <string>
#include <vector>

// Manifestos das traduções instaladas (Especificação 01, §6), em
// sdmc:/3ds/VersaoBrasileira/installed/<translation-id>.json.
namespace installed {

struct Manifest {
    std::string translationId;
    std::string gameId;
    std::string gameName;
    std::string translationName;
    std::string version;
    std::string installedAt; // ISO 8601, UTC
    std::string iconUrl;
    std::string iconSha256;
    std::vector<std::string> files;
    std::vector<std::string> cias;
};

bool load(const std::string &translationId, Manifest &out);
bool save(const Manifest &manifest);
void erase(const std::string &translationId);
std::vector<Manifest> all();

// Versão instalada, ou "" se não estiver instalada. Usa um cache em
// memória; chame invalidate() depois que uma instalação terminar.
std::string version(const std::string &translationId);
void invalidate();
// Muda a cada invalidate(): telas comparam para saber se precisam recarregar.
unsigned generation();

} // namespace installed
