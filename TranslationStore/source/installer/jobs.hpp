#pragma once

#include "catalog.hpp"

#include <cstdint>
#include <string>
#include <vector>

// Fila de processos (Especificação 03, §7): instala, atualiza e desinstala
// traduções, uma por vez, numa thread própria. As telas só leem snapshot().
namespace jobs {

enum class Kind { Install, Update, Uninstall };
enum class State { Queued, Running, Done, Failed, Cancelled };
enum class StepState { Pending, Running, Done, Skipped, Failed, Cancelled };

struct StepView {
    std::string label;
    StepState state = StepState::Pending;
    bool download = false; // etapa downloadFile (a única que pode cancelar)
    uint64_t done = 0;
    uint64_t total = 0;
    double rate = 0;         // bytes/s, só no download
    int64_t secondsLeft = -1;
};

struct JobView {
    int id = 0;
    Kind kind = Kind::Install;
    State state = State::Queued;
    std::string gameId;
    std::string gameName;
    std::string translationId;
    std::string translationName;
    std::string version;
    store::Image icon;
    std::string iconUrl;
    std::vector<StepView> steps;
    int current = -1;
    std::string error;
    std::string errorDetail;

    // Fração concluída de 0 a 1 (etapas feitas + parte da atual).
    float overall() const;
    // Cancelar só vale durante um download.
    bool cancellable() const;
};

struct Notice {
    bool ok = false;
    std::string title;
    std::string text;
};

void init();
void shutdown();

// Coloca na fila. Retorna o id, ou -1 com error explicando o problema
// (etapas inválidas, caminho proibido...).
int enqueue(const store::Catalog &catalog, const store::Game &game,
            const store::Translation &translation, Kind kind,
            std::string &error);
// Desinstalação a partir do manifesto (a tradução pode ter saído do
// catálogo; translation pode ser nullptr).
int enqueueUninstall(const std::string &translationId,
                     const store::Translation *translation, std::string &error);

std::vector<JobView> snapshot();
bool busy();                                     // algo na fila ou rodando
bool pending(const std::string &translationId);  // na fila ou rodando
int count();                                     // na fila + rodando
bool anyFailed();

void cancel(int id);
void retry(int id);
void remove(int id);
void clearFinished();

// Avisos de processos que terminaram, para a tela mostrar.
std::vector<Notice> takeNotices();

const char *kindName(Kind kind);

} // namespace jobs
