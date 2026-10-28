@echo off
setlocal
cd /d "%~dp0"
if not exist build mkdir build

echo [1/2] compiling resources ^(icon / version info^)
windres res\app.rc -O coff -o build\app_res.o
if errorlevel 1 goto err

echo [2/2] compiling and linking
gcc -O2 -std=c11 -Wall -Wno-format-truncation -Wno-unused-function src\*.c build\app_res.o -o build\LazuliLauncher.exe -mwindows -static-libgcc -lgdiplus -lwinhttp -ldwmapi -lshlwapi -lole32 -luuid -lws2_32 -lmsimg32 -lcomdlg32 -lshell32 -lgdi32 -luser32 -lcomctl32
if errorlevel 1 goto err

echo.
echo BUILD OK  -^>  build\LazuliLauncher.exe
exit /b 0

:err
echo.
echo BUILD FAILED
exit /b 1
