#include "image_cache.hpp"

#include "fetch.hpp"
#include "fs_util.hpp"
#include "image.hpp"
#include "logging.hpp"
#include "paths.hpp"
#include "platform.hpp"
#include "stb_image.h"

#include <3ds.h>

#include <algorithm>
#include <cstdio>
#include <deque>
#include <map>
#include <vector>

namespace imagecache {
namespace {

enum class Kind { Icon, Cover };

// Ícones: uma textura 512x512 com 10x10 células de 48x48.
constexpr int AtlasSize = 512;
constexpr int AtlasColumns = 10;
constexpr int IconSlots = AtlasColumns * AtlasColumns;
// Capas: texturas próprias de 128x128.
constexpr int CoverTexture = 128;
constexpr int CoverSlots = 6;
// Quantas imagens prontas vão para a GPU por quadro.
constexpr int UploadsPerFrame = 4;

struct Request {
    std::string key;
    std::string url;
    Kind kind = Kind::Icon;
};

struct Result {
    std::string key;
    Kind kind = Kind::Icon;
    bool ok = false;
    std::vector<uint16_t> pixels; // RGB565, linha por linha
};

enum class State { Loading, Ready, Failed };

struct Entry {
    State state = State::Loading;
    Kind kind = Kind::Icon;
    int slot = -1;
    uint64_t lastUsed = 0;
};

bool started = false;
bool texturesReady = false;
C3D_Tex atlas {};
C3D_Tex covers[CoverSlots] {};
Tex3DS_SubTexture iconSubs[IconSlots] {};
C2D_Image iconImages[IconSlots] {};
Tex3DS_SubTexture coverSub {};
C2D_Image coverImages[CoverSlots] {};
std::string iconOwner[IconSlots];
std::string coverOwner[CoverSlots];

std::map<std::string, Entry> entries;
uint64_t frame = 0;

// Fila entre a thread principal e a de carregamento.
LightLock lock;
LightEvent wake;
std::deque<Request> requests;
std::deque<Result> results;
Thread worker = nullptr;
volatile bool stopping = false;

uint64_t fnv1a(const std::string &text)
{
    uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char c : text) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string keyFor(const store::Image &image, const std::string &url)
{
    if (image.sha256.size() >= 16) {
        return image.sha256.substr(0, 16);
    }
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "%016llx",
                  static_cast<unsigned long long>(fnv1a(url)));
    return buffer;
}

uint16_t rgb565(const unsigned char *p)
{
    return static_cast<uint16_t>(((p[0] >> 3) << 11) | ((p[1] >> 2) << 5) |
                                 (p[2] >> 3));
}

// Decodifica PNG/JPEG e ajusta ao tamanho esperado (vizinho mais próximo,
// para tolerar imagens fora do padrão).
bool decode(const std::string &data, int width, int height,
            std::vector<uint16_t> &out)
{
    int w = 0;
    int h = 0;
    int channels = 0;
    unsigned char *pixels = stbi_load_from_memory(
        reinterpret_cast<const unsigned char *>(data.data()),
        static_cast<int>(data.size()), &w, &h, &channels, 3);
    if (pixels == nullptr || w <= 0 || h <= 0) {
        return false;
    }
    out.resize(static_cast<size_t>(width) * height);
    for (int y = 0; y < height; ++y) {
        const int sy = y * h / height;
        for (int x = 0; x < width; ++x) {
            const int sx = x * w / width;
            out[y * width + x] = rgb565(pixels + (sy * w + sx) * 3);
        }
    }
    stbi_image_free(pixels);
    return true;
}

Result load(const Request &request)
{
    Result result;
    result.key = request.key;
    result.kind = request.kind;
    const int width = request.kind == Kind::Icon ? IconSize : CoverWidth;
    const int height = request.kind == Kind::Icon ? IconSize : CoverHeight;
    const std::string path = std::string(paths::ImageCache) + request.key + ".img";

    std::string data;
    if (fs::readFile(path, data) && decode(data, width, height, result.pixels)) {
        result.ok = true;
        return result;
    }
    if (request.url.empty() ||
        platform::wifiStatus() == platform::Wifi::Disconnected) {
        return result;
    }
    net::FetchRequest fetch;
    fetch.url = request.url;
    fetch.caFile = paths::CaBundle;
    fetch.userAgent = paths::UserAgent;
    fetch.maxBytes = 1024 * 1024;
    fetch.connectTimeoutSeconds = 10;
    fetch.totalTimeoutSeconds = 30;
    const net::FetchResult answer = net::fetch(fetch);
    if (!answer.ok || !decode(answer.body, width, height, result.pixels)) {
        logging::warn("image.failed", {{"url", request.url},
                                       {"status", static_cast<int>(answer.httpStatus)}});
        return result;
    }
    fs::makeDirectories(paths::ImageCache);
    fs::writeFile(path, answer.body);
    result.ok = true;
    return result;
}

void workerMain(void *)
{
    while (!stopping) {
        LightEvent_Wait(&wake);
        while (!stopping) {
            Request request;
            LightLock_Lock(&lock);
            const bool any = !requests.empty();
            if (any) {
                request = requests.front();
                requests.pop_front();
            }
            LightLock_Unlock(&lock);
            if (!any) {
                break;
            }
            Result result = load(request);
            LightLock_Lock(&lock);
            results.push_back(std::move(result));
            LightLock_Unlock(&lock);
        }
    }
}

void setupTextures()
{
    if (texturesReady) {
        return;
    }
    if (!C3D_TexInit(&atlas, AtlasSize, AtlasSize, GPU_RGB565)) {
        return;
    }
    C3D_TexSetFilter(&atlas, GPU_NEAREST, GPU_NEAREST);
    for (int slot = 0; slot < IconSlots; ++slot) {
        const float x = (slot % AtlasColumns) * IconSize;
        const float y = (slot / AtlasColumns) * IconSize;
        iconSubs[slot] = {IconSize, IconSize, x / AtlasSize, 1.0f - y / AtlasSize,
                          (x + IconSize) / AtlasSize,
                          1.0f - (y + IconSize) / AtlasSize};
        iconImages[slot] = {&atlas, &iconSubs[slot]};
    }
    coverSub = {CoverWidth, CoverHeight, 0.0f, 1.0f,
                static_cast<float>(CoverWidth) / CoverTexture,
                1.0f - static_cast<float>(CoverHeight) / CoverTexture};
    for (int slot = 0; slot < CoverSlots; ++slot) {
        if (!C3D_TexInit(&covers[slot], CoverTexture, CoverTexture, GPU_RGB565)) {
            C3D_TexDelete(&atlas);
            for (int other = 0; other < slot; ++other) {
                C3D_TexDelete(&covers[other]);
            }
            return;
        }
        C3D_TexSetFilter(&covers[slot], GPU_LINEAR, GPU_LINEAR);
        coverImages[slot] = {&covers[slot], &coverSub};
    }
    texturesReady = true;
}

// A posição livre, ou a usada há mais tempo (fora deste quadro).
int claimSlot(std::string *owners, int count)
{
    int best = -1;
    uint64_t oldest = UINT64_MAX;
    for (int slot = 0; slot < count; ++slot) {
        if (owners[slot].empty()) {
            return slot;
        }
        const auto found = entries.find(owners[slot]);
        const uint64_t used = found == entries.end() ? 0 : found->second.lastUsed;
        if (used < frame && used < oldest) {
            oldest = used;
            best = slot;
        }
    }
    if (best >= 0) {
        entries.erase(owners[best]);
        owners[best].clear();
    }
    return best;
}

void upload(Result &result)
{
    const auto found = entries.find(result.key);
    if (found == entries.end()) {
        return; // descartada enquanto carregava
    }
    Entry &entry = found->second;
    if (!result.ok || !texturesReady) {
        entry.state = State::Failed;
        return;
    }
    if (result.kind == Kind::Icon) {
        const int slot = claimSlot(iconOwner, IconSlots);
        if (slot < 0) {
            entries.erase(found); // tenta de novo quando houver espaço
            return;
        }
        uint16_t *texels = static_cast<uint16_t *>(atlas.data);
        const int baseX = (slot % AtlasColumns) * IconSize;
        const int baseY = (slot / AtlasColumns) * IconSize;
        for (int y = 0; y < IconSize; ++y) {
            for (int x = 0; x < IconSize; ++x) {
                texels[image::tiledIndex(baseX + x, baseY + y, AtlasSize)] =
                    result.pixels[y * IconSize + x];
            }
        }
        C3D_TexFlush(&atlas);
        iconOwner[slot] = result.key;
        entry.slot = slot;
    } else {
        const int slot = claimSlot(coverOwner, CoverSlots);
        if (slot < 0) {
            entries.erase(found);
            return;
        }
        uint16_t *texels = static_cast<uint16_t *>(covers[slot].data);
        for (int y = 0; y < CoverHeight; ++y) {
            for (int x = 0; x < CoverWidth; ++x) {
                texels[image::tiledIndex(x, y, CoverTexture)] =
                    result.pixels[y * CoverWidth + x];
            }
        }
        C3D_TexFlush(&covers[slot]);
        coverOwner[slot] = result.key;
        entry.slot = slot;
    }
    entry.state = State::Ready;
}

const C2D_Image *get(Kind kind, const store::Image &image,
                     const std::string &url)
{
    if (!started || url.empty()) {
        return nullptr;
    }
    const std::string key = keyFor(image, url);
    const auto found = entries.find(key);
    if (found != entries.end()) {
        Entry &entry = found->second;
        entry.lastUsed = frame;
        if (entry.state != State::Ready) {
            return nullptr;
        }
        return kind == Kind::Icon ? &iconImages[entry.slot]
                                  : &coverImages[entry.slot];
    }
    Entry entry;
    entry.kind = kind;
    entry.lastUsed = frame;
    entries[key] = entry;
    LightLock_Lock(&lock);
    // Capas passam na frente: só a do jogo selecionado é pedida.
    if (kind == Kind::Cover) {
        requests.push_front({key, url, kind});
    } else {
        requests.push_back({key, url, kind});
    }
    LightLock_Unlock(&lock);
    LightEvent_Signal(&wake);
    return nullptr;
}

} // namespace

void init()
{
    if (started) {
        return;
    }
    LightLock_Init(&lock);
    LightEvent_Init(&wake, RESET_ONESHOT);
    setupTextures();
    stopping = false;
    s32 priority = 0x30;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    worker = threadCreate(workerMain, nullptr, 128 * 1024,
                          std::min(0x3f, static_cast<int>(priority) + 1), -2,
                          false);
    started = worker != nullptr && texturesReady;
}

void shutdown()
{
    if (worker != nullptr) {
        stopping = true;
        LightEvent_Signal(&wake);
        threadJoin(worker, U64_MAX);
        threadFree(worker);
        worker = nullptr;
    }
    if (texturesReady) {
        C3D_TexDelete(&atlas);
        for (C3D_Tex &texture : covers) {
            C3D_TexDelete(&texture);
        }
        texturesReady = false;
    }
    entries.clear();
    started = false;
}

void update()
{
    ++frame;
    for (int count = 0; count < UploadsPerFrame; ++count) {
        Result result;
        LightLock_Lock(&lock);
        const bool any = !results.empty();
        if (any) {
            result = std::move(results.front());
            results.pop_front();
        }
        LightLock_Unlock(&lock);
        if (!any) {
            break;
        }
        upload(result);
    }
}

const C2D_Image *icon(const store::Image &image, const std::string &url)
{
    return get(Kind::Icon, image, url);
}

const C2D_Image *cover(const store::Image &image, const std::string &url)
{
    return get(Kind::Cover, image, url);
}

} // namespace imagecache
