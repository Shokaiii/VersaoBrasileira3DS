#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// As operações do Installation Pipeline (Especificação 01, §4.2). Cada uma
// roda na thread da fila, informa o progresso por Context e anota o que
// gravou, para que a instalação possa ser desfeita ou desinstalada.
namespace ops {

struct Context {
    std::function<bool()> cancelled;
    // done/total em bytes; rate em bytes/s (0 = desconhecida);
    // secondsLeft = -1 quando não se sabe.
    std::function<void(uint64_t done, uint64_t total, double rate,
                       int64_t secondsLeft)>
        progress;
    std::vector<std::string> *written = nullptr; // arquivos criados
    std::vector<std::string> *cias = nullptr;    // Title IDs instalados
};

struct Outcome {
    bool ok = false;
    bool cancelled = false;
    std::string message; // em português, para a tela
    std::string detail;  // detalhe técnico
};

void setConnections(int count);

// Valida caminhos e URLs antes de executar. Retornam "" se estiver certo, ou
// a explicação do problema.
// Gravar (download, extrair, copiar): a raiz do SD é permitida; o que fica
// dentro de áreas protegidas não.
std::string checkWritePath(const std::string &path);
// Apagar: além do acima, não pode ser a raiz nem pastas inteiras do sistema.
std::string checkDeletePath(const std::string &path);
std::string checkReadPath(const std::string &path);
std::string checkUrl(const std::string &url);

Outcome download(const std::string &url, const std::string &output,
                 const std::string &sha256, Context &context);
Outcome extract(const std::string &file, const std::string &output,
                const std::string &input, Context &context);
Outcome copy(const std::string &source, const std::string &destination,
             bool overwrite, Context &context);
Outcome remove(const std::string &path, bool missingOk);
Outcome installCia(const std::string &file, bool deleteAfter,
                   Context &context);
// Remove um título instalado no cartão SD. Só aceita jogos, atualizações,
// DLCs e demos (nunca títulos do sistema). missingOk: não estar instalado
// conta como sucesso.
Outcome uninstallCia(const std::string &titleIdHex, bool missingOk = true);
// "" se o Title ID puder ser desinstalado pelo app, senão o motivo.
std::string checkUninstallTitle(const std::string &titleIdHex);

// Apaga uma pasta e tudo dentro dela (para a pasta temporária).
void removeFolder(const std::string &folder);

} // namespace ops
