#include "catalog_updater.hpp"

#include "catalog.hpp"
#include "fetch.hpp"
#include "fs_util.hpp"
#include "json.hpp"
#include "logging.hpp"
#include "paths.hpp"
#include "platform.hpp"
#include "semver.hpp"

#include <algorithm>

namespace repository {
namespace {

constexpr size_t StackSize = 128 * 1024;
constexpr size_t MaxVersionBytes = 64 * 1024;
constexpr size_t MaxCatalogBytes = 16 * 1024 * 1024;

net::FetchRequest request(const std::string &url, size_t maxBytes,
                          long totalTimeout, const std::atomic<bool> *cancel)
{
    net::FetchRequest out;
    out.url = url;
    out.caFile = paths::CaBundle;
    out.userAgent = paths::UserAgent;
    out.maxBytes = maxBytes;
    out.connectTimeoutSeconds = 10;
    out.totalTimeoutSeconds = totalTimeout;
    out.cancel = cancel;
    return out;
}

std::string describe(const net::FetchResult &result)
{
    std::string text = result.message;
    if (result.httpStatus != 0) {
        text += " (HTTP " + std::to_string(result.httpStatus) + ")";
    }
    if (!result.detail.empty()) {
        text += " " + result.detail;
    }
    return text;
}

} // namespace

std::string localCatalogVersion()
{
    std::string text;
    if (!fs::readFile(paths::Catalog, text)) {
        return "";
    }
    store::Catalog catalog;
    std::string error;
    if (!store::parseCatalog(text, catalog, error)) {
        logging::warn("catalog.local_invalid", {{"error", error}});
        return "";
    }
    return catalog.catalogVersion;
}

CatalogUpdater::CatalogUpdater()
{
    LightLock_Init(&lock_);
}

CatalogUpdater::~CatalogUpdater()
{
    stop();
}

void CatalogUpdater::start(const std::string &versionUrl, CheckMode mode)
{
    join();
    url_ = versionUrl;
    mode_ = mode;
    cancel_.store(false);
    publish(UpdateStatus {});

    s32 priority = 0x30;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    // Abaixo da thread principal, para o desenho nunca esperar pela rede.
    thread_ = threadCreate(entry, this, StackSize,
                           std::min(0x3f, static_cast<int>(priority) + 2), -2,
                           false);
    if (thread_ == nullptr) {
        UpdateStatus failed;
        failed.state = UpdateState::Failed;
        failed.finished = true;
        failed.localVersion = localCatalogVersion();
        failed.hasLocal = !failed.localVersion.empty();
        failed.detail = "Não foi possível iniciar a verificação.";
        publish(failed);
    }
}

UpdateStatus CatalogUpdater::status() const
{
    LightLock_Lock(&lock_);
    UpdateStatus copy = status_;
    LightLock_Unlock(&lock_);
    return copy;
}

void CatalogUpdater::stop()
{
    cancel_.store(true);
    join();
}

void CatalogUpdater::join()
{
    if (thread_ != nullptr) {
        threadJoin(thread_, U64_MAX);
        threadFree(thread_);
        thread_ = nullptr;
    }
}

void CatalogUpdater::publish(const UpdateStatus &value)
{
    LightLock_Lock(&lock_);
    status_ = value;
    LightLock_Unlock(&lock_);
}

void CatalogUpdater::entry(void *self)
{
    static_cast<CatalogUpdater *>(self)->run();
}

void CatalogUpdater::run()
{
    UpdateStatus current;
    current.localVersion = localCatalogVersion();
    current.hasLocal = !current.localVersion.empty();
    publish(current);

    const auto finish = [&](UpdateState state, const std::string &detail) {
        current.state = state;
        current.detail = detail;
        current.finished = true;
        publish(current);
        logging::info("catalog.check",
                      {{"local", current.localVersion},
                       {"remote", current.remoteVersion},
                       {"state", std::to_string(static_cast<int>(state))},
                       {"detail", detail}});
    };

    if (platform::wifiStatus() == platform::Wifi::Disconnected) {
        finish(UpdateState::Offline, "");
        return;
    }

    // 1. version.json
    const net::FetchResult version =
        net::fetch(request(url_, MaxVersionBytes, 20, &cancel_));
    if (!version.ok) {
        // Sem resposta nenhuma do servidor costuma ser falta de internet.
        finish(version.httpStatus == 0 && version.curlCode == 6
                   ? UpdateState::Offline
                   : UpdateState::Failed,
               describe(version));
        return;
    }
    json::Value root;
    std::string error;
    if (!json::parse(version.body, root, error) ||
        root.type != json::Type::Object) {
        finish(UpdateState::Failed, "version.json inválido: " + error);
        return;
    }

    // 1b. Aplicativo: se há versão nova, ela vem antes do catálogo.
    current.app = appupdate::parse(root, url_);
    if (mode_.app && appupdate::available(current.app)) {
        finish(UpdateState::AppUpdate, "");
        return;
    }
    if (!mode_.catalog) {
        finish(UpdateState::UpToDate, "");
        return;
    }
    const std::string remote = root["catalog_version"].text();
    const std::string catalogUrl = root["catalog_url"].text();
    if (!semver::isValid(remote) || catalogUrl.empty()) {
        finish(UpdateState::Failed,
               "version.json sem \"catalog_version\" ou \"catalog_url\".");
        return;
    }
    current.remoteVersion = remote;
    publish(current);

    // 2. Compara.
    if (current.hasLocal &&
        semver::compare(remote, current.localVersion) <= 0) {
        finish(UpdateState::UpToDate, "");
        return;
    }

    // 3. Baixa e valida o catálogo novo.
    current.state = UpdateState::Downloading;
    publish(current);
    const net::FetchResult catalog =
        net::fetch(request(catalogUrl, MaxCatalogBytes, 120, &cancel_));
    if (!catalog.ok) {
        finish(UpdateState::Failed, describe(catalog));
        return;
    }
    store::Catalog parsed;
    if (!store::parseCatalog(catalog.body, parsed, error)) {
        finish(UpdateState::Failed, "Catálogo baixado é inválido: " + error);
        return;
    }

    // 4. Substitui: o atual vira catalog.bak, o novo vira catalog.json.
    fs::makeDirectories(paths::CatalogFolder);
    std::string old;
    if (fs::readFile(paths::Catalog, old)) {
        fs::writeFile(paths::CatalogBackup, old);
    }
    if (!fs::writeFile(paths::Catalog, catalog.body)) {
        finish(UpdateState::Failed,
               "Não foi possível gravar o catálogo no cartão SD.");
        return;
    }
    fs::writeFile(paths::LocalVersion, version.body);
    current.localVersion = parsed.catalogVersion;
    current.hasLocal = true;
    finish(UpdateState::Updated, "");
}

} // namespace repository
