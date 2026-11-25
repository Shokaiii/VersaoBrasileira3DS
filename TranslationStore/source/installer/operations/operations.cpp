#include "operations.hpp"

#include "archives.hpp"
#include "cia.hpp"
#include "download_engine.hpp"
#include "fs_util.hpp"
#include "installer.hpp"
#include "paths.hpp"
#include "platform.hpp"
#include "sha256.hpp"
#include "units.hpp"

#include <3ds.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <unistd.h>

namespace ops {
namespace {

int connections = 4;

std::string lower(std::string value)
{
    for (char &c : value) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return value;
}

std::string parentOf(const std::string &path)
{
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? "" : path.substr(0, slash + 1);
}

std::string withSlash(std::string folder)
{
    if (!folder.empty() && folder.back() != '/') {
        folder += '/';
    }
    return folder;
}

Outcome failed(const std::string &message, const std::string &detail = "")
{
    Outcome out;
    out.message = message;
    out.detail = detail;
    return out;
}

Outcome cancelledOutcome()
{
    Outcome out;
    out.cancelled = true;
    out.message = "Cancelado.";
    return out;
}

Outcome success()
{
    Outcome out;
    out.ok = true;
    return out;
}

void note(Context &context, const std::string &path)
{
    if (context.written != nullptr &&
        std::find(context.written->begin(), context.written->end(), path) ==
            context.written->end()) {
        context.written->push_back(path);
    }
}

// --- download ----------------------------------------------------------------

class DownloadObserver : public net::Observer {
public:
    explicit DownloadObserver(Context &context) : context_(context) {}

    bool cancelRequested() override
    {
        return context_.cancelled && context_.cancelled();
    }

    void onProgress(uint64_t received, uint64_t total) override
    {
        meter_.sample(osGetTime(), received);
        const double rate = meter_.hasRate() ? meter_.bytesPerSecond() : 0.0;
        const int64_t left =
            total > received ? meter_.secondsLeft(total - received) : -1;
        if (context_.progress) {
            context_.progress(received, total, rate, left);
        }
    }

private:
    Context &context_;
    units::RateMeter meter_;
};

// --- extração ----------------------------------------------------------------

class ExtractObserver : public archives::Observer {
public:
    ExtractObserver(Context &context, uint64_t total)
        : context_(context), total_(total)
    {
    }

    bool cancelRequested() override
    {
        return context_.cancelled && context_.cancelled();
    }

    void onProgress(const archives::Progress &progress) override
    {
        if (context_.progress) {
            context_.progress(std::min(progress.archiveBytes, total_), total_, 0,
                              -1);
        }
    }

private:
    Context &context_;
    uint64_t total_;
};

// --- CIA ---------------------------------------------------------------------

class CiaObserver : public installer::Observer {
public:
    explicit CiaObserver(Context &context) : context_(context) {}

    bool cancelRequested() override
    {
        return context_.cancelled && context_.cancelled();
    }

    void onProgress(uint64_t done, uint64_t total) override
    {
        if (context_.progress) {
            context_.progress(done, total, 0, -1);
        }
    }

private:
    Context &context_;
};

// --- cópia -------------------------------------------------------------------

struct CopyState {
    uint64_t done = 0;
    uint64_t total = 0;
};

uint64_t measure(const std::string &folder)
{
    uint64_t bytes = 0;
    for (const fs::Entry &entry : fs::listEntries(folder, nullptr, true, false)) {
        if (entry.folder) {
            bytes += measure(folder + entry.name + "/");
        } else if (entry.size > 0) {
            bytes += static_cast<uint64_t>(entry.size);
        }
    }
    return bytes;
}

Outcome copyOne(const std::string &source, const std::string &destination,
                bool overwrite, Context &context, CopyState &state)
{
    if (!checkWritePath(destination).empty()) {
        return failed("A cópia tenta gravar numa área protegida.", destination);
    }
    if (!overwrite && fs::fileExists(destination)) {
        return success();
    }
    fs::makeDirectories(parentOf(destination));
    FILE *in = std::fopen(source.c_str(), "rb");
    if (in == nullptr) {
        return failed("Arquivo não encontrado.", source);
    }
    const std::string part = destination + ".part";
    FILE *out = std::fopen(part.c_str(), "wb");
    if (out == nullptr) {
        std::fclose(in);
        return failed("Não foi possível gravar no cartão SD.", destination);
    }
    constexpr size_t BufferSize = 128 * 1024;
    std::unique_ptr<char[]> buffer(new char[BufferSize]);
    bool ok = true;
    while (true) {
        if (context.cancelled && context.cancelled()) {
            ok = false;
            break;
        }
        const size_t got = std::fread(buffer.get(), 1, BufferSize, in);
        if (got == 0) {
            ok = !std::ferror(in);
            break;
        }
        if (std::fwrite(buffer.get(), 1, got, out) != got) {
            ok = false;
            break;
        }
        state.done += got;
        if (context.progress) {
            context.progress(state.done, state.total, 0, -1);
        }
    }
    std::fclose(in);
    if (std::fclose(out) != 0) {
        ok = false;
    }
    if (!ok) {
        fs::removeFile(part);
        if (context.cancelled && context.cancelled()) {
            return cancelledOutcome();
        }
        return failed("Falha ao copiar o arquivo (espaço no cartão SD?).",
                      destination);
    }
    fs::removeFile(destination);
    if (!fs::renameFile(part, destination)) {
        fs::removeFile(part);
        return failed("Não foi possível gravar no cartão SD.", destination);
    }
    note(context, destination);
    return success();
}

Outcome copyFolder(const std::string &source, const std::string &destination,
                   bool overwrite, Context &context, CopyState &state)
{
    bool ok = false;
    const std::vector<fs::Entry> entries =
        fs::listEntries(source, &ok, true, false);
    if (!ok) {
        return failed("Pasta não encontrada.", source);
    }
    fs::makeDirectories(destination);
    for (const fs::Entry &entry : entries) {
        Outcome result =
            entry.folder ? copyFolder(source + entry.name + "/",
                                      destination + entry.name + "/", overwrite,
                                      context, state)
                         : copyOne(source + entry.name, destination + entry.name,
                                   overwrite, context, state);
        if (!result.ok) {
            return result;
        }
    }
    return success();
}

bool removeTree(const std::string &folder)
{
    if (fs::removeFolderTree(folder)) {
        return true;
    }
    for (const fs::Entry &entry : fs::listEntries(folder, nullptr, true, false)) {
        if (entry.folder) {
            removeTree(folder + entry.name + "/");
        } else {
            fs::removeFile(folder + entry.name);
        }
    }
    std::string bare = folder;
    if (!bare.empty() && bare.back() == '/') {
        bare.pop_back();
    }
    return ::rmdir(bare.c_str()) == 0 || !fs::directoryExists(folder);
}

} // namespace

void setConnections(int count)
{
    connections = std::max(1, std::min(4, count));
}

std::string checkReadPath(const std::string &path)
{
    if (path.rfind("sdmc:/", 0) != 0) {
        return "Caminho fora do cartão SD: " + path;
    }
    if (path.find('\\') != std::string::npos ||
        path.find("/../") != std::string::npos ||
        (path.size() >= 3 && path.compare(path.size() - 3, 3, "/..") == 0)) {
        return "Caminho inválido: " + path;
    }
    return "";
}

std::string checkWritePath(const std::string &path)
{
    const std::string problem = checkReadPath(path);
    if (!problem.empty()) {
        return problem;
    }
    const std::string low = lower(path);
    static const char *const Protected[] = {
        "sdmc:/nintendo 3ds",
        "sdmc:/boot.firm",
        "sdmc:/boot.3dsx",
        "sdmc:/luma/config.bin",
        "sdmc:/3ds/versaobrasileira/config.json",
        "sdmc:/3ds/versaobrasileira/favorites.json",
        "sdmc:/3ds/versaobrasileira/catalog/",
        "sdmc:/3ds/versaobrasileira/installed/",
    };
    for (const char *prefix : Protected) {
        if (low.rfind(prefix, 0) == 0) {
            return "Área protegida do cartão SD: " + path;
        }
    }
    return "";
}

std::string checkDeletePath(const std::string &path)
{
    const std::string problem = checkWritePath(path);
    if (!problem.empty()) {
        return problem;
    }
    const std::string low = lower(path);
    static const char *const Folders[] = {"sdmc:/", "sdmc:/luma/", "sdmc:/luma",
                                          "sdmc:/3ds/", "sdmc:/3ds",
                                          "sdmc:/luma/titles/", "sdmc:/luma/titles"};
    for (const char *folder : Folders) {
        if (low == folder) {
            return "Não é permitido apagar esta pasta: " + path;
        }
    }
    return "";
}

std::string checkUrl(const std::string &url)
{
    if (url.rfind("https://", 0) == 0 || url.rfind("http://", 0) == 0) {
        return "";
    }
    return "Endereço de download inválido: " + url;
}

Outcome download(const std::string &url, const std::string &output,
                 const std::string &expectedSha, Context &context)
{
    const std::string folder = parentOf(output);
    fs::makeDirectories(folder);
    net::Request request;
    request.url = url;
    request.folder = folder;
    request.caFile = paths::CaBundle;
    request.userAgent = paths::UserAgent;
    request.connections = connections;
    request.freeSpace = [](const std::string &) { return platform::sdFreeBytes(); };
    DownloadObserver observer(context);
    const net::Result result = net::download(request, observer);
    if (result.outcome == net::Outcome::Cancelled) {
        return cancelledOutcome();
    }
    if (result.outcome != net::Outcome::Completed) {
        std::string detail = result.message;
        if (!result.detail.empty()) {
            detail += " " + result.detail;
        }
        if (result.httpStatus != 0) {
            detail += " (HTTP " + std::to_string(result.httpStatus) + ")";
        }
        return failed(result.noSpace ? "Espaço insuficiente no cartão SD."
                                     : "Falha no download.",
                      detail);
    }
    if (!expectedSha.empty()) {
        const std::string actual = sha256::ofFile(result.partPath);
        if (lower(actual) != lower(expectedSha)) {
            fs::removeFile(result.partPath);
            return failed("O arquivo baixado está corrompido (sha256 diferente).",
                          url);
        }
    }
    if (result.partPath != output) {
        fs::removeFile(output);
        if (!fs::renameFile(result.partPath, output)) {
            fs::removeFile(result.partPath);
            return failed("Não foi possível gravar o arquivo no cartão SD.", output);
        }
    }
    note(context, output);
    return success();
}

Outcome extract(const std::string &file, const std::string &output,
                const std::string &input, Context &context)
{
    archives::Listing listing;
    std::string error;
    if (!archives::read(file, archives::Scan::Full, listing, error)) {
        return failed("Arquivo compactado inválido.", error);
    }
    const std::string folder = withSlash(output);
    fs::makeDirectories(folder);

    archives::Options options;
    options.existing = archives::Existing::Overwrite;
    std::string prefix = input;
    while (!prefix.empty() && prefix.front() == '/') {
        prefix.erase(prefix.begin());
    }
    if (!prefix.empty()) {
        prefix = withSlash(prefix);
        options.place = [prefix](const std::string &path, bool) {
            return path.rfind(prefix, 0) == 0 ? path.substr(prefix.size())
                                              : std::string();
        };
    }
    // Confere o destino de cada arquivo do pacote antes de gravar qualquer
    // coisa: um .zip extraído na raiz não pode mexer em áreas protegidas.
    {
        const archives::Listing planned =
            options.place ? archives::placed(listing, options.place) : listing;
        for (const archives::Entry &entry : planned.entries) {
            if (entry.path.empty()) {
                continue;
            }
            const std::string problem = checkWritePath(folder + entry.path);
            if (!problem.empty()) {
                return failed("O pacote tenta gravar numa área protegida.",
                              entry.path);
            }
        }
    }
    ExtractObserver observer(context, std::max<uint64_t>(1, listing.archiveSize));
    const archives::Result result =
        archives::extract(file, listing, folder, options, observer);
    if (result.cancelled) {
        return cancelledOutcome();
    }
    if (!result.ok) {
        return failed("Falha ao extrair os arquivos.", result.message);
    }
    const archives::Listing shown =
        options.place ? archives::placed(listing, options.place) : listing;
    for (const archives::Entry &entry : shown.entries) {
        if (!entry.directory && entry.supported && !entry.path.empty()) {
            note(context, folder + entry.path);
        }
    }
    return success();
}

Outcome copy(const std::string &source, const std::string &destination,
             bool overwrite, Context &context)
{
    CopyState state;
    const bool folder = !source.empty() && source.back() == '/';
    if (folder) {
        state.total = measure(source);
        return copyFolder(source, withSlash(destination), overwrite, context,
                          state);
    }
    const int64_t size = fs::fileSize(source);
    if (size < 0) {
        return failed("Arquivo não encontrado.", source);
    }
    state.total = static_cast<uint64_t>(size);
    // Destino terminado em "/" é uma pasta: mantém o nome do arquivo.
    std::string target = destination;
    if (!target.empty() && target.back() == '/') {
        target += source.substr(source.find_last_of('/') + 1);
    }
    return copyOne(source, target, overwrite, context, state);
}

Outcome remove(const std::string &path, bool missingOk)
{
    if (!path.empty() && path.back() == '/') {
        if (!fs::directoryExists(path)) {
            return missingOk ? success() : failed("Pasta não encontrada.", path);
        }
        return removeTree(path) ? success()
                                : failed("Não foi possível apagar a pasta.", path);
    }
    if (!fs::fileExists(path)) {
        return missingOk ? success() : failed("Arquivo não encontrado.", path);
    }
    return fs::removeFile(path) ? success()
                                : failed("Não foi possível apagar o arquivo.", path);
}

Outcome installCia(const std::string &file, bool deleteAfter, Context &context)
{
    if (!installer::available()) {
        return failed("Instalação de CIA indisponível.",
                      installer::unavailableReason());
    }
    cia::Info info;
    std::string error;
    if (!cia::readInfo(file, info, error)) {
        return failed("Arquivo CIA inválido.", error);
    }
    CiaObserver observer(context);
    const installer::Result result =
        installer::installCia(file, info.titleId, observer, true);
    if (result.outcome == installer::Outcome::Cancelled) {
        return cancelledOutcome();
    }
    if (result.outcome != installer::Outcome::Installed) {
        char code[16];
        std::snprintf(code, sizeof(code), "0x%08lX",
                      static_cast<unsigned long>(result.code));
        return failed("Falha ao instalar o CIA.",
                      result.message + " (" + code + ")");
    }
    if (context.cias != nullptr) {
        char id[24];
        std::snprintf(id, sizeof(id), "%016llX",
                      static_cast<unsigned long long>(info.titleId));
        context.cias->push_back(id);
    }
    if (deleteAfter) {
        fs::removeFile(file);
    }
    return success();
}

std::string checkUninstallTitle(const std::string &titleIdHex)
{
    if (titleIdHex.size() != 16 ||
        titleIdHex.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) {
        return "Title ID inválido: " + titleIdHex;
    }
    // Parte alta do Title ID: 00040000 jogo, 00040002 demo, 0004000E
    // atualização, 0004008C DLC. O resto é do sistema e nunca é removido.
    const std::string high = lower(titleIdHex.substr(0, 8));
    if (high != "00040000" && high != "00040002" && high != "0004000e" &&
        high != "0004008c") {
        return "Este Title ID é do sistema e não pode ser desinstalado: " + titleIdHex;
    }
    return "";
}

Outcome uninstallCia(const std::string &titleIdHex, bool missingOk)
{
    const std::string problem = checkUninstallTitle(titleIdHex);
    if (!problem.empty()) {
        return failed("Não é possível desinstalar este título.", problem);
    }
    const unsigned long long titleId = std::strtoull(titleIdHex.c_str(), nullptr, 16);
    u64 wanted = titleId;
    AM_TitleEntry entry;
    if (R_FAILED(AM_GetTitleInfo(MEDIATYPE_SD, 1, &wanted, &entry))) {
        // Não está instalado no cartão SD.
        return missingOk ? success()
                         : failed("O CIA não está instalado.", titleIdHex);
    }
    const Result result = AM_DeleteTitle(MEDIATYPE_SD, titleId);
    if (R_FAILED(result)) {
        char code[16];
        std::snprintf(code, sizeof(code), "0x%08lX",
                      static_cast<unsigned long>(result));
        return failed("Não foi possível desinstalar o CIA.", code);
    }
    return success();
}

void removeFolder(const std::string &folder)
{
    if (fs::directoryExists(folder)) {
        removeTree(withSlash(folder));
    }
}

} // namespace ops
