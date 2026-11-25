#pragma once

#include <string>

// Versões no formato MAJOR.MINOR.PATCH (Especificação 01, §2). Os números são
// comparados como números: "1.10.0" é maior que "1.9.0". Partes ausentes
// contam como zero ("1.2" == "1.2.0"); um sufixo depois de "-" ou "+" é
// ignorado.
namespace semver {

struct Version {
    long major = 0;
    long minor = 0;
    long patch = 0;
};

bool parse(const std::string &text, Version &out);
bool isValid(const std::string &text);
// -1 se a < b, 0 se iguais, 1 se a > b. Versões inválidas contam como 0.0.0.
int compare(const std::string &a, const std::string &b);

} // namespace semver
