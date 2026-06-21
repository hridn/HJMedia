@echo off
setlocal

set "ROOT=%~dp0"
set "MINGW_BIN=C:\Program Files\JetBrains\CLion 2026.1.2\bin\mingw\bin"
set "EXE=%ROOT%build-mingw\standalone_audio_player.exe"

if exist "%MINGW_BIN%" set "PATH=%MINGW_BIN%;%PATH%"

if not exist "%EXE%" (
    echo executable not found: "%EXE%"
    echo run build_mingw.bat first
    exit /b 1
)

"%EXE%" %*
