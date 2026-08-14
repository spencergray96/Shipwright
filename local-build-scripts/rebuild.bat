@echo off
cd /d "%~dp0.."
"C:\Program Files\CMake\bin\cmake.exe" --build build\x64
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo BUILD FAILED
) else (
    echo.
    echo BUILD SUCCEEDED
)
pause
