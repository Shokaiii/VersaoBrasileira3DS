@echo off
rem Compila o Versao Brasileira com o devkitPro instalado no Windows.
rem Gera VersaoBrasileira.3dsx (e VersaoBrasileira.cia, se makerom e
rem bannertool estiverem em TranslationStore\tools) na pasta deste arquivo.
setlocal

set "PROJ=%~dp0"
set "PROJ=%PROJ:~0,-1%"
set "BASH=C:\devkitPro\msys2\usr\bin\bash.exe"
set "DRV=W:"

if not exist "%BASH%" (
    echo Nao encontrei o devkitPro em C:\devkitPro.
    goto fim
)

rem O Makefile chega como Makefile.txt; usa sempre a versao mais nova.
if exist "%PROJ%\TranslationStore\Makefile.txt" (
    copy /Y "%PROJ%\TranslationStore\Makefile.txt" "%PROJ%\TranslationStore\Makefile" >nul
)

rem O make do devkitPro nao aceita espacos no caminho: usa uma letra de unidade.
subst %DRV% /d >nul 2>&1
subst %DRV% "%PROJ%"
if errorlevel 1 (
    echo Nao foi possivel criar a unidade %DRV%.
    goto fim
)

set MSYSTEM=MSYS
set CHERE_INVOKING=1
"%BASH%" -lc "cd /w/TranslationStore && make 2>&1 | tee build.log; exit ${PIPESTATUS[0]}"
if errorlevel 1 (
    echo.
    echo A compilacao falhou. Veja TranslationStore\build.log
    goto desfaz
)

copy /Y "%DRV%\TranslationStore\VersaoBrasileira.3dsx" "%PROJ%\" >nul
echo VersaoBrasileira.3dsx copiado para a pasta do projeto.

"%BASH%" -lc "cd /w/TranslationStore && make cia 2>&1 | tee -a build.log; exit ${PIPESTATUS[0]}"
if errorlevel 1 (
    echo.
    echo O .cia nao foi gerado. Coloque makerom.exe e bannertool.exe em
    echo TranslationStore\tools e rode de novo.
) else (
    copy /Y "%DRV%\TranslationStore\VersaoBrasileira.cia" "%PROJ%\" >nul
    echo VersaoBrasileira.cia copiado para a pasta do projeto.
)

:desfaz
subst %DRV% /d >nul 2>&1
:fim
echo.
pause
