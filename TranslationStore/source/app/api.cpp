#include "api.hpp"

#include "fetch.hpp"
#include "json.hpp"
#include "logging.hpp"
#include "paths.hpp"

#include <3ds.h>

namespace api {
namespace {

LightLock lock;
bool lockReady = false;
std::string base;

void ensureLock()
{
    if (!lockReady) {
        LightLock_Init(&lock);
        lockReady = true;
    }
}

// Texto como string JSON, com aspas.
std::string quote(const std::string &text)
{
    std::string out = "\"";
    for (const unsigned char c : text) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20) {
                break; // outros caracteres de controle ficam de fora
            }
            out += static_cast<char>(c);
        }
    }
    return out + "\"";
}

net::FetchRequest request(const std::string &url, long timeout)
{
    net::FetchRequest r;
    r.url = url;
    r.caFile = paths::CaBundle;
    r.userAgent = paths::UserAgent;
    r.maxBytes = 256 * 1024;
    r.connectTimeoutSeconds = 10;
    r.totalTimeoutSeconds = timeout;
    return r;
}

// A mensagem de erro em português que a API devolve, se houver.
std::string apiError(const net::FetchResult &answer)
{
    json::Value root;
    std::string error;
    if (!answer.body.empty() && json::parse(answer.body, root, error)) {
        const std::string text = root["error"].text();
        if (!text.empty()) {
            return text;
        }
    }
    if (answer.httpStatus == 0) {
        return "Sem conexão com o servidor.";
    }
    return "O servidor respondeu com erro " + std::to_string(answer.httpStatus) + ".";
}

net::FetchResult post(const std::string &url, const std::string &body, long timeout)
{
    net::FetchRequest r = request(url, timeout);
    r.postBody = body;
    r.post = true;
    r.headers.push_back("Content-Type: application/json");
    return net::fetch(r);
}

} // namespace

void setVersionUrl(const std::string &versionUrl)
{
    ensureLock();
    const size_t slash = versionUrl.rfind('/');
    LightLock_Lock(&lock);
    base = slash == std::string::npos ? "" : versionUrl.substr(0, slash + 1) + "api/";
    LightLock_Unlock(&lock);
}

std::string endpoint(const std::string &name)
{
    ensureLock();
    LightLock_Lock(&lock);
    const std::string url = base + name;
    LightLock_Unlock(&lock);
    return url;
}

Ranking ranking()
{
    Ranking out;
    const net::FetchResult answer = net::fetch(request(endpoint("baixados.php"), 20));
    if (!answer.ok) {
        out.message = apiError(answer);
        logging::warn("api.ranking_failed", {{"status", static_cast<int>(answer.httpStatus)},
                                            {"detail", answer.detail}});
        return out;
    }
    json::Value root;
    std::string error;
    if (!json::parse(answer.body, root, error) || root["items"].type != json::Type::Array) {
        out.message = "Resposta inválida do servidor.";
        return out;
    }
    for (const json::Value &item : root["items"].items) {
        RankItem rank;
        rank.position = static_cast<int>(item["position"].integer());
        rank.translationId = item["translation_id"].text();
        rank.gameId = item["game_id"].text();
        rank.downloads = static_cast<int>(item["downloads"].integer());
        if (!rank.translationId.empty()) {
            out.items.push_back(rank);
        }
    }
    out.ok = true;
    return out;
}

void reportDownload(const std::string &translationId)
{
    const net::FetchResult answer =
        post(endpoint("baixados.php"), "{\"translation_id\":" + quote(translationId) + "}", 15);
    if (answer.ok) {
        logging::info("api.download_reported", {{"translation", translationId}});
    } else {
        logging::warn("api.download_failed", {{"translation", translationId},
                                             {"status", static_cast<int>(answer.httpStatus)}});
    }
}

SendResult sendContact(const ContactMessage &contact)
{
    SendResult out;
    const std::string body = "{\"kind\":" + quote(contact.kind) +
                             ",\"message\":" + quote(contact.message) +
                             ",\"contact\":" + quote(contact.contact) +
                             ",\"game_id\":" + quote(contact.gameId) +
                             ",\"translation_id\":" + quote(contact.translationId) +
                             ",\"game_name\":" + quote(contact.gameName) +
                             ",\"app_version\":" + quote(APP_VERSION) + "}";
    const net::FetchResult answer = post(endpoint("contato.php"), body, 25);
    if (!answer.ok) {
        out.message = apiError(answer);
        return out;
    }
    out.ok = true;
    return out;
}

} // namespace api
