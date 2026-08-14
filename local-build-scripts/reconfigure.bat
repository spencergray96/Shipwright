@echo off
cd /d "%~dp0.."
"C:\Program Files\CMake\bin\cmake.exe" -S . -B "build/x64" -G "Visual Studio 17 2022" -T v143 -A x64
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo RECONFIGURE FAILED
) else (
    echo.
    echo RECONFIGURE SUCCEEDED - run rebuild.bat next
)
pause
