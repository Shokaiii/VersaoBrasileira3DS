#pragma once

#include "catalog.hpp"
#include "favorite_games.hpp"

#include <string>

// Tela 3 — Catálogo, e Tela 5 — Favoritos (Especificação 03, §3 e §5):
// grade de jogos 2x4 na tela superior, detalhes do jogo selecionado na
// inferior.
namespace screens {

enum class BrowserMode { Catalog, Favorites };

void runGameBrowser(const store::Catalog &catalog, FavoriteGames &favorites,
                    BrowserMode mode);

// Tela 4 — Busca (Especificação 03, §4): resultados na tela superior,
// teclado na inferior; A abre os detalhes do jogo selecionado. term é
// mantido entre uma visita e outra.
// Tela 6 — Biblioteca (Especificação 03, §6): traduções instaladas, com
// Atualizar e Desinstalar (as duas pela Fila).
void runLibrary(const store::Catalog &catalog, FavoriteGames &favorites);

void runSearch(const store::Catalog &catalog, FavoriteGames &favorites,
               std::string &term);

} // namespace screens
