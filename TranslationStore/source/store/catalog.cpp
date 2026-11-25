#include "catalog.hpp"

#include "json.hpp"
#include "semver.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace store {
namespace {

std::vector<std::string> strings(const json::Value &value)
{
    std::vector<std::string> out;
    if (value.type == json::Type::Array) {
        for (const json::Value &item : value.items) {
            if (item.type == json::Type::String) {
                out.push_back(item.string);
            }
        }
    }
    return out;
}

bool flag(const json::Value &value, bool fallback)
{
    return value.type == json::Type::Bool ? value.boolean : fallback;
}

uint64_t size(const json::Value &value)
{
    return value.type == json::Type::Number && value.number > 0
               ? static_cast<uint64_t>(value.number)
               : 0;
}

Image image(const json::Value &value)
{
    Image out;
    if (value.type != json::Type::Object) {
        return out;
    }
    out.url = value["url"].text();
    out.sha256 = value["sha256"].text();
    out.width = static_cast<int>(value["width"].integer(0));
    out.height = static_cast<int>(value["height"].integer(0));
    return out;
}

StepType stepType(const std::string &name)
{
    if (name == "downloadFile") {
        return StepType::DownloadFile;
    }
    if (name == "extractFile") {
        return StepType::ExtractFile;
    }
    if (name == "copyFile") {
        return StepType::CopyFile;
    }
    if (name == "deleteFile") {
        return StepType::DeleteFile;
    }
    if (name == "installCia") {
        return StepType::InstallCia;
    }
    if (name == "uninstallCia") {
        return StepType::UninstallCia;
    }
    return StepType::Unknown;
}

bool steps(const json::Value &value, std::vector<Step> &out,
           const std::string &where, std::string &error)
{
    if (value.isNull()) {
        return true;
    }
    if (value.type != json::Type::Array) {
        error = where + ": \"steps\" deve ser uma lista.";
        return false;
    }
    int number = 0;
    for (const json::Value &item : value.items) {
        ++number;
        const std::string here = where + ", etapa " + std::to_string(number);
        if (item.type != json::Type::Object) {
            error = here + ": deve ser um objeto.";
            return false;
        }
        Step step;
        step.typeName = item["type"].text();
        step.type = stepType(step.typeName);
        step.label = item["label"].text();
        step.optional = flag(item["optional"], false);
        step.file = item["file"].text();
        step.output = item["output"].text();
        step.source = item["source"].text();
        step.destination = item["destination"].text();
        step.input = item["input"].text();
        step.sha256 = item["sha256"].text();
        step.titleId = item["title_id"].text();
        for (char &c : step.titleId) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        step.overwrite = flag(item["overwrite"], true);
        step.missingOk = flag(item["missing_ok"], true);
        step.deleteAfter = flag(item["delete_after"], true);

        const auto need = [&](const std::string &field,
                              const std::string &content) {
            if (content.empty()) {
                error = here + " (" + step.typeName + "): falta \"" + field +
                        "\".";
                return false;
            }
            return true;
        };
        bool ok = true;
        switch (step.type) {
        case StepType::DownloadFile:
            ok = need("file", step.file) && need("output", step.output);
            break;
        case StepType::ExtractFile:
            ok = need("file", step.file) && need("output", step.output);
            break;
        case StepType::CopyFile:
            ok = need("source", step.source) &&
                 need("destination", step.destination);
            break;
        case StepType::DeleteFile:
        case StepType::InstallCia:
            ok = need("file", step.file);
            break;
        case StepType::UninstallCia:
            ok = need("title_id", step.titleId);
            if (ok && (step.titleId.size() != 16 ||
                       step.titleId.find_first_not_of("0123456789ABCDEF") !=
                           std::string::npos)) {
                error = here + " (uninstallCia): \"title_id\" deve ter 16 "
                               "dígitos hexadecimais.";
                ok = false;
            }
            break;
        case StepType::Unknown:
            if (step.typeName.empty()) {
                error = here + ": falta \"type\".";
                ok = false;
            }
            break;
        }
        if (!ok) {
            return false;
        }
        out.push_back(step);
    }
    return true;
}

} // namespace

bool Translation::needsNewerApp() const
{
    const auto unknown = [](const Step &step) {
        return step.type == StepType::Unknown;
    };
    return std::any_of(install.begin(), install.end(), unknown) ||
           std::any_of(uninstall.begin(), uninstall.end(), unknown);
}

const Game *Catalog::findGame(const std::string &id) const
{
    for (const Game &game : games) {
        if (game.id == id) {
            return &game;
        }
    }
    return nullptr;
}

std::string Catalog::resolveUrl(const std::string &url) const
{
    if (url.empty() || url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0 ||
        baseUrl.empty()) {
        return url;
    }
    std::string base = baseUrl;
    if (base.back() != '/') {
        base += '/';
    }
    return base + (url.front() == '/' ? url.substr(1) : url);
}

std::string fold(const std::string &value)
{
    // Letras acentuadas do Latin-1 em UTF-8 (0xC3 0x80..0xBF).
    static const char Plain[64 + 1] =
        "aaaaaaaceeeeiiiidnooooo/ouuuuyts"  // À..ß
        "aaaaaaaceeeeiiiidnooooo/ouuuuyty"; // à..ÿ
    std::string out;
    out.reserve(value.size());
    for (size_t index = 0; index < value.size(); ++index) {
        const unsigned char c = static_cast<unsigned char>(value[index]);
        if (c == 0xC3 && index + 1 < value.size()) {
            const unsigned char next = static_cast<unsigned char>(value[index + 1]);
            if (next >= 0x80 && next <= 0xBF) {
                out += Plain[next - 0x80];
                ++index;
                continue;
            }
        }
        out += static_cast<char>(std::tolower(c));
    }
    return out;
}

const char *stepTypeName(StepType type)
{
    switch (type) {
    case StepType::DownloadFile:
        return "downloadFile";
    case StepType::ExtractFile:
        return "extractFile";
    case StepType::CopyFile:
        return "copyFile";
    case StepType::DeleteFile:
        return "deleteFile";
    case StepType::InstallCia:
        return "installCia";
    case StepType::UninstallCia:
        return "uninstallCia";
    case StepType::Unknown:
        break;
    }
    return "?";
}

bool parseCatalog(const std::string &text, Catalog &out, std::string &error)
{
    json::Value root;
    std::string parseError;
    if (!json::parse(text, root, parseError)) {
        error = "JSON inválido: " + parseError;
        return false;
    }
    if (root.type != json::Type::Object) {
        error = "O catálogo deve ser um objeto JSON.";
        return false;
    }
    Catalog catalog;
    catalog.catalogVersion = root["catalog_version"].text();
    if (!semver::isValid(catalog.catalogVersion)) {
        error = "\"catalog_version\" ausente ou inválido.";
        return false;
    }
    catalog.generatedAt = root["generated_at"].text();
    catalog.baseUrl = root["base_url"].text();

    const json::Value &games = root["games"];
    if (games.type != json::Type::Array) {
        error = "\"games\" ausente ou não é uma lista.";
        return false;
    }
    std::set<std::string> gameIds;
    std::set<std::string> translationIds;
    for (const json::Value &item : games.items) {
        if (item.type != json::Type::Object) {
            error = "Um item de \"games\" não é um objeto.";
            return false;
        }
        Game game;
        game.id = item["id"].text();
        game.name = item["name"].text();
        if (game.id.empty() || game.name.empty()) {
            error = "Jogo sem \"id\" ou \"name\".";
            return false;
        }
        if (!gameIds.insert(game.id).second) {
            error = "Jogo repetido: " + game.id;
            return false;
        }
        game.platform = item["platform"].text();
        game.description = item["description"].text();
        game.publisher = item["publisher"].text();
        game.releaseYear = static_cast<int>(item["release_year"].integer(0));
        game.genres = strings(item["genres"]);
        for (const json::Value &title : item["title_ids"].items) {
            game.titleIds.push_back(
                {title["region"].text(), title["id"].text()});
        }
        game.icon = image(item["images"]["icon"]);
        game.cover = image(item["images"]["cover"]);

        for (const json::Value &entry : item["translations"].items) {
            Translation translation;
            translation.id = entry["id"].text();
            translation.name = entry["name"].text();
            translation.version = entry["version"].text();
            const std::string where = "Tradução \"" +
                                      (translation.id.empty() ? game.id
                                                              : translation.id) +
                                      "\"";
            if (translation.id.empty() || translation.name.empty() ||
                translation.version.empty()) {
                error = where + ": falta \"id\", \"name\" ou \"version\".";
                return false;
            }
            if (!translationIds.insert(translation.id).second) {
                error = "Tradução repetida: " + translation.id;
                return false;
            }
            translation.language = entry["language"].text();
            translation.releaseDate = entry["release_date"].text();
            translation.description = entry["description"].text();
            for (const json::Value &author : entry["authors"].items) {
                translation.authors.push_back(
                    {author["name"].text(), author["role"].text()});
            }
            for (const json::Value &change : entry["changelog"].items) {
                translation.changelog.push_back(
                    {change["version"].text(), change["notes"].text()});
            }
            const json::Value &compat = entry["compatibility"];
            translation.compatibility.regions = strings(compat["regions"]);
            translation.compatibility.gameVersions =
                strings(compat["game_versions"]);
            translation.compatibility.consoles =
                compat["consoles"].text("all");
            translation.compatibility.requirements =
                strings(compat["requirements"]);
            translation.compatibility.notes = compat["notes"].text();
            translation.downloadSize = size(entry["download_size"]);
            translation.installSize = size(entry["install_size"]);
            if (!steps(entry["install"]["steps"], translation.install, where,
                       error) ||
                !steps(entry["uninstall"]["steps"], translation.uninstall,
                       where + " (desinstalação)", error)) {
                return false;
            }
            if (translation.install.empty()) {
                error = where + ": sem etapas de instalação.";
                return false;
            }
            game.translations.push_back(translation);
        }
        catalog.games.push_back(game);
    }
    std::sort(catalog.games.begin(), catalog.games.end(),
              [](const Game &a, const Game &b) {
                  return fold(a.name) < fold(b.name);
              });
    out = catalog;
    return true;
}

} // namespace store
