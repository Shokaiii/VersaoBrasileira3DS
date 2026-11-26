# Versão Brasileira — Especificação 04: Painel administrativo

Status: v1.1 (2026-11-25). Código no repositório VersaoBrasileira3DS-Painel (`TranslationStore-Admin/`); inicia com `iniciar_painel.bat` (http://127.0.0.1:8765, só no computador).

## Tecnologia
Python + FastAPI (backend), HTML/CSS/JavaScript sem framework (frontend), JSON Schema (`backend/validators/schemas/game.schema.json`) + regras do app (caminhos `sdmc:/`, sem `..`, áreas protegidas iguais a `ops::checkWritePath`), Pillow para imagens, JSON local em `data/`.

## Dados
- `data/games/<game-id>.json` — jogo com as traduções, no mesmo formato do catalog.json (sem as URLs de imagem).
- `data/images/<game-id>/icon.png` (48×48) e `cover.jpg` (192×128, 3:2) — já processadas.
- `data/files/<translation-id>/<arquivo>` — pacotes das traduções (fora do Git).
- `data/settings/segredos.json` — chave do painel na API (`panel_key`), fora do Git.
- `data/settings/settings.json` — `base_url`, `catalog_url` (opcional), `catalog_version` e a publicação do aplicativo (`app_publish`, `app_required`, `app_notes`).

## Telas
- **Jogos**: lista com busca; **Novo jogo** (nome, identificador fixo, plataforma).
- **Jogo**: dados, Title IDs, ícone e capa (recorte na proporção certa com prévia no tamanho real), traduções (dados, equipe, compatibilidade, histórico, arquivos enviados com sha256/tamanho, etapas de instalação e desinstalação com sugestões de caminho e modelos LayeredFS/CIA). Identificadores de tradução ficam fixos depois de salvos.
- **Gerar catálogo**: verificação (erros bloqueiam), escolha do aumento de versão, prévia do catalog.json e geração de `output/` (version.json, catalog.json, images/, files/ e app/ quando o app é publicado).
- **Configurações**: endereço do servidor, versão do catálogo, chave do painel (Servidor) e a seção **Aplicativo** (versão do `TranslationStore/VERSION`, arquivos `.3dsx`/`.cia` compilados, publicar, atualização obrigatória, novidades).
- **Mensagens**: as mensagens do Contato lidas do servidor (`mensagens.php`), da mais nova para a mais antiga, com não lidas destacadas; **Marcar como lida** e **Apagar**.
- **Importar**: traz um catalog.json existente e, com a pasta informada, as imagens e os arquivos.

## Geração
- `sha256` dos `downloadFile` com arquivo enviado ao painel e `download_size` (quando vazio) são calculados na geração.
- `install_size` vazio = igual ao download.
- Valores padrão das etapas (`overwrite`, `missing_ok`, `delete_after` = true; `optional` = false) não vão para o JSON.
- A versão do catálogo só aumenta (o 3DS compara SemVer).
- Com "Publicar" marcado, o app compilado vai para `output/app/` e o bloco `app` entra no version.json (Especificação 01, §2.1).
