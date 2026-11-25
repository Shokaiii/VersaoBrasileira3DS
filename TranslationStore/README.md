# Versão Brasileira (aplicativo 3DS)

Homebrew para baixar e instalar traduções de fãs direto pelo Nintendo 3DS.
Especificações em `docs/` no repositório VersaoBrasileira3DS.

## 1. Pacotes do devkitPro

No Windows, abra o **MSYS2** do devkitPro (menu Iniciar → devkitPro → MSYS2) e rode:

```
pacman -Syu
pacman -S --needed 3ds-dev 3ds-portlibs
```

`3ds-portlibs` traz curl, mbedtls, libarchive, zlib, bzip2, liblzma, zstd, lz4 e
libjpeg-turbo.

## 2. Compilar

Dê dois cliques em **`compilar.bat`**, na raiz da pasta do projeto. Ele:

- usa o devkitPro instalado em `C:\devkitPro`;
- cria a unidade temporária `W:` (o `make` do devkitPro não aceita espaços no caminho);
- compila e copia **`VersaoBrasileira.3dsx`** para a raiz da pasta do projeto;
- gera também **`VersaoBrasileira.cia`** se `makerom.exe` e `bannertool.exe` estiverem em
  `TranslationStore\tools\` (já incluídos; créditos e licenças em `tools/LICENCAS.md`);
- grava a saída em `TranslationStore\build.log`.

## 3. Instalar no console

- **.3dsx:** copie para `sdmc:/3ds/VersaoBrasileira/` e abra pelo Homebrew Launcher.
- **.cia:** instale com o FBI (ou o 3dsDL); o app aparece no menu HOME.

Na primeira execução o app cria `sdmc:/3ds/VersaoBrasileira/config.json` com
`version_url` = `https://versaobrasileira3ds.com.br/versaobrasileirahb/version.json`. Se o arquivo já
existir, ele é mantido como está.

Exemplos de `version.json` e `catalog.json` para esse servidor estão na raiz da pasta
do projeto.

## 4. Testar a Tela de Atualização com um servidor local

1. No computador, vá para `tests/servidor-exemplo/`.
2. Troque `192.168.0.10` pelo IP do computador na rede (veja com `ipconfig`) em
   `version.json`, `catalog.json` e `config.json`.
3. Rode um servidor simples nessa pasta: `python -m http.server 8000`
   (libere o Python no Firewall do Windows se ele perguntar).
4. Copie `config.json` para `sdmc:/3ds/VersaoBrasileira/config.json`.
5. Abra o app. Cenários para testar:
   - Primeira vez → "Baixando novo catálogo" → "Catálogo atualizado para a versão 1.0.0".
   - Abrir de novo → "O catálogo local já está atualizado".
   - Mudar `catalog_version` para `1.1.0` nos dois JSON → baixa de novo.
   - Parar o servidor → "Não foi possível verificar o catálogo" com
     **Tentar Novamente** / **Continuar**.
   - Desligar o Wi-Fi do console → "Sem conexão — usando o catálogo local".

O registro fica em `sdmc:/3ds/VersaoBrasileira/log.txt` (e `crash.txt` se o app
travar) — mande esses arquivos quando algo der errado.

## Estrutura

```
source/
  app/          inicialização, navegação, config.json
  ui/           kit visual (do 3dsDL), stb_image
  ui/screens/   telas
  store/        modelo do catálogo (jogos, traduções, etapas)
  repository/   JSON, versões, atualização do catálogo
  download/     HTTP(S) (do 3dsDL)
  installer/    Installation Pipeline (próxima etapa)
  storage/      arquivos no SD
  platform/     serviços do 3DS, registro, sons, LED
assets/icons/   ícone do app (app-icon.svg → icon.png)
romfs/          atlas de ícones e certificados
scripts/        make-ui-icons.py (gera o atlas de ícones)
tests/          servidor de exemplo
```
