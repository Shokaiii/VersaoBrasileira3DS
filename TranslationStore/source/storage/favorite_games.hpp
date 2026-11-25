#pragma once

#include <set>
#include <string>

// Jogos favoritos, por game_id, em sdmc:/3ds/VersaoBrasileira/favorites.json.
class FavoriteGames {
public:
    void load();
    bool contains(const std::string &gameId) const;
    // Adiciona ou remove e grava no cartão SD. Retorna se ficou favorito.
    bool toggle(const std::string &gameId);
    bool empty() const { return ids_.empty(); }

private:
    void save() const;
    std::set<std::string> ids_;
};
