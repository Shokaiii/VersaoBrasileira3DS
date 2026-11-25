# Versão Brasileira 3DS

Homebrew para Nintendo 3DS que baixa e instala traduções de fãs em português do Brasil direto no console: catálogo com capas e ícones, busca, favoritos, biblioteca das traduções instaladas, fila de instalação e atualização do próprio aplicativo.

O catálogo é gerado pelo painel administrativo, que fica no repositório [VersaoBrasileira3DS-Painel](https://github.com/Shokaiii/VersaoBrasileira3DS-Painel).

## Conteúdo

```
TranslationStore/   código do aplicativo (C++17, devkitPro, libctru, citro2d)
  source/           código-fonte
  romfs/            imagens e certificados embutidos no app
  resources/        banner, som e configuração do .cia
  tools/            makerom e bannertool (para gerar o .cia)
compilar.bat        compila o .3dsx e o .cia no Windows
docs/               especificações do projeto
```

## Compilar (Windows)

1. Instale o [devkitPro](https://devkitpro.org/wiki/Getting_Started) em `C:\devkitPro`.
2. No MSYS2 do devkitPro, instale as bibliotecas:
   ```
   pacman -Syu
   pacman -S --needed 3ds-dev 3ds-portlibs
   ```
3. Dê dois cliques em `compilar.bat`. Os arquivos `VersaoBrasileira.3dsx` e `VersaoBrasileira.cia` aparecem na pasta onde está o `.bat`.

A versão do aplicativo fica em `TranslationStore/VERSION`.

## Instalar no console

- **.cia:** instale com o FBI; o app aparece no menu HOME.
- **.3dsx:** copie para `sdmc:/3ds/VersaoBrasileira/` e abra pelo Homebrew Launcher.

Requer um 3DS com Luma3DS.

## Créditos

- **[3dsDL](https://github.com/ankushKun/3dsDL)** (ankushKun, GPL-3.0) — kit de interface, rede, utilitários de arquivos e de plataforma, reaproveitados e adaptados.
- **[Universal Updater](https://github.com/Universal-Team/Universal-Updater)** (Universal-Team, GPL-3.0) — referência para o formato das etapas de instalação.
- **[makerom](https://github.com/3DSGuy/Project_CTR)** (Project_CTR — 3DSGuy, applestash e jakcron, licença MIT) — gera o `.cia`. Incluído em `TranslationStore/tools/`.
- **[bannertool](https://github.com/Steveice10/bannertool)** (Steveice10) — gera o banner e o ícone do `.cia`. Incluído em `TranslationStore/tools/`.
- Bibliotecas: libctru, citro2d, citro3d (devkitPro), libcurl, Mbed TLS, libarchive, zlib, bzip2, liblzma, zstd, lz4, libjpeg-turbo e stb_image.

Detalhes em `TranslationStore/CREDITS.md` e `TranslationStore/tools/LICENCAS.md`.

## Licença

GPL-3.0 — veja `LICENSE`.
