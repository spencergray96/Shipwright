@echo off
cd /d "%~dp0.."
"C:\Program Files\CMake\bin\cmake.exe" --build build\x64 --target GenerateSohOtr
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ASSET GENERATION FAILED
) else (
    echo.
    echo ASSET GENERATION SUCCEEDED - run rebuild.bat next
)
pause
