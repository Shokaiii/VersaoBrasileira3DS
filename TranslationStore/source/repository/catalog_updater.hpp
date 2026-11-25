#pragma once

#include <3ds.h>

#include "app_update.hpp"

#include <atomic>
#include <string>

// Atualização do catálogo na abertura (Especificação 01, §5): consulta o
// version.json, compara com o catálogo local e, se o remoto for maior, baixa,
// valida e substitui o catalog.json do cartão SD. Roda numa thread própria;
// a tela só lê status().
namespace repository {

enum class UpdateState {
    Checking,    // consultando o version.json
    Downloading, // baixando o catálogo novo
    UpToDate,    // o local já é o mais recente
    Updated,     // catálogo novo salvo
    Offline,     // sem conexão
    Failed,      // erro ao consultar, baixar ou validar
    AppUpdate,   // há versão nova do aplicativo (o catálogo fica para depois)
};

// O que verificar: o catálogo segue a opção "Verificar ao abrir"; o
// aplicativo é verificado sempre.
struct CheckMode {
    bool catalog = true;
    bool app = true;
};

struct UpdateStatus {
    UpdateState state = UpdateState::Checking;
    bool finished = false;
    bool hasLocal = false;
    std::string localVersion;  // "" quando não há catálogo local
    std::string remoteVersion; // "" enquanto não se sabe
    std::string detail;        // detalhe técnico do erro
    appupdate::Release app;    // bloco "app" do version.json
};

// Versão do catálogo salvo no SD, ou "" se não houver um válido.
std::string localCatalogVersion();

class CatalogUpdater {
public:
    CatalogUpdater();
    ~CatalogUpdater();
    CatalogUpdater(const CatalogUpdater &) = delete;
    CatalogUpdater &operator=(const CatalogUpdater &) = delete;

    // Começa (ou recomeça, depois de terminar) a verificação.
    void start(const std::string &versionUrl, CheckMode mode = {});
    UpdateStatus status() const;
    // Pede para parar e espera a thread terminar.
    void stop();

private:
    static void entry(void *self);
    void run();
    void publish(const UpdateStatus &value);
    void join();

    mutable LightLock lock_;
    UpdateStatus status_;
    std::string url_;
    CheckMode mode_;
    Thread thread_ = nullptr;
    std::atomic<bool> cancel_ {false};
};

} // namespace repository
