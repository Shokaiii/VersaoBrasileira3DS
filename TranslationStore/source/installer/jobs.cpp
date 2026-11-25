#include "jobs.hpp"

#include "fs_util.hpp"
#include "installed.hpp"
#include "logging.hpp"
#include "operations.hpp"
#include "paths.hpp"
#include "platform.hpp"

#include <3ds.h>

#include <algorithm>
#include <atomic>
#include <ctime>
#include <functional>
#include <list>
#include <memory>

namespace jobs {
namespace {

// Etapas internas, além das do catálogo.
enum class Internal { None, RemoveOld, RemoveManifestFiles, RemoveCias };

struct ExecStep {
    Internal internal = Internal::None;
    store::Step step;
};

struct Job {
    JobView view;
    std::vector<ExecStep> steps;
    int resumeFrom = 0;
    std::vector<std::string> written;
    std::vector<std::string> cias;
    installed::Manifest previous; // da versão anterior (atualizar/desinstalar)
    bool hasPrevious = false;
    uint64_t neededBytes = 0;
    std::string temporary;
    std::atomic<bool> cancel {false};
};

LightLock lock;
LightEvent wake;
std::list<std::unique_ptr<Job>> queue;
std::vector<Notice> notices;
Thread worker = nullptr;
std::atomic<bool> stopping {false};
int nextId = 1;
bool started = false;

std::string defaultLabel(const store::Step &step)
{
    const std::string &file = step.file;
    const std::string name = file.substr(file.find_last_of('/') + 1);
    switch (step.type) {
    case store::StepType::DownloadFile:
        return "Baixando " + step.output.substr(step.output.find_last_of('/') + 1);
    case store::StepType::ExtractFile:
        return "Extraindo " + name;
    case store::StepType::CopyFile:
        return "Copiando arquivos";
    case store::StepType::DeleteFile:
        return "Apagando " + name;
    case store::StepType::InstallCia:
        return "Instalando " + name;
    case store::StepType::UninstallCia:
        return "Desinstalando CIA " + step.titleId;
    case store::StepType::Unknown:
        break;
    }
    return step.typeName;
}

std::string validate(const store::Catalog &catalog, store::Step &step)
{
    switch (step.type) {
    case store::StepType::DownloadFile:
        step.file = catalog.resolveUrl(step.file);
        if (!ops::checkUrl(step.file).empty()) {
            return ops::checkUrl(step.file);
        }
        return ops::checkWritePath(step.output);
    case store::StepType::ExtractFile: {
        const std::string read = ops::checkReadPath(step.file);
        return read.empty() ? ops::checkWritePath(step.output) : read;
    }
    case store::StepType::CopyFile: {
        const std::string read = ops::checkReadPath(step.source);
        return read.empty() ? ops::checkWritePath(step.destination) : read;
    }
    case store::StepType::DeleteFile:
        return ops::checkDeletePath(step.file);
    case store::StepType::InstallCia:
        return ops::checkReadPath(step.file);
    case store::StepType::UninstallCia:
        return ops::checkUninstallTitle(step.titleId);
    case store::StepType::Unknown:
        break;
    }
    return "Etapa desconhecida: " + step.typeName +
           ". Atualize o aplicativo.";
}

void addStep(Job &job, Internal internal, const std::string &label)
{
    ExecStep exec;
    exec.internal = internal;
    job.steps.push_back(exec);
    StepView view;
    view.label = label;
    job.view.steps.push_back(view);
}

bool addCatalogSteps(Job &job, const store::Catalog *catalog,
                     const std::vector<store::Step> &source, std::string &error)
{
    static const store::Catalog empty;
    for (store::Step step : source) {
        const std::string problem = validate(catalog ? *catalog : empty, step);
        if (!problem.empty()) {
            error = problem;
            return false;
        }
        ExecStep exec;
        exec.step = step;
        job.steps.push_back(exec);
        StepView view;
        view.label = step.label.empty() ? defaultLabel(step) : step.label;
        view.download = step.type == store::StepType::DownloadFile;
        job.view.steps.push_back(view);
    }
    return true;
}

std::string nowIso()
{
    const time_t now = std::time(nullptr);
    struct tm parts;
    gmtime_r(&now, &parts);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &parts);
    return buffer;
}

void publish(Job &job, const std::function<void(JobView &)> &change)
{
    LightLock_Lock(&lock);
    change(job.view);
    LightLock_Unlock(&lock);
}

void pushNotice(bool ok, const std::string &title, const std::string &text)
{
    LightLock_Lock(&lock);
    notices.push_back({ok, title, text});
    LightLock_Unlock(&lock);
}

// Desfaz o que a instalação cancelada já gravou.
void rollback(Job &job)
{
    for (auto it = job.written.rbegin(); it != job.written.rend(); ++it) {
        fs::removeFile(*it);
    }
    job.written.clear();
    ops::removeFolder(job.temporary);
}

ops::Outcome runStep(Job &job, int index)
{
    ops::Context context;
    context.cancelled = [&job]() { return job.cancel.load() || stopping.load(); };
    context.progress = [&job, index](uint64_t done, uint64_t total, double rate,
                                     int64_t left) {
        publish(job, [&](JobView &view) {
            StepView &step = view.steps[index];
            step.done = done;
            step.total = total;
            step.rate = rate;
            step.secondsLeft = left;
        });
    };
    context.written = &job.written;
    context.cias = &job.cias;

    const ExecStep &exec = job.steps[index];
    switch (exec.internal) {
    case Internal::RemoveOld:
    case Internal::RemoveManifestFiles: {
        const size_t total = job.previous.files.size();
        size_t done = 0;
        for (const std::string &file : job.previous.files) {
            // Só apaga arquivos dentro do SD e fora das áreas protegidas.
            if (ops::checkWritePath(file).empty()) {
                fs::removeFile(file);
            }
            context.progress(++done, total, 0, -1);
        }
        ops::Outcome ok;
        ok.ok = true;
        return ok;
    }
    case Internal::RemoveCias: {
        for (const std::string &title : job.previous.cias) {
            const ops::Outcome result = ops::uninstallCia(title);
            if (!result.ok) {
                return result;
            }
        }
        ops::Outcome ok;
        ok.ok = true;
        return ok;
    }
    case Internal::None:
        break;
    }

    const store::Step &step = exec.step;
    switch (step.type) {
    case store::StepType::DownloadFile:
        return ops::download(step.file, step.output, step.sha256, context);
    case store::StepType::ExtractFile:
        return ops::extract(step.file, step.output, step.input, context);
    case store::StepType::CopyFile:
        return ops::copy(step.source, step.destination, step.overwrite, context);
    case store::StepType::DeleteFile:
        return ops::remove(step.file, step.missingOk);
    case store::StepType::InstallCia:
        return ops::installCia(step.file, step.deleteAfter, context);
    case store::StepType::UninstallCia:
        return ops::uninstallCia(step.titleId, step.missingOk);
    case store::StepType::Unknown:
        break;
    }
    ops::Outcome unknown;
    unknown.message = "Etapa desconhecida: " + step.typeName;
    return unknown;
}

void finishSuccess(Job &job)
{
    if (job.view.kind == Kind::Uninstall) {
        installed::erase(job.view.translationId);
    } else {
        installed::Manifest manifest;
        manifest.translationId = job.view.translationId;
        manifest.gameId = job.view.gameId;
        manifest.gameName = job.view.gameName;
        manifest.translationName = job.view.translationName;
        manifest.version = job.view.version;
        manifest.installedAt = nowIso();
        manifest.iconUrl = job.view.iconUrl;
        manifest.iconSha256 = job.view.icon.sha256;
        // Arquivos temporários não fazem parte da instalação.
        for (const std::string &file : job.written) {
            if (file.rfind(job.temporary, 0) != 0) {
                manifest.files.push_back(file);
            }
        }
        manifest.cias = job.cias;
        installed::save(manifest);
    }
    ops::removeFolder(job.temporary);
    publish(job, [](JobView &view) {
        view.state = State::Done;
        view.current = -1;
    });
    const char *title = job.view.kind == Kind::Uninstall ? "Desinstalação concluída"
                        : job.view.kind == Kind::Update  ? "Atualização concluída"
                                                         : "Instalação concluída";
    pushNotice(true, title, job.view.gameName);
}

void run(Job &job)
{
    publish(job, [](JobView &view) {
        view.state = State::Running;
        view.error.clear();
        view.errorDetail.clear();
    });
    logging::info("job.start", {{"translation", job.view.translationId},
                                {"kind", kindName(job.view.kind)}});

    if (job.resumeFrom == 0 && job.neededBytes > 0) {
        const int64_t free = platform::sdFreeBytes();
        if (free >= 0 && static_cast<uint64_t>(free) < job.neededBytes) {
            publish(job, [](JobView &view) {
                view.state = State::Failed;
                view.error = "Espaço insuficiente no cartão SD.";
            });
            pushNotice(false, "Falha na instalação", job.view.gameName);
            return;
        }
    }

    const int count = static_cast<int>(job.steps.size());
    for (int index = job.resumeFrom; index < count; ++index) {
        publish(job, [index](JobView &view) {
            view.current = index;
            view.steps[index].state = StepState::Running;
            view.steps[index].done = 0;
            view.steps[index].total = 0;
        });
        const ops::Outcome result = runStep(job, index);

        if (result.cancelled || job.cancel.load()) {
            rollback(job);
            publish(job, [index](JobView &view) {
                view.steps[index].state = StepState::Cancelled;
                view.state = State::Cancelled;
                view.current = -1;
            });
            pushNotice(false, "Processo cancelado", job.view.gameName);
            return;
        }
        if (!result.ok) {
            const bool optional = job.steps[index].internal == Internal::None &&
                                  job.steps[index].step.optional;
            if (optional) {
                publish(job, [index](JobView &view) {
                    view.steps[index].state = StepState::Skipped;
                });
                continue;
            }
            job.resumeFrom = index;
            publish(job, [&](JobView &view) {
                view.steps[index].state = StepState::Failed;
                view.state = State::Failed;
                view.error = result.message;
                view.errorDetail = result.detail;
            });
            logging::warn("job.failed", {{"translation", job.view.translationId},
                                         {"step", std::to_string(index + 1)},
                                         {"error", result.message},
                                         {"detail", result.detail}});
            pushNotice(false,
                       job.view.kind == Kind::Uninstall ? "Falha na desinstalação"
                                                        : "Falha na instalação",
                       job.view.gameName);
            return;
        }
        publish(job, [index](JobView &view) {
            view.steps[index].state = StepState::Done;
        });
    }
    finishSuccess(job);
}

Job *nextQueued()
{
    for (const std::unique_ptr<Job> &job : queue) {
        if (job->view.state == State::Queued) {
            return job.get();
        }
    }
    return nullptr;
}

void workerMain(void *)
{
    while (!stopping.load()) {
        LightLock_Lock(&lock);
        Job *job = nextQueued();
        if (job != nullptr) {
            job->view.state = State::Running;
        }
        LightLock_Unlock(&lock);
        if (job == nullptr) {
            LightEvent_Wait(&wake);
            continue;
        }
        run(*job);
    }
}

Job *find(int id)
{
    for (const std::unique_ptr<Job> &job : queue) {
        if (job->view.id == id) {
            return job.get();
        }
    }
    return nullptr;
}

int add(std::unique_ptr<Job> job)
{
    LightLock_Lock(&lock);
    job->view.id = nextId++;
    const int id = job->view.id;
    queue.push_back(std::move(job));
    LightLock_Unlock(&lock);
    LightEvent_Signal(&wake);
    return id;
}

} // namespace

float JobView::overall() const
{
    if (state == State::Done) {
        return 1.0f;
    }
    if (steps.empty()) {
        return 0.0f;
    }
    float done = 0;
    for (const StepView &step : steps) {
        if (step.state == StepState::Done || step.state == StepState::Skipped) {
            done += 1;
        } else if (step.state == StepState::Running && step.total > 0) {
            done += static_cast<float>(step.done) / step.total;
        }
    }
    return std::min(1.0f, done / steps.size());
}

bool JobView::cancellable() const
{
    return state == State::Running && current >= 0 &&
           current < static_cast<int>(steps.size()) && steps[current].download;
}

void init()
{
    if (started) {
        return;
    }
    LightLock_Init(&lock);
    LightEvent_Init(&wake, RESET_ONESHOT);
    stopping.store(false);
    s32 priority = 0x30;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    worker = threadCreate(workerMain, nullptr, 128 * 1024,
                          std::min(0x3f, static_cast<int>(priority) + 1), -2,
                          false);
    started = worker != nullptr;
}

void shutdown()
{
    if (worker != nullptr) {
        stopping.store(true);
        LightLock_Lock(&lock);
        for (const std::unique_ptr<Job> &job : queue) {
            job->cancel.store(true);
        }
        LightLock_Unlock(&lock);
        LightEvent_Signal(&wake);
        threadJoin(worker, U64_MAX);
        threadFree(worker);
        worker = nullptr;
    }
    queue.clear();
    started = false;
}

int enqueue(const store::Catalog &catalog, const store::Game &game,
            const store::Translation &translation, Kind kind,
            std::string &error)
{
    if (!started) {
        error = "A fila não está disponível.";
        return -1;
    }
    if (pending(translation.id)) {
        error = "Esta tradução já está na fila.";
        return -1;
    }
    std::unique_ptr<Job> job(new Job());
    job->view.kind = kind;
    job->view.gameId = game.id;
    job->view.gameName = game.name;
    job->view.translationId = translation.id;
    job->view.translationName = translation.name;
    job->view.version = translation.version;
    job->view.icon = game.icon;
    job->view.iconUrl = game.icon.empty() ? "" : catalog.resolveUrl(game.icon.url);
    job->temporary = std::string(paths::Temporary) + translation.id + "/";
    job->neededBytes = translation.downloadSize + translation.installSize;

    if (!addCatalogSteps(*job, &catalog, translation.install, error)) {
        return -1;
    }
    // Atualizar ou reinstalar: os arquivos da versão anterior são apagados
    // depois dos downloads (se um download falhar, a versão antiga continua
    // instalada) e antes da primeira etapa que grava no destino.
    job->hasPrevious = installed::load(translation.id, job->previous);
    if (job->hasPrevious && !job->previous.files.empty()) {
        size_t at = 0;
        while (at < job->steps.size() &&
               job->steps[at].step.type == store::StepType::DownloadFile) {
            ++at;
        }
        ExecStep exec;
        exec.internal = Internal::RemoveOld;
        StepView view;
        view.label = "Removendo versão anterior";
        job->steps.insert(job->steps.begin() + at, exec);
        job->view.steps.insert(job->view.steps.begin() + at, view);
    }
    return add(std::move(job));
}

int enqueueUninstall(const std::string &translationId,
                     const store::Translation *translation, std::string &error)
{
    if (!started) {
        error = "A fila não está disponível.";
        return -1;
    }
    if (pending(translationId)) {
        error = "Esta tradução já está na fila.";
        return -1;
    }
    std::unique_ptr<Job> job(new Job());
    if (!installed::load(translationId, job->previous)) {
        error = "Esta tradução não está instalada.";
        return -1;
    }
    job->hasPrevious = true;
    job->view.kind = Kind::Uninstall;
    job->view.gameId = job->previous.gameId;
    job->view.gameName = job->previous.gameName;
    job->view.translationId = translationId;
    job->view.translationName = job->previous.translationName;
    job->view.version = job->previous.version;
    job->view.icon.url = job->previous.iconUrl;
    job->view.icon.sha256 = job->previous.iconSha256;
    job->view.iconUrl = job->previous.iconUrl;
    job->temporary = std::string(paths::Temporary) + translationId + "/";

    if (translation != nullptr && !translation->uninstall.empty()) {
        if (!addCatalogSteps(*job, nullptr, translation->uninstall, error)) {
            return -1;
        }
    } else {
        addStep(*job, Internal::RemoveManifestFiles, "Apagando arquivos da tradução");
        if (!job->previous.cias.empty()) {
            addStep(*job, Internal::RemoveCias, "Desinstalando CIA");
        }
    }
    return add(std::move(job));
}

std::vector<JobView> snapshot()
{
    std::vector<JobView> out;
    LightLock_Lock(&lock);
    for (const std::unique_ptr<Job> &job : queue) {
        out.push_back(job->view);
    }
    LightLock_Unlock(&lock);
    return out;
}

bool busy()
{
    return count() > 0;
}

int count()
{
    int total = 0;
    LightLock_Lock(&lock);
    for (const std::unique_ptr<Job> &job : queue) {
        if (job->view.state == State::Queued || job->view.state == State::Running) {
            ++total;
        }
    }
    LightLock_Unlock(&lock);
    return total;
}

bool pending(const std::string &translationId)
{
    bool found = false;
    LightLock_Lock(&lock);
    for (const std::unique_ptr<Job> &job : queue) {
        if (job->view.translationId == translationId &&
            (job->view.state == State::Queued || job->view.state == State::Running)) {
            found = true;
        }
    }
    LightLock_Unlock(&lock);
    return found;
}

bool anyFailed()
{
    bool found = false;
    LightLock_Lock(&lock);
    for (const std::unique_ptr<Job> &job : queue) {
        found = found || job->view.state == State::Failed;
    }
    LightLock_Unlock(&lock);
    return found;
}

void cancel(int id)
{
    LightLock_Lock(&lock);
    Job *job = find(id);
    if (job != nullptr && job->view.cancellable()) {
        job->cancel.store(true);
    }
    LightLock_Unlock(&lock);
}

void retry(int id)
{
    LightLock_Lock(&lock);
    Job *job = find(id);
    if (job != nullptr && job->view.state == State::Failed) {
        job->cancel.store(false);
        job->view.state = State::Queued;
        job->view.error.clear();
        job->view.errorDetail.clear();
        for (size_t index = job->resumeFrom; index < job->view.steps.size(); ++index) {
            job->view.steps[index].state = StepState::Pending;
        }
    }
    LightLock_Unlock(&lock);
    LightEvent_Signal(&wake);
}

void remove(int id)
{
    LightLock_Lock(&lock);
    for (auto it = queue.begin(); it != queue.end(); ++it) {
        if ((*it)->view.id == id && (*it)->view.state != State::Running) {
            if ((*it)->view.state == State::Failed) {
                rollback(**it); // falhou e foi descartada: limpa o que gravou
            }
            queue.erase(it);
            break;
        }
    }
    LightLock_Unlock(&lock);
}

void clearFinished()
{
    LightLock_Lock(&lock);
    queue.remove_if([](const std::unique_ptr<Job> &job) {
        return job->view.state == State::Done || job->view.state == State::Cancelled;
    });
    LightLock_Unlock(&lock);
}

std::vector<Notice> takeNotices()
{
    LightLock_Lock(&lock);
    std::vector<Notice> out;
    out.swap(notices);
    LightLock_Unlock(&lock);
    return out;
}

const char *kindName(Kind kind)
{
    switch (kind) {
    case Kind::Install:
        return "Instalação";
    case Kind::Update:
        return "Atualização";
    case Kind::Uninstall:
        break;
    }
    return "Desinstalação";
}

} // namespace jobs
