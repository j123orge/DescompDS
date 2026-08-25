@echo off
setlocal
echo =========================================================
echo   Building DescompDS (MSVC x64 + NMake)
echo =========================================================

:: Clean PATH to avoid batch syntax issues with parentheses
set "PATH=C:\devkitPro\msys2\usr\bin;C:\Windows\system32;C:\Windows;C:\Windows\System32\Wbem;C:\Windows\System32\WindowsPowerShell\v1.0"

call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
if %ERRORLEVEL% neq 0 (
    echo Error: Failed to initialize MSVC environment.
    exit /b 1
)

cd /d "D:\DescompDS"

cmake -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -B build -S .
if %ERRORLEVEL% neq 0 (
    echo Error: CMake configuration failed.
    exit /b 1
)

cmake --build build --config Release
if %ERRORLEVEL% neq 0 (
    echo Error: Build failed.
    exit /b 1
)

echo.
echo =========================================================
echo   Running Tests
echo =========================================================
build\tests\descomp_tests.exe
if %ERRORLEVEL% neq 0 (
    echo Error: Tests failed.
    exit /b 1
)

echo.
echo =========================================================
echo   Build and Tests Succeeded!
echo =========================================================
