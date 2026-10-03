@echo off
rem Build kotemado.exe (needs MinGW-w64 gcc and windres on PATH)
rem
rem   build.cmd          release build -> kotemado.exe
rem   build.cmd debug    debug build   -> build\kotemado-debug.exe
rem
rem MinGW's gcc links its own default-manifest.o, which clashes with ours
rem (two RT_MANIFEST #1). An empty default-manifest.o found first via -B
rem replaces it, so only src\kotemado.manifest ends up in the exe.
rem
rem This file is ASCII-only on purpose (see mayous\build.bat).
setlocal
cd /d "%~dp0"
if not exist build mkdir build
echo.> build\empty.c
gcc -c build\empty.c -o build\default-manifest.o || exit /b 1
windres -I src src\kotemado.rc -O coff -o build\kotemado.res.o || exit /b 1

set "SRC=src\main.c src\config.c src\engine.c src\theme.c src\ui_common.c src\ui_main.c src\ui_rule.c"
set "LIBS=-luser32 -lgdi32 -lshell32 -lcomctl32 -ldwmapi -luxtheme -lshcore -lshlwapi -lole32 -luuid"
set "WARN=-Wall -Wextra -Wno-cast-function-type"

if /i "%~1"=="debug" (
    gcc -Bbuild/ -g -O0 %WARN% -municode -mwindows -o build\kotemado-debug.exe %SRC% build\kotemado.res.o %LIBS% || exit /b 1
    echo built build\kotemado-debug.exe
    exit /b 0
)
gcc -Bbuild/ -O2 -fno-ident %WARN% -municode -mwindows -static -s -o kotemado.exe %SRC% build\kotemado.res.o %LIBS% || exit /b 1
for %%F in (kotemado.exe) do echo built kotemado.exe (%%~zF bytes)
