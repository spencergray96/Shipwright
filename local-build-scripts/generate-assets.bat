@echo off
REM Regenerate soh.o2r from soh\assets\custom, then put it where the game will actually read it.
REM
REM The copy is the point. GenerateSohOtr writes build\x64\soh\soh.o2r and copies it to the repo
REM root, and soh.exe reads NEITHER - it runs from x64\Debug (or x64\Release) and searches its own
REM directory. A regenerated archive that never gets copied fails silently and badly: the missing
REM resource makes the renderer skip a second graphics command as well as the failed one, so the
REM texture draws as garbage rather than as the old art or as nothing.
REM See sturdy-bassoon docs\reference\ASSET_PIPELINE.md.
cd /d "%~dp0.."
"C:\Program Files\CMake\bin\cmake.exe" --build build\x64 --target GenerateSohOtr
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ASSET GENERATION FAILED
    pause
    exit /b 1
)

REM Debug always; Release only if it has been populated, so this never creates a half-set-up folder.
copy /Y "build\x64\soh\soh.o2r" "x64\Debug\soh.o2r" >nul
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ARCHIVE BUILT BUT COPY TO x64\Debug FAILED - the game will draw missing textures as garbage
    pause
    exit /b 1
)
echo Copied soh.o2r to x64\Debug
if exist "x64\Release\soh.exe" (
    copy /Y "build\x64\soh\soh.o2r" "x64\Release\soh.o2r" >nul
    echo Copied soh.o2r to x64\Release
)

echo.
echo ASSET GENERATION SUCCEEDED - run rebuild.bat next
pause
