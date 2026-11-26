# Versão Brasileira — Especificação 03: Telas

Status: rascunho v0.3 (2026-11-25)
Baseado na definição do usuário (8 telas) e no kit visual da Especificação 02. Medidas em pixels do console: superior 400×240, inferior 320×240.

Alterações da v0.3: detalhes do jogo em abas (Info, Tradução, Créditos), capa 3:2 (192×128), ações nas abas de baixo.

Alterações da v0.2: botões dedicados **Instalar** e **Favoritar** nos detalhes do jogo; favoritos confirmados por jogo; definição de versão do jogo × versão da tradução; cor de destaque confirmada.

---

## 0. Decisões gerais

1. **Inversão da regra do 3dsDL.** Nas telas de lista (Catálogo, Busca, Favoritos, Biblioteca, Processos) a **lista fica na tela superior** e os **detalhes e ações na inferior**. A tela superior não recebe toque, então a lista é navegada só pelo direcional e pelo Circle Pad; a tela inferior é tocável (rolar detalhes, botões).
2. **Ícone de região** em cada jogo (ver §3.2).
3. **Versões:**
   - **Versão do jogo** = versão em que o jogo precisa estar para a tradução funcionar. Vem de `compatibility.game_versions` da tradução (sem campo novo no jogo).
   - **Versão da tradução** = versão atual da própria tradução (`version` da tradução).
4. **Manifesto de instalação** (Espec. 01 §6) guarda também `game_name`, `translation_name` e o caminho do ícone em cache, para a Biblioteca funcionar mesmo se o jogo sair do catálogo.
5. **Desinstalação** entra na fila de Processos, como a instalação.
6. **Favoritos são por jogo.**
7. **Cor de destaque:** verde-bandeira escuro `#178A4A`.

---

## 1. Atualização do catálogo (abertura)

Primeira tela ao abrir o app.

**Superior**
- Ícone do app (64 px) e nome “Versão Brasileira” centralizados (y ≈ 40–120).
- Caixa de estado (painel cáqui, x 17–383) com uma das mensagens:
  - “Verificando atualizações do catálogo” (com spinner)
  - “Baixando novo catálogo” (com barra de progresso e tamanho)
  - “O catálogo local já está atualizado”
  - “Catálogo atualizado para a versão X”
  - “Sem conexão — usando o catálogo local”
  - “Não foi possível verificar o catálogo” (detalhe técnico em texto pequeno)

**Inferior**
- Linhas de fatos: **Catálogo local** `1.0.0` (ou “Nenhum”) e **Catálogo online** `1.1.0` (ou “—” enquanto verifica / “Indisponível”).
- Comportamento:
  - Sucesso ou já atualizado → vai para a Tela inicial sozinho após ~1 s.
  - Erro com catálogo local → botões **Tentar Novamente** e **Continuar** (usa o local).
  - Erro sem catálogo local → **Tentar Novamente** e **Fechar**.

### 1.1 Atualização do aplicativo

Quando o version.json traz versão nova do app (Especificação 01, §2.1), uma janela aparece antes do catálogo: versão instalada, versão nova e até 4 novidades. Botões **Atualizar** e **Abrir sem atualizar** (ou **Fechar**, se a atualização for obrigatória). Durante a atualização, a tela mostra a barra de progresso e **Cancelar**; em caso de falha, **Repetir**.

---

## 2. Tela inicial

**Superior**
- Barra de status (y 0–17): Wi-Fi, conexão, data/hora, bateria.
- Ícone grande do app e “Versão Brasileira” centralizados.
- Abaixo: **Catálogo local** e **Catálogo online** em linhas de fatos.

**Inferior**
- No alto, dois botões largos (148×30, ícone à esquerda do texto): **+Baixados** (troféu) e **Contato** (balão de conversa), em y 6.
- Abaixo, 6 blocos (`drawTile`) de 96×76 em 3 colunas × 2 linhas (y 42 e 126), com ícone de 30 px sobre o rótulo:
  - Linha 1: **Catálogo**, **Busca**, **Favoritos**
  - Linha 2: **Biblioteca**, **Fila**, **Ajustes**
- Selos sobre o canto superior direito do ícone do bloco: Biblioteca com a quantidade de atualizações disponíveis (laranja); Fila com a quantidade na fila (azul) ou um selo vermelho se algo falhou.
- Barra inferior única **START Fechar** (como no 3dsDL); barra em y 209–239.

---

## 3. Catálogo

### 3.1 Superior — grade de jogos (Referência 1)

- Sem barra de status: a grade ocupa a tela toda, como na referência.
- 2 colunas × 4 linhas visíveis.
  - Colunas: x 4–197 e x 203–396 (cartões de 193 px).
  - Linhas: cartões de 55 px de altura a cada 59 px (y 4, 63, 122, 181).
- Cartão (fundo claro, cantos arredondados):
  - Ícone 48×48 em (x+4, y+4), com moldura de ícone do sistema.
  - Nome em até 2 linhas, x+58, largura 129 px, tamanho de texto “Row”; o que não couber termina em “...” (no cartão selecionado o texto rola).
  - Na base, alinhados à direita: ícone(s) de região na ponta e, à esquerda deles, o ícone de status **Instalada** ou **Atualização** (o de atualização substitui o de instalada). Sem ícone de status quando não instalada.
  - Estrela dourada pequena no canto superior direito quando o jogo é favorito.
- Seleção: anel verde em volta do cartão.
- Navegação: direcional ou Circle Pad move entre cartões (esquerda/direita troca de coluna). Descendo além da 4ª linha, a grade rola uma linha (animação suave); subindo além da 1ª, idem. Segurar a direção repete.
- Ordem: alfabética pelo nome do jogo.

### 3.2 Ícones de região

Ícones 16×16 (`romfs/ui/badges.png`): **USA**, **EUR**, **JPN**, **KOR**, **ALL** (livre de região), mais **Instalada** e **Atualização**. **CHN** e **TWN** usam selo de texto. Fonte: `compatibility.regions` da tradução (se mais de uma, até 2 ícones e “+N”).

### 3.3 Inferior — detalhes do jogo selecionado

Três abas no alto da tela (y 4–28), trocadas por toque: **Info**, **Tradução** e **Créditos**. Abaixo delas, a área rolável (y 32–206, toque ou Circle Pad). As ações ficam nas abas cinza de baixo: **B Voltar** à esquerda e a ação da aba à direita. **L/R** trocam de tradução em qualquer aba.

**Info**
- Nome do jogo (até 2 linhas, à esquerda).
- Capa 192×128 (3:2), centralizada.
- **Plataforma**, **Title ID** (todos numa linha), **Versão do jogo** (exigida pela tradução selecionada), cada um em uma linha.
- Descrição do jogo.
- Aba direita: **X Favoritar** / **X Desfavoritar**.

**Tradução**
- “Tradução 1/2 · L / R para trocar”, quando houver mais de uma.
- Nome da tradução (até 2 linhas).
- **Idioma**, **Versão da tradução**, **Lançamento**, **Regiões**, **Consoles**, **Download** (uma linha cada), **Requisitos** (até 2 linhas), **Observações** (várias linhas, em vermelho).
- Descrição da tradução.
- Aba direita: **A Instalar** / **A Atualizar** / **A Reinstalar**; “Na fila” ou “Requer app novo” sem tecla quando não dá para instalar.

**Créditos**
- Equipe da tradução: nome e, abaixo, a função, à esquerda, com quebra de linha.
- Sem ação à direita.

---

## 4. Busca

**Inferior — teclado próprio** (não o teclado do sistema, que ocuparia as duas telas):
- Campo de texto no topo (y 6–34) com o termo e um botão de apagar tudo.
- Teclado QWERTY em 4 linhas de teclas 28×28: letras, números, espaço, apagar. A busca ignora acentos e maiúsculas (“pokemon” encontra “Pokémon”).
- A busca é instantânea a cada tecla.

**Superior — resultados**
- A mesma grade 2×4 do Catálogo, filtrada pelo nome do jogo.
- Vazio: “Digite o nome do jogo”; sem resultados: “Nenhum jogo encontrado”.

**Fluxo**
- O direcional move a seleção na grade de resultados enquanto o teclado continua tocável.
- **A** num resultado → a tela inferior troca o teclado pelos detalhes do jogo (idênticos ao §3.3, com os botões Favoritar e Instalar).
- **B** nos detalhes → volta ao teclado com o termo mantido. **B** no teclado → volta à Tela inicial.

---

## 5. Favoritos

- Superior e inferior idênticas ao Catálogo, mostrando só os jogos favoritos.
- Favoritos salvos por jogo em `sdmc:/3ds/VersaoBrasileira/favorites.json` (lista de `game_id`).
- Desfavoritar aqui remove o jogo da grade na hora (a seleção passa para o vizinho).
- Vazio: “Nenhum favorito.”

---

## 5.1 +Baixados

- **Superior**: a mesma grade do Catálogo, com as traduções mais baixadas em ordem (ranking do servidor, `baixados.php`). Cada cartão tem uma medalha com a posição sobre o canto do ícone (1º ouro, 2º prata, 3º bronze, as demais em verde) e, na base, o total de downloads. Só entram traduções que existem no catálogo local; um jogo pode aparecer mais de uma vez, uma por tradução.
- **Inferior**: os mesmos detalhes em abas do §3.3, abertos na tradução daquele cartão (L/R continuam trocando).
- Enquanto carrega: “Carregando as mais baixadas...”. Sem conexão: mensagem e **A Repetir**.
- O ranking é consultado a cada vez que a tela é aberta.

---

## 6. Biblioteca

**Superior**: a mesma grade do Catálogo, só com jogos que têm tradução instalada (lida dos manifestos). Todos os cartões têm o ícone **Instalada** ou **Atualização**.

**Inferior**: as mesmas abas do §3.3.
- **Info** e **Créditos** iguais ao Catálogo.
- **Tradução**: nome, Idioma, **Versão instalada**, **Versão disponível**, **Instalada em**, **Arquivos** (quantidade e tamanho no SD), **CIA** (quando houver), Lançamento, compatibilidade e descrição.
- Botões iguais em todas as abas: **B Voltar** à esquerda; **Favoritar** só com o ícone de estrela, no meio (tecla X); à direita **A Atualizar** quando há versão nova, senão **Y Desinstalar** (Y desinstala também quando a aba mostra Atualizar). Desinstalar abre o diálogo destrutivo e vai para a Fila.

---

## 7. Fila

Uma operação por vez; as demais esperam na fila. A fila continua rodando enquanto o usuário navega pelas outras telas do app. A fila fica só na memória (o app pergunta antes de fechar se houver algo em andamento).

**Superior — fila (1 coluna × 4 linhas)**
- Linhas de 392×55 a cada 59 px.
- Cada entrada: ícone 48×48, nome do jogo (e da tradução em texto menor), etapa atual (“Etapa 2 de 4 · Extraindo arquivos”) e um selo de estado: **Em andamento** (azul), **Na fila** (cinza), **Concluído** (verde), **Falhou** (vermelho), **Cancelado** (cinza).
- Barra fina de progresso no pé da entrada em andamento.
- Navegação pelo direcional/Circle Pad com rolagem.
- Entradas concluídas ficam até serem limpas.

**Inferior — detalhes da entrada selecionada**
- Fatos: **Tradução**, **Operação** (Instalação / Atualização / Desinstalação), **Etapas** (“2 de 4”).
- **Barra geral** com porcentagem (texto 18 px acima da barra).
- **Barra da etapa** com duas linhas acima: o nome da etapa e, abaixo, os números (“1,27 MiB de 4,42 MiB · 185 KiB/s · 17 s”). Se falhou, as duas linhas mostram o erro e o detalhe técnico.
- Botões conforme o estado:
  - Em andamento: **Cancelar** — habilitado só enquanto a etapa atual for `downloadFile`.
  - Na fila: **Remover da Fila**.
  - Falhou: **Repetir** e **Remover**.
- Abas: **B Voltar**, **X Limpar Concluídos**.

Cancelar num download que vem depois de etapas que já gravaram arquivos (Exemplo 4) apaga esses arquivos usando o registro da instalação.

---

## 8. Ajustes

Segue o padrão “cima mostra, baixo age” do 3dsDL.

**Superior**: barra de status, título “Ajustes” com engrenagem, painel com o valor atual da opção selecionada.

**Inferior — lista de opções**
| Opção | Tipo | Detalhe |
|---|---|---|
| Servidor do catálogo | texto | URL do `version.json`; editada pelo teclado do sistema (`swkbd`), com “Restaurar Padrão” |
| Verificar ao Iniciar | Sim/Não | Pula a verificação do catálogo se “Não” (a do aplicativo continua) |
| Conexões Simultâneas | 1–4 | Para downloads |
| Sons | Ligado/Desligado | |
| Limpar Cache de Imagens | ação | mostra o tamanho atual |
| Limpar Arquivos Temporários | ação | mostra o tamanho atual |
| Sobre | tela | versão do app, versão do catálogo, créditos (3dsDL, Universal Updater), licença GPL-3.0 |

Abas: **B Voltar**.

---

## 9. Elementos globais

- **Notificações** (cartão que desce na tela superior): “Instalação concluída: X”, “Falha ao instalar X”, “Adicionado à fila”.
- **Modo de espera** bloqueado enquanto houver processo; LED de notificação com a tampa fechada.
- **Fechar** com processo ativo: diálogo “Há um processo em andamento. Fechar mesmo assim?”.
- Imagens: ícones carregados sob demanda para as linhas visíveis e vizinhas; capa carregada quando o jogo é selecionado.
