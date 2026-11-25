#include "favorite_games.hpp"

#include "config.hpp"
#include "fs_util.hpp"
#include "json.hpp"
#include "paths.hpp"

void FavoriteGames::load()
{
    ids_.clear();
    std::string text;
    if (!fs::readFile(paths::Favorites, text)) {
        return;
    }
    json::Value root;
    std::string error;
    if (!json::parse(text, root, error)) {
        return;
    }
    for (const json::Value &item : root["games"].items) {
        if (item.type == json::Type::String && !item.string.empty()) {
            ids_.insert(item.string);
        }
    }
}

bool FavoriteGames::contains(const std::string &gameId) const
{
    return ids_.count(gameId) != 0;
}

bool FavoriteGames::toggle(const std::string &gameId)
{
    bool now = false;
    if (ids_.erase(gameId) == 0) {
        ids_.insert(gameId);
        now = true;
    }
    save();
    return now;
}

void FavoriteGames::save() const
{
    std::string text = "{\n  \"games\": [";
    bool first = true;
    for (const std::string &id : ids_) {
        text += first ? "\n    " : ",\n    ";
        text += config::quote(id);
        first = false;
    }
    text += first ? "]\n}\n" : "\n  ]\n}\n";
    fs::makeDirectories(paths::Root);
    fs::writeFile(paths::Favorites, text);
}
