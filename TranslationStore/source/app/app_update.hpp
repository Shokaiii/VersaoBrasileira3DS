#pragma once

#include "json.hpp"

#include <3ds.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

// Atualização do próprio aplicativo (Especificação 01, §2.1). O bloco "app"
// do version.json diz qual é a versão mais nova e onde baixá-la; o app
// escolhe o arquivo do formato em que está rodando (.3dsx pelo Homebrew
// Launcher ou .cia pelo menu HOME), baixa, confere o sha256 e só então
// substitui a versão atual.
namespace appupdate {

enum class Format { ThreeDsx, Cia };

struct Release {
    bool present = false;  // o version.json tem o bloco "app"
    std::string version;   // versão publicada
    bool required = false; // "required": "sim"
    std::vector<std::string> notes;
    std::string url;    // do formato em que o app está rodando
    std::string sha256; // idem
    std::string problem; // por que não dá para atualizar ("" se dá)
};

// Formato em que o app está rodando agora.
Format runningFormat();
const char *formatName(Format format);

// Caminho do .3dsx em execução (argv[0]), guardado por main().
void setLaunchPath(const char *path);

// Lê o bloco "app" do version.json. URLs relativas valem a partir da pasta
// do version.json.
Release parse(const json::Value &root, const std::string &versionUrl);
// A versão publicada é maior que a deste app e há arquivo para o formato.
bool available(const Release &release);

// Download e instalação numa thread própria.
enum class Phase { Downloading, Installing, Ready, Failed, Cancelled };

struct Progress {
    Phase phase = Phase::Downloading;
    uint64_t done = 0;
    uint64_t total = 0;
    double rate = 0;
    int64_t secondsLeft = -1;
    std::string message; // em caso de falha
    std::string detail;
};

class Updater {
public:
    Updater();
    ~Updater();
    Updater(const Updater &) = delete;
    Updater &operator=(const Updater &) = delete;

    void start(const Release &release);
    Progress progress() const;
    void cancel(); // só vale durante o download
    void join();

private:
    static void entry(void *self);
    void run();
    void publish(const Progress &value);

    mutable LightLock lock_;
    Progress progress_;
    Release release_;
    Thread thread_ = nullptr;
    std::atomic<bool> cancel_ {false};
};

// .3dsx: a troca do arquivo fica para depois que os arquivos internos do app
// (romfs) forem fechados, já que eles são lidos do próprio .3dsx. main()
// chama applyPendingSwap() no fim.
void applyPendingSwap();
// .cia: pede ao sistema para abrir o app de novo quando ele fechar.
void relaunchOnExit();

} // namespace appupdate
