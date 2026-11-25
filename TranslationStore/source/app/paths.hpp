#pragma once

// Locais no cartão SD (Especificação 01, §8).
#define VB_ROOT "sdmc:/3ds/VersaoBrasileira/"

namespace paths {
constexpr const char *Root = VB_ROOT;
constexpr const char *Config = VB_ROOT "config.json";
constexpr const char *Favorites = VB_ROOT "favorites.json";
constexpr const char *CatalogFolder = VB_ROOT "catalog/";
constexpr const char *Catalog = VB_ROOT "catalog/catalog.json";
constexpr const char *CatalogBackup = VB_ROOT "catalog/catalog.bak";
constexpr const char *LocalVersion = VB_ROOT "catalog/version.json";
constexpr const char *ImageCache = VB_ROOT "cache/images/";
constexpr const char *Installed = VB_ROOT "installed/";
constexpr const char *Temporary = VB_ROOT "tmp/";
constexpr const char *Logs = VB_ROOT "logs/";

// Arquivos que vêm dentro do .3dsx.
constexpr const char *CaBundle = "romfs:/cacert.pem";
constexpr const char *UserAgent =
    "VersaoBrasileira/" APP_VERSION " (Nintendo 3DS)";
} // namespace paths
