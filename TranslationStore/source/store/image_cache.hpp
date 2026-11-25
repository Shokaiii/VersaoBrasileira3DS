#pragma once

#include "catalog.hpp"

#include <citro2d.h>

// Ícones (48x48) e capas (112x128) do catálogo, carregados sob demanda
// (Especificação 02, §5): memória -> cartão SD -> download em segundo plano.
// Enquanto uma imagem não está pronta, as funções devolvem nullptr e a tela
// desenha um marcador no lugar.
namespace imagecache {

constexpr int IconSize = 48;
constexpr int CoverWidth = 112;
constexpr int CoverHeight = 128;

void init();
void shutdown();

// Chame uma vez por quadro, na thread principal: envia para a GPU as
// imagens que terminaram de carregar.
void update();

// url já resolvida (Catalog::resolveUrl).
const C2D_Image *icon(const store::Image &image, const std::string &url);
const C2D_Image *cover(const store::Image &image, const std::string &url);

} // namespace imagecache
