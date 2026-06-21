@echo off
setlocal

set "ROOT=%~dp0"
set "OUT_DIR=%ROOT%build-mingw"
set "SRC=%ROOT%src\main.cpp"
set "EXE=%OUT_DIR%\standalone_audio_player.exe"
set "CLION_GPP=C:\Program Files\JetBrains\CLion 2026.1.2\bin\mingw\bin\g++.exe"
set "MINGW_BIN="

if not defined CXX (
    where g++.exe >nul 2>nul
    if errorlevel 1 (
        if exist "%CLION_GPP%" (
            set "CXX=%CLION_GPP%"
            set "MINGW_BIN=C:\Program Files\JetBrains\CLion 2026.1.2\bin\mingw\bin"
        ) else (
            echo g++.exe was not found in PATH.
            echo Set CXX to your compiler path, for example:
            echo   set "CXX=C:\Program Files\JetBrains\CLion 2026.1.2\bin\mingw\bin\g++.exe"
            exit /b 1
        )
    ) else (
        set "CXX=g++.exe"
    )
)

if defined MINGW_BIN set "PATH=%MINGW_BIN%;%PATH%"

if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"

echo compiler: "%CXX%"
"%CXX%" -std=c++17 "%SRC%" -o "%EXE%" -static -static-libgcc -static-libstdc++ -lwinmm -municode
if errorlevel 1 (
    echo build failed
    exit /b 1
)

echo built: "%EXE%"
