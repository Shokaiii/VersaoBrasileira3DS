// Versão Brasileira — loja de traduções de fãs para Nintendo 3DS.
// Baseado no 3dsDL (github.com/ankushKun/3dsDL) e no Universal Updater
// (github.com/Universal-Team/Universal-Updater). Licença GPL-3.0.

#include "app.hpp"
#include "app_update.hpp"
#include "awake.hpp"
#include "crash.hpp"
#include "installer.hpp"
#include "led.hpp"
#include "logging.hpp"
#include "sound.hpp"
#include "ui.hpp"

#include <3ds.h>
#include <curl/curl.h>

#include <cstdio>
#include <cstdlib>
#include <malloc.h>
#include <string>

u32 __stacksize__ = 0x40000;

namespace {

constexpr size_t SocBufferSize = 0x100000;

std::string failure(const char *operation, Result result)
{
    char buffer[112];
    std::snprintf(buffer, sizeof(buffer), "%s falhou (0x%08lX).", operation,
                  static_cast<unsigned long>(result));
    return buffer;
}

struct Services {
    u32 *socBuffer = nullptr;
    bool romfs = false;
    bool soc = false;
    bool sslc = false;
    bool ac = false;
    bool curl = false;

    bool start(std::string &error)
    {
        socBuffer = static_cast<u32 *>(memalign(0x1000, SocBufferSize));
        if (socBuffer == nullptr) {
            error = "Não foi possível reservar memória para a rede.";
            return false;
        }

        Result result = romfsInit();
        if (R_FAILED(result)) {
            error = failure("Abrir os arquivos do aplicativo", result);
            return false;
        }
        romfs = true;

        result = socInit(socBuffer, SocBufferSize);
        if (R_FAILED(result)) {
            error = failure("Iniciar o serviço de rede", result);
            return false;
        }
        soc = true;

        // O Mbed TLS do devkitPro obtém aleatoriedade por
        // sslcGenerateRandomData; sem uma sessão SSLC aberta as conexões
        // seguras ficariam com chaves fracas.
        result = sslcInit(0);
        if (R_FAILED(result)) {
            error = failure("Iniciar a fonte aleatória segura", result);
            return false;
        }
        sslc = true;

        // Só para o indicador de Wi-Fi; não é fatal.
        ac = R_SUCCEEDED(acInit());
        // Serviço de instalação de CIA (etapa installCia); sem ele o resto
        // funciona.
        installer::init();

        // Sem o dump do firmware DSP não há sons, o que não é problema.
        sound::init();
        awake::init();
        led::init();

        const CURLcode code = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (code != CURLE_OK) {
            error = std::string("Iniciar o libcurl falhou: ") +
                    curl_easy_strerror(code);
            return false;
        }
        curl = true;
        return true;
    }

    ~Services()
    {
        led::exit();
        awake::exit();
        sound::exit();
        if (curl) {
            curl_global_cleanup();
        }
        installer::exit();
        if (ac) {
            acExit();
        }
        if (sslc) {
            sslcExit();
        }
        if (soc) {
            socExit();
        }
        if (romfs) {
            romfsExit();
        }
        std::free(socBuffer);
    }
};

} // namespace

int main(int argc, char **argv)
{
    appupdate::setLaunchPath(argc > 0 ? argv[0] : nullptr);
    crash::install(false);
    // Usa o clock e o cache L2 do New 3DS quando existem.
    osSetSpeedupEnable(true);
    gfxInitDefault();
    gfxSet3D(false);
    logging::start(false); // depois do gfx, por causa da memória linear

    if (!ui::init()) {
        gfxExit();
        return 1;
    }

    int status = 0;
    {
        Services services;
        std::string error;
        if (services.start(error)) {
            // Mantém o Wi-Fi ativo enquanto o app estiver aberto.
            const awake::Hold open;
            App app;
            app.run();
        } else {
            ui::message("Falha ao iniciar", error, ui::Tone::Error);
            status = 1;
        }
    }
    // Com os arquivos internos (romfs) já fechados, o .3dsx novo pode tomar
    // o lugar do atual.
    appupdate::applyPendingSwap();
    logging::finish();

    ui::shutdown();
    gfxExit();
    return status;
}
