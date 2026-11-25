# Versão Brasileira — Especificação 01: Catálogo e Instalação

Status: v0.4 (2026-11-20)
Escopo: formato dos JSON (`version.json`, `catalog.json`), estrutura no cartão SD, fluxo de atualização do catálogo e do aplicativo, cache de imagens e o Installation Pipeline (interpretador de etapas).

Este documento é o **contrato** entre o Painel Administrativo (que gera os JSON) e o aplicativo 3DS (que os consome).

Alterações da v0.2: removidos `catalog_sha256`, `catalog_size` e `min_app_version` do version.json; removido `schema_version` do catalog.json; removidas as variáveis em caminhos (todos os caminhos são literais); removida a operação `verifyFile`.

Alterações da v0.3: nova operação `uninstallCia`; extrair/copiar para a raiz do SD é permitido, com conferência de cada arquivo do pacote; apagar pastas inteiras do sistema continua proibido.

Alterações da v0.4: bloco opcional `app` no version.json para atualizar o próprio aplicativo (§2.1), com atualização obrigatória ou opcional. O fluxo do catálogo não mudou.

---

## 1. Princípios

1. **O catálogo descreve, o app executa.** Nenhuma lógica específica de jogo fica no código do app; tudo vem das etapas no JSON.
2. **Nada é substituído sem validação.** Catálogo novo só substitui o antigo depois de baixado e interpretado com sucesso. O mesmo vale para o aplicativo: só é trocado depois de baixado e conferido pelo `sha256`.
3. **Toda escrita no SD é rastreada.** Cada instalação gera um manifesto com os arquivos criados, permitindo desinstalar e atualizar.
4. **Caminhos são literais e confinados.** Todo caminho no JSON é absoluto e começa com `sdmc:/`. O app rejeita `..`, caminhos fora das áreas permitidas e protocolos diferentes de `https://`/`http://` para downloads.

---

## 2. version.json

```json
{
  "catalog_version": "1.0.0",
  "catalog_url": "https://servidor.com/catalog.json",
  "published_at": "2026-11-17T23:00:00Z",
  "app": {
    "version": "0.1.15",
    "required": "nao",
    "notes": ["Ícones de região nos cartões"],
    "3dsx_url": "https://servidor.com/app/VersaoBrasileira.3dsx",
    "3dsx_sha256": "…",
    "cia_url": "https://servidor.com/app/VersaoBrasileira.cia",
    "cia_sha256": "…"
  }
}
```

| Campo | Obrigatório | Uso |
|---|---|---|
| `catalog_version` | sim | SemVer. Comparado numericamente (MAJOR.MINOR.PATCH), nunca como texto ("1.10.0" > "1.9.0"). |
| `catalog_url` | sim | URL absoluta do catálogo. |
| `published_at` | não | Exibido na tela de informações. |
| `app` | não | Atualização do aplicativo (§2.1). Sem ele, o app não oferece atualização. |

O progresso do download do catálogo usa o `Content-Length` informado pelo servidor.

### 2.1 Bloco `app` (atualização do aplicativo)

| Campo | Obrigatório | Uso |
|---|---|---|
| `version` | sim | SemVer da versão publicada (o `TranslationStore/VERSION`). O app só oferece se for **maior** que a dele; nunca volta para uma versão mais antiga. |
| `required` | não | `"sim"`: o app só abre depois de atualizar (opções Atualizar / Fechar). `"nao"` (padrão): opções Atualizar / Abrir sem atualizar. Também aceita `true`/`false`. |
| `notes` | não | Lista de novidades mostrada na janela (até 4). |
| `3dsx_url`, `3dsx_sha256` | para .3dsx | Arquivo para quem abre pelo Homebrew Launcher. |
| `cia_url`, `cia_sha256` | para .cia | Arquivo para quem abre pelo menu HOME. |

URLs relativas valem a partir da pasta do version.json. O `sha256` é obrigatório; sem o arquivo do formato em uso (ou sem `sha256`), o app não oferece a atualização e registra o motivo no log.

Fluxo no 3DS:
1. Ao abrir, o app sempre consulta o version.json (mesmo com "Verificar ao Iniciar" desligado — essa opção vale só para o catálogo).
2. Se há versão nova do app, a janela aparece **antes** do catálogo. O catálogo é verificado depois, se o app abrir.
3. O app identifica o formato em que está rodando (`envIsHomebrew()`): `.3dsx` ou `.cia`.
4. Baixa para `tmp/app/` e confere o `sha256`. Falha ou cancelamento não altera nada.
5. `.3dsx`: o app fecha; depois que os arquivos internos (romfs) são fechados, o `.3dsx` em execução (`argv[0]`) vira `.3dsx.bak` e o novo toma o lugar dele. O usuário abre de novo pelo Homebrew Launcher.
6. `.cia`: instala por cima (AM) e o app fecha e abre sozinho na nova versão.
7. Sem conexão: o app abre normalmente, mesmo com atualização obrigatória publicada.

A versão do título dentro do `.cia` é `MAJOR*16384 + MINOR*1024 + PATCH` (limite 3.15.1023), para caber nos 16 bits do formato e sempre crescer.

---

## 3. catalog.json

### 3.1 Estrutura geral

```json
{
  "catalog_version": "1.0.0",
  "generated_at": "2026-11-17T23:00:00Z",
  "base_url": "https://servidor.com/",
  "games": [ /* Game */ ]
}
```

`base_url` permite que imagens e arquivos usem URLs relativas (`images/pokemon-x/icon.png`). URLs absolutas também são aceitas. (Isso vale só para URLs de download; caminhos no SD são sempre literais.)

### 3.2 Game

```json
{
  "id": "pokemon-x",
  "name": "Pokémon X",
  "platform": "3ds",
  "description": "RPG de captura…",
  "publisher": "Nintendo",
  "release_year": 2013,
  "genres": ["RPG"],
  "title_ids": [
    { "region": "USA", "id": "0004000000055D00" },
    { "region": "EUR", "id": "0004000000055D00" }
  ],
  "images": {
    "icon":  { "url": "images/pokemon-x/icon.png",  "sha256": "…", "width": 48,  "height": 48 },
    "cover": { "url": "images/pokemon-x/cover.jpg", "sha256": "…", "width": 192, "height": 128 }
  },
  "translations": [ /* Translation */ ]
}
```

- `id`: slug único e estável (`[a-z0-9-]`, até 64 caracteres). Nunca muda depois de publicado — é a chave de cache, de manifesto e de “instalado”.
- `platform`: `3ds`, `nds`, `dsiware`, `gba`, `virtual-console`… (lista fechada definida no JSON Schema do painel).
- `title_ids`: 16 caracteres hexadecimais maiúsculos. Informativo (exibição e detecção de “jogo instalado”). Pode ser vazio.
- `images.*.sha256`: usado como nome no cache, para invalidar a cópia local quando a imagem muda (ver §7).

### 3.3 Translation

```json
{
  "id": "pokemon-x-ptbr",
  "name": "Pokémon X — Tradução PT-BR",
  "language": "pt-BR",
  "version": "1.2.0",
  "release_date": "2026-10-01",
  "authors": [
    { "name": "Fulano", "role": "Tradução" },
    { "name": "Ciclano", "role": "Revisão" }
  ],
  "description": "Tradução completa de textos e gráficos.",
  "changelog": [
    { "version": "1.2.0", "notes": "Correções de diálogo." }
  ],
  "compatibility": {
    "regions": ["USA"],
    "game_versions": ["1.5"],
    "consoles": "all",
    "requirements": ["luma3ds"],
    "notes": "Requer atualização 1.5 do jogo instalada."
  },
  "download_size": 15728640,
  "install_size": 41943040,
  "install": { "steps": [ /* Step */ ] },
  "uninstall": { "steps": [ /* Step, opcional */ ] }
}
```

- `consoles`: `all` ou `new3ds` (para traduções que só funcionam no New 3DS / New 2DS XL).
- `requirements`: identificadores mostrados como avisos (`luma3ds`, `layeredfs`, `twilightmenu`…). Não bloqueiam a instalação na v1.
- `install_size` permite checar espaço livre no SD antes de começar.
- `uninstall` é opcional: sem ele, o app desinstala apagando os arquivos registrados no manifesto (ver §6).

**Regiões com Title IDs diferentes:** como não há variáveis, os caminhos levam o Title ID escrito por extenso. Se uma tradução atende regiões com Title IDs diferentes, o painel cadastra uma tradução por região (ex.: `jogo-ptbr-usa` e `jogo-ptbr-eur`), cada uma com suas etapas.

Caminho padrão do LayeredFS no Luma3DS: `sdmc:/luma/titles/<TitleID>/romfs/` e `sdmc:/luma/titles/<TitleID>/code.bin` (ou `code.ips`).

Pasta temporária recomendada para downloads: `sdmc:/3ds/VersaoBrasileira/tmp/<translation-id>/`.

---

## 4. Installation Pipeline

### 4.1 Formato de etapa

Toda etapa tem `type` e campos opcionais comuns:

```json
{ "type": "downloadFile", "label": "Baixando patch", "optional": false, ... }
```

- `label`: texto mostrado na tela de progresso. Se ausente, o app gera um texto padrão por tipo.
- `optional`: se `true`, falha nessa etapa não interrompe a instalação (registrada como “ignorada”).

### 4.2 Operações (v1)

Nomes e parâmetros seguem o padrão do Universal Updater.

| type | Parâmetros | Extras opcionais |
|---|---|---|
| `downloadFile` | `file` (URL), `output` (caminho) | `sha256` (confere o arquivo ao terminar o download) |
| `extractFile` | `file` (arquivo compactado), `output` (pasta) | `input` (subpasta/padrão dentro do arquivo a extrair) |
| `copyFile` | `source`, `destination` | `overwrite` (padrão `true`) |
| `deleteFile` | `file` | `missing_ok` (padrão `true`) |
| `installCia` | `file` | `delete_after` (padrão `true`) |
| `uninstallCia` | `title_id` (16 dígitos hex) | `missing_ok` (padrão `true`: não estar instalado conta como sucesso) |

Regras gerais:
- Operações que escrevem criam as pastas-pai automaticamente.
- `copyFile` com origem terminada em `/` copia a pasta recursivamente.
- `extractFile` usa libarchive (zip, 7z, tar, tar.gz/bz2/xz/zst).
- `installCia` usa AM (`AM_StartCiaInstall`), com progresso por bytes escritos.
- `uninstallCia` usa `AM_DeleteTitle` na mídia SD e só aceita Title IDs de jogo (`00040000`), demo (`00040002`), atualização (`0004000E`) e DLC (`0004008C`); títulos do sistema são recusados. O painel lê o Title ID de dentro do `.cia` enviado e sugere a etapa.
- Download é feito para `<output>.part` e renomeado ao concluir; com `Range` o app retoma downloads interrompidos no “Tentar novamente”.

### 4.3 Exemplos (os quatro casos do projeto)

**Exemplo 1 — LayeredFS**
```json
"steps": [
  { "type": "downloadFile", "file": "files/pokemon-x/ptbr-1.2.0.zip", "output": "sdmc:/3ds/VersaoBrasileira/tmp/pokemon-x-ptbr/patch.zip" },
  { "type": "extractFile", "file": "sdmc:/3ds/VersaoBrasileira/tmp/pokemon-x-ptbr/patch.zip", "output": "sdmc:/luma/titles/0004000000055D00/" }
]
```

**Exemplo 2 — CIA**
```json
"steps": [
  { "type": "downloadFile", "file": "files/jogo/patch.cia", "output": "sdmc:/3ds/VersaoBrasileira/tmp/jogo-ptbr/patch.cia" },
  { "type": "installCia", "file": "sdmc:/3ds/VersaoBrasileira/tmp/jogo-ptbr/patch.cia" }
]
```

**Exemplo 3 — Múltiplos arquivos**
```json
"steps": [
  { "type": "downloadFile", "file": "files/jogo/parte1.7z", "output": "sdmc:/3ds/VersaoBrasileira/tmp/jogo-ptbr/p1.7z", "sha256": "…" },
  { "type": "downloadFile", "file": "files/jogo/parte2.7z", "output": "sdmc:/3ds/VersaoBrasileira/tmp/jogo-ptbr/p2.7z", "sha256": "…" },
  { "type": "extractFile", "file": "sdmc:/3ds/VersaoBrasileira/tmp/jogo-ptbr/p1.7z", "output": "sdmc:/3ds/VersaoBrasileira/tmp/jogo-ptbr/x/" },
  { "type": "extractFile", "file": "sdmc:/3ds/VersaoBrasileira/tmp/jogo-ptbr/p2.7z", "output": "sdmc:/3ds/VersaoBrasileira/tmp/jogo-ptbr/x/" },
  { "type": "copyFile", "source": "sdmc:/3ds/VersaoBrasileira/tmp/jogo-ptbr/x/code.bin", "destination": "sdmc:/luma/titles/0004000000055D00/code.bin" },
  { "type": "copyFile", "source": "sdmc:/3ds/VersaoBrasileira/tmp/jogo-ptbr/x/romfs/", "destination": "sdmc:/luma/titles/0004000000055D00/romfs/" }
]
```
(A verificação do Exemplo 3 é feita pelo `sha256` opcional do `downloadFile`.)

**Exemplo 4 — Arquivos + CIA**
```json
"steps": [
  { "type": "downloadFile", "file": "files/jogo/dados.zip", "output": "sdmc:/3ds/VersaoBrasileira/tmp/jogo-ptbr/dados.zip" },
  { "type": "extractFile", "file": "sdmc:/3ds/VersaoBrasileira/tmp/jogo-ptbr/dados.zip", "output": "sdmc:/luma/titles/0004000000055D00/" },
  { "type": "downloadFile", "file": "files/jogo/dlc.cia", "output": "sdmc:/3ds/VersaoBrasileira/tmp/jogo-ptbr/dlc.cia" },
  { "type": "installCia", "file": "sdmc:/3ds/VersaoBrasileira/tmp/jogo-ptbr/dlc.cia" }
]
```

### 4.4 Validação antes de executar

Antes da primeira etapa, o app valida o pipeline inteiro e recusa com mensagem clara se:
- `type` desconhecido (catálogo feito para uma versão mais nova do app): a tradução aparece como “requer atualização do aplicativo”;
- parâmetro obrigatório ausente;
- caminho que não começa com `sdmc:/`, contém `..`, ou aponta para áreas protegidas (`sdmc:/Nintendo 3DS/`, `sdmc:/boot.firm`, `sdmc:/boot.3dsx`, `sdmc:/luma/config.bin`, `config.json`, `favorites.json`, `catalog/` e `installed/` do app);
- gravar na raiz (`sdmc:/`), em `sdmc:/luma/` ou `sdmc:/3ds/` é permitido para extrair e copiar, mas antes de extrair o app confere o destino de cada arquivo do pacote e recusa o pacote inteiro se algum cair numa área protegida;
- `deleteFile` não pode apagar `sdmc:/`, `sdmc:/luma/`, `sdmc:/luma/titles/` nem `sdmc:/3ds/` inteiras;
- espaço livre menor que `download_size + install_size`;
- `consoles: new3ds` em um Old 3DS (aviso, não bloqueio).

### 4.5 Estados, progresso e eventos

O instalador roda em uma thread própria e **não conhece a UI**. Ele publica um estado compartilhado (protegido por `LightLock`) que a tela lê a cada quadro:

```cpp
enum class StepState { Pending, Running, Done, Skipped, Failed, Cancelled };

struct StepStatus {
    std::string label;
    StepState   state = StepState::Pending;
    uint64_t    done = 0, total = 0;   // bytes ou itens
    std::string error;                  // vazio se ok
};

struct InstallStatus {
    std::vector<StepStatus> steps;
    int  current = -1;
    bool finished = false;
    bool success = false;
};
```

Interface do instalador (rascunho):

```cpp
class Installer {
public:
    bool start(const Translation&);   // valida e cria a thread
    void cancel();                    // flag atômica; operações checam entre blocos
    bool retry();                     // recomeça a partir da etapa que falhou
    InstallStatus snapshot() const;   // cópia para a UI desenhar
};

class Operation {
public:
    virtual ~Operation() = default;
    virtual Result run(OpContext& ctx) = 0; // ctx: progresso, cancelamento, journal
};
```

Isso atende os requisitos da tela de instalação: etapas concluídas / em andamento / pendentes, progresso do download, mensagens de erro, cancelar e tentar novamente — sem acoplar a aparência.

### 4.6 Erros

Códigos padronizados, cada um com mensagem em português pronta para a UI:
`NETWORK_UNAVAILABLE`, `HTTP_ERROR(<código>)`, `CHECKSUM_MISMATCH`, `NO_SPACE`, `ARCHIVE_INVALID`, `FILE_NOT_FOUND`, `PATH_FORBIDDEN`, `CIA_INSTALL_FAILED(<result>)`, `CANCELLED`, `UNKNOWN_STEP`.

Todo erro também vai para `logs/install.log` com a etapa, os parâmetros e o código `Result` do libctru.

### 4.7 Limpeza

- Sucesso: apaga `sdmc:/3ds/VersaoBrasileira/tmp/<translation-id>/`.
- Falha/cancelamento: mantém a pasta temporária para permitir “Tentar novamente” com retomada; o app limpa pastas `tmp/` órfãs na próxima inicialização.

---

## 5. Atualização do catálogo (fluxo na inicialização)

1. Lê `catalog/catalog.json` local (se existir) e mostra a loja o mais cedo possível.
2. Em paralelo, baixa `version.json` (timeout curto). Se ele traz versão nova do aplicativo, a janela de atualização do app vem antes (§2.1).
3. Compara `catalog_version` remoto com o local (SemVer).
4. Se maior: baixa o catálogo para `tmp/catalog.json.part` → faz o parse completo → move o atual para `catalog.bak` → renomeia o novo para `catalog.json` → recarrega a loja.
5. Qualquer falha nos passos 2–4: mantém o catálogo atual e mostra um aviso discreto (“Sem conexão — usando catálogo de dd/mm”).
6. Sem catálogo local e sem rede: tela de erro com “Tentar novamente”.

---

## 6. Manifesto de instalação (gerenciamento de instalados)

Ao terminar uma instalação, o app grava `installed/<translation-id>.json`:

```json
{
  "translation_id": "pokemon-x-ptbr",
  "game_id": "pokemon-x",
  "version": "1.2.0",
  "installed_at": "2026-11-17T23:30:00Z",
  "files": [
    "sdmc:/luma/titles/0004000000055D00/romfs/a/0/1/2"
  ],
  "cias": ["0004000E00055D00"]
}
```

Usos:
- **Instaladas:** lista as traduções com manifesto.
- **Atualizações:** se `version` do catálogo > `version` do manifesto, mostra “Atualização disponível”.
- **Desinstalar:** executa `uninstall.steps` se existir (inclua `uninstallCia` para cada CIA); senão apaga os `files` do manifesto e desinstala os `cias` registrados.
- **Atualizar:** remove os arquivos da versão anterior e instala a nova, evitando arquivos antigos esquecidos no romfs.

---

## 7. Imagens e cache

- O catálogo guarda só `url`, `sha256`, `width`, `height`.
- Cache: `cache/images/<sha256-curto>.<ext>`. Usar o hash como nome invalida automaticamente quando a imagem muda.
- Lazy loading: a tela pede a imagem → cache em memória (LRU de texturas) → cache no SD → fila de download em segundo plano → placeholder até chegar.
- Restrições do 3DS: texturas da GPU são potências de 2 (máx. 1024). O painel exporta tamanhos fixos para evitar processamento pesado no console. Tamanhos definitivos: ver Especificação 02 (Sistema Visual).
- Limite de RAM: o Old 3DS tem pouca memória para apps; o LRU tem teto e descarrega as texturas que saíram da tela.

---

## 8. Estrutura no cartão SD

```
sdmc:/3ds/VersaoBrasileira/
├── VersaoBrasileira.3dsx
├── config.json          (preferências e URL do version.json)
├── catalog/
│   ├── version.json     (último version.json aplicado)
│   ├── catalog.json
│   └── catalog.bak
├── cache/images/
├── installed/           (manifestos)
├── tmp/                 (inclui tmp/app/, download da atualização do app)
└── logs/
```

A URL do `version.json` fica em `config.json` (com um padrão embutido no app), permitindo trocar de servidor sem recompilar.

No servidor, a atualização do app fica em `app/` ao lado do version.json (`app/VersaoBrasileira.3dsx` e `app/VersaoBrasileira.cia`), gerada pelo painel.

---

## 9. Pontos em aberto

1. Lista final de plataformas e de `requirements`.
2. Se o painel deve calcular `sha256` e tamanhos dos arquivos automaticamente (recomendado: sim, a partir do arquivo local antes de publicar).
3. Licenciamento: resolvido — 3dsDL e Universal Updater são GPL-3.0; o Versão Brasileira é GPL-3.0.
