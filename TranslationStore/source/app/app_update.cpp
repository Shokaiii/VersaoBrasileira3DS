#include "app_update.hpp"

#include "fs_util.hpp"
#include "logging.hpp"
#include "operations.hpp"
#include "paths.hpp"
#include "semver.hpp"

#include <algorithm>
#include <cctype>

namespace appupdate {
namespace {

constexpr size_t StackSize = 64 * 1024;
const std::string DownloadFolder = std::string(paths::Temporary) + "app/";

std::string launchPath;
std::string swapFrom; // .3dsx novo, à espera da troca
std::string swapTo;

std::string lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return text;
}

bool endsWith(const std::string &text, const std::string &suffix)
{
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// "sim"/"nao" (também aceita true/false).
bool isYes(const json::Value &value)
{
    if (value.type == json::Type::Bool) {
        return value.boolean;
    }
    const std::string text = lower(value.text());
    return text == "sim" || text == "s" || text == "true";
}

// URLs relativas partem da pasta do version.json.
std::string resolve(const std::string &url, const std::string &versionUrl)
{
    if (url.empty() || url.rfind("https://", 0) == 0 || url.rfind("http://", 0) == 0) {
        return url;
    }
    const size_t scheme = versionUrl.find("://");
    if (scheme == std::string::npos) {
        return url;
    }
    if (url[0] == '/') {
        const size_t host = versionUrl.find('/', scheme + 3);
        return (host == std::string::npos ? versionUrl : versionUrl.substr(0, host)) + url;
    }
    const size_t slash = versionUrl.rfind('/');
    return versionUrl.substr(0, slash + 1) + url;
}

} // namespace

Format runningFormat()
{
    return envIsHomebrew() ? Format::ThreeDsx : Format::Cia;
}

const char *formatName(Format format)
{
    return format == Format::ThreeDsx ? ".3dsx" : ".cia";
}

void setLaunchPath(const char *path)
{
    launchPath = path != nullptr ? path : "";
}

Release parse(const json::Value &root, const std::string &versionUrl)
{
    Release out;
    const json::Value &app = root["app"];
    if (app.type != json::Type::Object) {
        return out;
    }
    out.present = true;
    out.version = app["version"].text();
    out.required = isYes(app["required"]);
    for (const json::Value &note : app["notes"].items) {
        if (!note.text().empty()) {
            out.notes.push_back(note.text());
        }
    }
    const bool homebrew = runningFormat() == Format::ThreeDsx;
    out.url = resolve(app[homebrew ? "3dsx_url" : "cia_url"].text(), versionUrl);
    out.sha256 = app[homebrew ? "3dsx_sha256" : "cia_sha256"].text();

    if (!semver::isValid(out.version)) {
        out.problem = "Versão do aplicativo inválida no version.json.";
    } else if (out.url.empty() || out.sha256.size() != 64) {
        out.problem = std::string("O version.json não traz o arquivo ") +
                      formatName(runningFormat()) + " com sha256.";
    } else if (!ops::checkUrl(out.url).empty()) {
        out.problem = ops::checkUrl(out.url);
    } else if (homebrew && (launchPath.rfind("sdmc:/", 0) != 0 ||
                            !endsWith(lower(launchPath), ".3dsx"))) {
        out.problem = "Não foi possível localizar o .3dsx em execução (" +
                      launchPath + ").";
    }
    return out;
}

bool available(const Release &release)
{
    if (!release.present || semver::compare(release.version, APP_VERSION) <= 0) {
        return false;
    }
    if (!release.problem.empty()) {
        logging::warn("app_update.unavailable",
                      {{"version", release.version}, {"problem", release.problem}});
        return false;
    }
    return true;
}

Updater::Updater()
{
    LightLock_Init(&lock_);
}

Updater::~Updater()
{
    cancel();
    join();
}

void Updater::start(const Release &release)
{
    join();
    release_ = release;
    cancel_.store(false);
    publish(Progress {});
    s32 priority = 0x30;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    thread_ = threadCreate(entry, this, StackSize,
                           std::min(0x3f, static_cast<int>(priority) + 2), -2, false);
    if (thread_ == nullptr) {
        Progress failed;
        failed.phase = Phase::Failed;
        failed.message = "Não foi possível iniciar a atualização.";
        publish(failed);
    }
}

Progress Updater::progress() const
{
    LightLock_Lock(&lock_);
    Progress copy = progress_;
    LightLock_Unlock(&lock_);
    return copy;
}

void Updater::cancel()
{
    cancel_.store(true);
}

void Updater::join()
{
    if (thread_ != nullptr) {
        threadJoin(thread_, U64_MAX);
        threadFree(thread_);
        thread_ = nullptr;
    }
}

void Updater::publish(const Progress &value)
{
    LightLock_Lock(&lock_);
    progress_ = value;
    LightLock_Unlock(&lock_);
}

void Updater::entry(void *self)
{
    static_cast<Updater *>(self)->run();
}

void Updater::run()
{
    const Format format = runningFormat();
    Progress current;
    const auto fail = [&](const ops::Outcome &outcome) {
        current.phase = outcome.cancelled ? Phase::Cancelled : Phase::Failed;
        current.message = outcome.message;
        current.detail = outcome.detail;
        publish(current);
        logging::warn("app_update.failed", {{"version", release_.version},
                                            {"message", outcome.message},
                                            {"detail", outcome.detail}});
    };

    ops::Context context;
    context.cancelled = [this]() { return cancel_.load(); };
    context.progress = [&](uint64_t done, uint64_t total, double rate,
                           int64_t secondsLeft) {
        current.done = done;
        current.total = total;
        current.rate = rate;
        current.secondsLeft = secondsLeft;
        publish(current);
    };

    // 1. Baixa e confere o sha256 (o arquivo só ganha o nome final se bater).
    ops::removeFolder(DownloadFolder);
    fs::makeDirectories(DownloadFolder);
    const std::string file = DownloadFolder + "VersaoBrasileira" + formatName(format);
    ops::Outcome outcome = ops::download(release_.url, file, release_.sha256, context);
    if (!outcome.ok) {
        fail(outcome);
        return;
    }

    if (format == Format::Cia) {
        // 2. Instala por cima da versão atual.
        current = Progress {};
        current.phase = Phase::Installing;
        publish(current);
        context.cancelled = []() { return false; };
        outcome = ops::installCia(file, true, context);
        if (!outcome.ok) {
            fail(outcome);
            return;
        }
    } else {
        // 2. A troca do .3dsx fica para o fim (applyPendingSwap).
        swapFrom = file;
        swapTo = launchPath;
    }
    current.phase = Phase::Ready;
    publish(current);
    logging::info("app_update.ready", {{"version", release_.version},
                                       {"format", formatName(format)}});
}

void applyPendingSwap()
{
    if (swapFrom.empty() || swapTo.empty()) {
        return;
    }
    const std::string backup = swapTo + ".bak";
    fs::removeFile(backup);
    if (fs::fileExists(swapTo) && !fs::renameFile(swapTo, backup)) {
        logging::warn("app_update.swap_failed", {{"step", "backup"}, {"path", swapTo}});
        return;
    }
    if (!fs::renameFile(swapFrom, swapTo)) {
        fs::renameFile(backup, swapTo); // volta a versão anterior
        logging::warn("app_update.swap_failed", {{"step", "replace"}, {"path", swapTo}});
        return;
    }
    logging::info("app_update.swapped", {{"path", swapTo}});
    swapFrom.clear();
}

void relaunchOnExit()
{
    u64 programId = 0;
    if (R_SUCCEEDED(APT_GetProgramID(&programId))) {
        aptSetChainloader(programId, MEDIATYPE_SD);
    }
}

} // namespace appupdate
