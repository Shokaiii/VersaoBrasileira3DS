# Versão Brasileira — Especificação 02: Sistema Visual

Status: rascunho v0.1 (2026-11-17)
Base: 3dsDL 0.9.0 (github.com/ankushKun/3dsDL), estudado em `docs/DESIGN.md`, `source/app/ui.hpp`/`ui.cpp` e nas capturas do console.

---

## 1. O que o 3dsDL oferece

O 3dsDL é escrito em **C++ (gnu++17)**, licença **GPL-3.0** — compatível com o Versão Brasileira, que também será GPL-3.0 (o Universal Updater é GPL-3.0). Mantemos os avisos de copyright dos arquivos reaproveitados e creditamos o projeto no “Sobre”.

O visual dele, chamado **System Look**, imita os aplicativos de sistema do 3DS (Configurações, Gerenciamento de Dados): telas cáqui, botões creme em relevo, abas cinza nos cantos inferiores, diálogos de sistema e o anel verde do D-pad. Usa a **fonte do sistema** (suporta acentos do português), o que faz o app parecer nativo.

Todo o desenho passa por um único kit, `source/app/ui.cpp` (~4100 linhas), sobre Citro2D. Ele não usa proctex (funciona igual no emulador Azahar e no console) e desenha formas como quads texturizados a partir de um atlas.

### 1.1 Peças reaproveitáveis

| Área | Arquivos do 3dsDL | Uso no Versão Brasileira |
|---|---|---|
| Kit de UI | `app/ui.hpp`, `app/ui.cpp` | Base de todas as telas |
| Ícones | `app/icon_atlas.hpp`, `scripts/make-ui-icons.py`, `romfs/ui/icons.png` | Atlas de ícones de interface (64/32/24/18 px), com ícones novos nossos |
| Texto | `core/text`, `core/units` | Quebra de linha, tamanhos (“4,30 MiB”), tempo restante |
| JSON | `core/json` | Leitor do `version.json` e `catalog.json` (sem exceções) |
| Rede | `net/fetch`, `net/download_engine`, `net/tls`, `romfs/cacert.pem` | `downloadFile` (HTTPS, retomada, até 4 conexões) |
| CIA | `core/cia`, `app/installer` | `installCia` (inclui seeds e instalação em streaming) |
| Arquivos | `core/zip`, `core/archives`, `core/fs_util`, `core/file_ops` | `extractFile`, `copyFile`, `deleteFile` |
| Imagens | `core/image` (tiling para textura PICA200), `stb/stb_image.h`, libjpeg-turbo | Ícones e capas do catálogo |
| Extras | `app/sound`, `app/led`, `app/awake` | Sons, LED de notificação, impedir modo de espera durante instalação |

Observação técnica: o 3dsDL compila com `-fno-exceptions -fno-rtti`. Seguimos o mesmo, então o código novo usa retornos de erro (não exceções) — coerente com o pipeline da Especificação 01.

---

## 2. Regras de design (herdadas e adaptadas)

1. **A tela superior mostra, a inferior age.** Título, capa e informações em cima; tudo que responde a toque ou botão fica embaixo.
2. **Poucos controles grandes.** Botões de 26 a 40 px, no máximo quatro ou cinco escolhas por tela.
3. **Voltar e Ação nos cantos inferiores.** Aba cinza à esquerda (B: Voltar/Cancelar) e à direita (ação principal, com a tecla indicada: X, Y ou START). A tela inicial tem uma barra única “START Fechar”.
4. **O anel verde é o D-pad.** Toque e botões funcionam em todo lugar; o anel marca onde o A atua e nunca vai para as abas.
5. **Perguntas são diálogos de sistema.** Ações destrutivas (desinstalar) têm botão vermelho.
6. **Fatos, não explicações.** Informações aparecem como linhas de etiqueta + valor (“Versão 1.2.0”, “Tamanho 15,0 MiB”). Frases só em perguntas, erros e avisos de perda de dados.
7. **Textos em português do Brasil**, com a mesma convenção do sistema em PT: “Instalar”, “Voltar”, “Aguarde...”. Rótulos em português são mais longos — conferir cada botão (o kit já faz texto rolar quando não cabe).

---

## 3. Identidade

Mantemos todos os tokens do System Look (fundos, painéis, botões, tipografia, medidas de layout) e trocamos só a **cor de destaque** (no 3dsDL, azul `#1C93C2`), usada no título de cada tela e nos ícones do app.

Proposta: **verde-bandeira escuro `#178A4A`** para títulos e ícones, com o **amarelo** já existente das barras de progresso e do botão escolhido. O anel de foco continua verde-claro `#3CDC78` (é o padrão do sistema); o verde escuro do título é distinto o bastante dele. Alternativa, se preferir evitar dois verdes: azul-bandeira `#1F4E9C`.

Sem logotipos, personagens ou artes da Nintendo, assim como o 3dsDL. Ícone e banner do app serão desenhos próprios.

---

## 4. Medidas de referência (pixels do console)

- Tela superior 400×240; inferior 320×240.
- Barra de status: y 0–17 (Wi-Fi, conexão, espaço livre no SD, data/hora, bateria).
- Linha de título: y 24–50, ícone + título centralizados, na cor de destaque.
- Divisória pontilhada: y 57.
- Painel de informações: x 17–383, y 69–225.
- Conteúdo inferior: y 6–202. Abas: y 209–239, 126 px de largura.
- Listas: linhas de 27 px a cada 31 px (seis por página), ícone de 17 px.
- Diálogo: x 10, y 12, 300×216.

---

## 5. Imagens do catálogo

| Imagem | Tamanho | Formato | Onde aparece |
|---|---|---|---|
| Ícone do jogo | **48×48** | PNG | Grade/lista do catálogo, telas de detalhe e instalação (mesmo tamanho dos ícones do sistema; usa a moldura de `drawTitleIcon`) |
| Capa | **112×128** | JPEG | Painel da tela superior do jogo selecionado |

- O painel gera exatamente esses tamanhos (recorte + redimensionamento). O app não redimensiona: imagem com tamanho diferente é recusada e mostra o ícone padrão.
- Ambos cabem em texturas potência de 2 (64×64 e 128×128) sem desperdício grande.

### 5.1 Diferença importante em relação ao 3dsDL

O 3dsDL mostra **um** ícone e **uma** imagem por vez (uma textura compartilhada para cada). Uma loja precisa de muitos ícones ao mesmo tempo. Componente novo:

- **`IconAtlas`**: uma textura 512×512 RGB565 dividida em 100 células de 48×48 (10×10, com margem), com substituição LRU. Uma página da grade nunca passa de ~15 ícones, então 100 células sobram para as páginas vizinhas sem baixar de novo. Memória: 512 KiB.
- **`CoverSlot`**: 2 texturas 128×128 (capa atual e a próxima, para trocar sem piscar). Memória: 64 KiB.
- Carregamento: SD → decodificação (stb_image/libjpeg-turbo) em thread separada → upload para a célula no thread principal. Enquanto isso, mostra um ícone de espera.

---

## 6. Componentes

### 6.1 Reaproveitados do kit (sem mudanças de API)

Fundos, barra de status, linha de título (`drawTopChrome`), painel (`drawPanel`), botões e blocos de menu (`drawButton`, `drawTile`), anel de foco, abas (`drawTab`), etiquetas e selos (`drawTag`, `drawBadge`), linhas de fatos (`drawFacts`), barra de progresso, spinner, diálogos (`choose`, `message`), telas de botões (`runScreen`), menus, folhas de opções (`runSheet`), listas com rolagem e paginação (`runList`), `Scroller`, texto rolante (`drawMarquee`), notificações.

### 6.2 Novos

| Componente | Função |
|---|---|
| `GameGrid` | Grade de ícones 48×48 na tela inferior (5 colunas × 3 linhas por página), com anel de foco, selos no canto (“Instalada”, “Atualização”) e contador de página. Alternativa à lista. |
| `GameRow` | Linha de lista mais alta (ícone 48 px, nome, plataforma, selo) para os modos lista/pesquisa/instaladas. |
| `CoverPanel` | Tela superior do jogo selecionado: capa 112×128 à esquerda, nome e fatos (Plataforma, Traduções, Versão, Status) à direita. |
| `StepList` | Lista das etapas de instalação com estado por etapa (pendente, em andamento, concluída, ignorada, falhou) — ícone e cor por estado, barra na etapa atual. Lê o `InstallStatus` da Especificação 01. |
| `SearchField` | Campo de pesquisa que abre o teclado do sistema (`swkbd`) e mostra o termo e a contagem de resultados. |
| `IconAtlas`, `CoverSlot` | Cache de texturas (§5.1). |

Ícones novos no atlas de interface: Catálogo, Pesquisa, Instaladas, Atualizar, Tradução/idioma, Autores, Plataforma, Desinstalar.

### 6.3 Estados por cor

| Estado | Cor | Uso |
|---|---|---|
| Instalada | verde `#3FA65A` | Selo na grade e etiqueta no detalhe |
| Atualização disponível | laranja `#E69614` | Selo e etiqueta |
| Requer app mais novo / incompatível | cinza `#ACA27A` | Selo; botão Instalar desabilitado |
| Erro | vermelho `#E0301A` | Etapa que falhou, diálogo de erro |

---

## 7. Movimento e som

Herdados do 3dsDL: anel de foco desliza em 80 ms e “respira”; diálogos com fade 120 ms; telas com cross-fade 100 ms; barras de progresso suavizadas; sons sintetizados (tique ao mover, bipes de escolher/voltar, tom de erro), com opção de desligar. LED de notificação durante instalações com a tampa fechada.

---

## 8. Próximos passos

1. Definir a cor de destaque e o modo principal do catálogo (grade ou lista).
2. Mapear todas as telas (superior/inferior) — Especificação 03.
3. Montar o esqueleto do projeto com o kit de UI do 3dsDL compilando e uma tela inicial no emulador Azahar.
4. Calibrar no console real (Old 3DS e New 3DS): 60 fps, tamanhos de texto, anel de foco.
