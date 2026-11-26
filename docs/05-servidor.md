# Versão Brasileira — Especificação 05: Servidor (API)

Status: v1 (2026-11-25). Código no repositório VersaoBrasileira3DS-Painel, em `TranslationStore-Admin/servidor/api/`. Instalação em `servidor/LEIA-ME.md`.

## Hospedagem
- HostGator, PHP 8.4, MySQL (banco `janvic19_vbrasil`, usuário `janvic19_vbapp`).
- Endereço: `https://versaobrasileira3ds.com.br/versaobrasileirahb/api/`.
- `config.php` (senha do banco, `panel_key`, `ip_salt`, e-mails) fica só no servidor e fora do Git.
- E-mail do Contato: enviado de e recebido em `contato@versaobrasileira3ds.com.br` (Titan), por SMTP autenticado em `smtp.titan.email:465` (SSL). O `mail()` do PHP entregava numa caixa local do servidor e não chegava ao Titan; ele só é usado se `smtp_pass` estiver vazio. A conta precisa ter o **acesso por apps de terceiros (IMAP/SMTP)** ativado no Titan; sem isso, o Titan responde "535 authentication failed".

## Tabelas
- `downloads` — total por tradução (`translation_id`, `game_id`, `total`, `updated_at`).
- `download_log` — cada +1 aceito (`translation_id`, código do endereço, data); apagado após 30 dias.
- `messages` — mensagens do Contato (`kind`, jogo/tradução de origem, texto, contato, versão do app, data, lida, enviada por e-mail).

## Endpoints
| Endereço | Uso |
|---|---|
| `GET baixados.php` | Ranking (até 50): `position`, `translation_id`, `game_id`, `downloads`. |
| `POST baixados.php` | `{"translation_id"}` → +1. Recusa traduções fora do `catalog.json` publicado; ignora repetição do mesmo endereço e tradução em 24 h (`counted: false`). |
| `POST contato.php` | `kind` (`erro`, `pedido`, `outro`), `message` (3–2000 caracteres), `contact`, `game_id`, `translation_id`, `game_name`, `app_version`. Grava e envia e-mail. Até 5 por hora por endereço (HTTP 429). |
| `GET/POST mensagens.php` | Para o painel, com o cabeçalho `X-Painel-Chave`: listar (`?desde=ID`), marcar como lida, apagar. |

Todas as respostas são JSON com `ok` (e `error` em português quando `ok` é falso). O endereço de quem chama é guardado só como `sha256(ip_salt + IP)`.

## Regra do +1 (app)
O app envia o +1 só quando uma instalação termina com sucesso **e** não havia manifesto daquela tradução no console: primeira instalação ou instalação depois de desinstalar. Atualizar e reinstalar não contam.
