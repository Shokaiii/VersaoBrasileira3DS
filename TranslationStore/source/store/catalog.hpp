#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Modelo do catalog.json (Especificação 01, §3).
namespace store {

struct Image {
    std::string url; // relativa a base_url ou absoluta
    std::string sha256;
    int width = 0;
    int height = 0;
    bool empty() const { return url.empty(); }
};

struct TitleId {
    std::string region; // USA, EUR, JPN, KOR, CHN, TWN, ALL
    std::string id;     // 16 dígitos hexadecimais
};

struct Author {
    std::string name;
    std::string role;
};

struct ChangelogEntry {
    std::string version;
    std::string notes;
};

struct Compatibility {
    std::vector<std::string> regions;
    std::vector<std::string> gameVersions; // versão exigida do jogo
    std::string consoles = "all";          // "all" ou "new3ds"
    std::vector<std::string> requirements;
    std::string notes;
};

// Operações do Installation Pipeline (Especificação 01, §4.2).
enum class StepType {
    DownloadFile,
    ExtractFile,
    CopyFile,
    DeleteFile,
    InstallCia,
    UninstallCia,
    Unknown, // tipo que esta versão do app não conhece
};

struct Step {
    StepType type = StepType::Unknown;
    std::string typeName; // como veio no JSON
    std::string label;
    bool optional = false;
    // Parâmetros; cada tipo usa os seus.
    std::string file;        // downloadFile (URL), extractFile, deleteFile, installCia
    std::string output;      // downloadFile, extractFile
    std::string source;      // copyFile
    std::string destination; // copyFile
    std::string input;       // extractFile (opcional)
    std::string sha256;      // downloadFile (opcional)
    std::string titleId;     // uninstallCia (16 dígitos hexadecimais)
    bool overwrite = true;   // copyFile
    bool missingOk = true;   // deleteFile, uninstallCia
    bool deleteAfter = true; // installCia
};

struct Translation {
    std::string id;
    std::string name;
    std::string language;
    std::string version;
    std::string releaseDate;
    std::vector<Author> authors;
    std::string description;
    std::vector<ChangelogEntry> changelog;
    Compatibility compatibility;
    uint64_t downloadSize = 0;
    uint64_t installSize = 0;
    std::vector<Step> install;
    std::vector<Step> uninstall;

    // Alguma etapa usa um tipo desconhecido: a tradução aparece como
    // "requer atualização do aplicativo".
    bool needsNewerApp() const;
};

struct Game {
    std::string id;
    std::string name;
    std::string platform;
    std::string description;
    std::string publisher;
    int releaseYear = 0;
    std::vector<std::string> genres;
    std::vector<TitleId> titleIds;
    Image icon;
    Image cover;
    std::vector<Translation> translations;
};

struct Catalog {
    std::string catalogVersion;
    std::string generatedAt;
    std::string baseUrl;
    std::vector<Game> games; // em ordem alfabética do nome

    const Game *findGame(const std::string &id) const;
    // URL relativa vira base_url + url; URL absoluta fica como está.
    std::string resolveUrl(const std::string &url) const;
};

// Interpreta o texto do catalog.json. Em caso de erro, error explica em
// português o que está errado (e onde).
bool parseCatalog(const std::string &text, Catalog &out, std::string &error);

const char *stepTypeName(StepType type);

// Minúsculas sem acentos ("Pokémon Ação" -> "pokemon acao"), para ordenar e
// pesquisar.
std::string fold(const std::string &value);

} // namespace store
