#include "installed.hpp"

#include "config.hpp"
#include "fs_util.hpp"
#include "json.hpp"
#include "paths.hpp"

#include <atomic>
#include <map>

namespace installed {
namespace {

std::map<std::string, std::string> versions;
std::atomic<bool> cached {false};
std::atomic<unsigned> changes {0};

std::string pathFor(const std::string &translationId)
{
    return std::string(paths::Installed) + translationId + ".json";
}

std::string list(const std::vector<std::string> &items)
{
    std::string out = "[";
    for (size_t index = 0; index < items.size(); ++index) {
        out += index == 0 ? "\n    " : ",\n    ";
        out += config::quote(items[index]);
    }
    return out + (items.empty() ? "]" : "\n  ]");
}

std::vector<std::string> strings(const json::Value &value)
{
    std::vector<std::string> out;
    for (const json::Value &item : value.items) {
        if (item.type == json::Type::String) {
            out.push_back(item.string);
        }
    }
    return out;
}

} // namespace

bool load(const std::string &translationId, Manifest &out)
{
    std::string text;
    if (!fs::readFile(pathFor(translationId), text)) {
        return false;
    }
    json::Value root;
    std::string error;
    if (!json::parse(text, root, error) || root.type != json::Type::Object) {
        return false;
    }
    out = Manifest {};
    out.translationId = root["translation_id"].text(translationId);
    out.gameId = root["game_id"].text();
    out.gameName = root["game_name"].text();
    out.translationName = root["translation_name"].text();
    out.version = root["version"].text();
    out.installedAt = root["installed_at"].text();
    out.iconUrl = root["icon_url"].text();
    out.iconSha256 = root["icon_sha256"].text();
    out.files = strings(root["files"]);
    out.cias = strings(root["cias"]);
    return true;
}

bool save(const Manifest &m)
{
    std::string text = "{\n";
    text += "  \"translation_id\": " + config::quote(m.translationId) + ",\n";
    text += "  \"game_id\": " + config::quote(m.gameId) + ",\n";
    text += "  \"game_name\": " + config::quote(m.gameName) + ",\n";
    text += "  \"translation_name\": " + config::quote(m.translationName) + ",\n";
    text += "  \"version\": " + config::quote(m.version) + ",\n";
    text += "  \"installed_at\": " + config::quote(m.installedAt) + ",\n";
    text += "  \"icon_url\": " + config::quote(m.iconUrl) + ",\n";
    text += "  \"icon_sha256\": " + config::quote(m.iconSha256) + ",\n";
    text += "  \"files\": " + list(m.files) + ",\n";
    text += "  \"cias\": " + list(m.cias) + "\n}\n";
    fs::makeDirectories(paths::Installed);
    const bool ok = fs::writeFile(pathFor(m.translationId), text);
    invalidate();
    return ok;
}

void erase(const std::string &translationId)
{
    fs::removeFile(pathFor(translationId));
    invalidate();
}

std::vector<Manifest> all()
{
    std::vector<Manifest> out;
    for (const fs::Entry &entry : fs::listEntries(paths::Installed, nullptr, false, false)) {
        const std::string &name = entry.name;
        if (entry.folder || name.size() <= 5 ||
            name.compare(name.size() - 5, 5, ".json") != 0) {
            continue;
        }
        Manifest manifest;
        if (load(name.substr(0, name.size() - 5), manifest)) {
            out.push_back(manifest);
        }
    }
    return out;
}

std::string version(const std::string &translationId)
{
    if (!cached) {
        versions.clear();
        for (const Manifest &manifest : all()) {
            versions[manifest.translationId] = manifest.version;
        }
        cached = true;
    }
    const auto found = versions.find(translationId);
    return found == versions.end() ? "" : found->second;
}

void invalidate()
{
    cached = false;
    ++changes;
}

unsigned generation()
{
    return changes.load();
}

} // namespace installed
