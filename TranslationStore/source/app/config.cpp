#include "config.hpp"

#include "fs_util.hpp"
#include "json.hpp"
#include "paths.hpp"

#include <cstdio>

namespace config {

std::string quote(const std::string &value)
{
    std::string out = "\"";
    for (const char c : value) {
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
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char code[8];
                std::snprintf(code, sizeof(code), "\\u%04x", c);
                out += code;
            } else {
                out += c;
            }
        }
    }
    return out + "\"";
}

Config load()
{
    Config value;
    std::string text;
    if (!fs::readFile(paths::Config, text)) {
        save(value);
        return value;
    }
    json::Value root;
    std::string error;
    if (!json::parse(text, root, error) || root.type != json::Type::Object) {
        return value;
    }
    const std::string url = root["version_url"].text();
    if (!url.empty()) {
        value.versionUrl = url;
    }
    if (root["check_on_start"].type == json::Type::Bool) {
        value.checkOnStart = root["check_on_start"].boolean;
    }
    const int64_t connections = root["connections"].integer(value.connections);
    if (connections >= 1 && connections <= 4) {
        value.connections = static_cast<int>(connections);
    }
    if (root["sounds"].type == json::Type::Bool) {
        value.sounds = root["sounds"].boolean;
    }
    return value;
}

bool save(const Config &value)
{
    fs::makeDirectories(paths::Root);
    std::string text = "{\n";
    text += "  \"version_url\": " + quote(value.versionUrl) + ",\n";
    text += std::string("  \"check_on_start\": ") +
            (value.checkOnStart ? "true" : "false") + ",\n";
    text += "  \"connections\": " + std::to_string(value.connections) + ",\n";
    text += std::string("  \"sounds\": ") + (value.sounds ? "true" : "false") +
            "\n}\n";
    return fs::writeFile(paths::Config, text);
}

} // namespace config
