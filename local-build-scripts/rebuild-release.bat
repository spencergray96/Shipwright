@echo off
REM Build the Release configuration - the timing build - and make x64\Release runnable.
REM
REM Any frame-rate number worth quoting comes from Release; Debug is several times slower. The
REM build writes soh.exe, soh.o2r (regenerated on every build, sturdy-bassoon#146) and, as a
REM post-build step, assets\ into x64\Release, but not oot.o2r or the config the game needs beside
REM it, so those are copied over from x64\Debug. A file already in x64\Release is left alone - its
REM shipofharkinian.json is its own, and settings changed in a Release session survive a rebuild.
REM
REM Node reuse is off because Release builds have hung with it on. When to reconfigure first, and
REM everything else: sturdy-bassoon docs\BUILD_GUIDE.md, "When to run which" and "Release builds".
cd /d "%~dp0.."
"C:\Program Files\CMake\bin\cmake.exe" --build build\x64 --config Release -- /nodeReuse:false
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo BUILD FAILED
    pause
    exit /b 1
)

if not exist "x64\Debug\oot.o2r" (
    echo.
    echo BUILD SUCCEEDED, but x64\Debug has no oot.o2r to copy from.
    echo Build Debug and run the game from x64\Debug once, then run this again.
    pause
    exit /b 1
)

for %%F in (oot.o2r gamecontrollerdb.txt shipofharkinian.json imgui.ini) do (
    if not exist "x64\Release\%%F" if exist "x64\Debug\%%F" copy "x64\Debug\%%F" "x64\Release\%%F" >nul
)
REM The build's post-build step writes assets\, but only when soh.exe is relinked.
if not exist "x64\Release\assets" xcopy /E /I /Q "x64\Debug\assets" "x64\Release\assets" >nul

REM mods\ can hold a texture pack of ~23 GB, and it is loaded at boot, so Release needs it too for
REM the two tiers to be comparable. Hard links: the same bytes, no second copy. Top-level files only.
if exist "x64\Debug\mods" (
    if not exist "x64\Release\mods" mkdir "x64\Release\mods"
    for %%F in ("x64\Debug\mods\*") do (
        if not exist "x64\Release\mods\%%~nxF" mklink /H "x64\Release\mods\%%~nxF" "%%~fF" >nul
    )
)

if not exist "x64\Release\oot.o2r" (
    echo.
    echo BUILD SUCCEEDED, but copying oot.o2r into x64\Release FAILED
    pause
    exit /b 1
)
if not exist "x64\Release\soh.o2r" (
    echo.
    echo BUILD SUCCEEDED, but the build left no soh.o2r in x64\Release - the game would draw garbage
    pause
    exit /b 1
)

echo.
echo BUILD SUCCEEDED - the game is x64\Release\soh.exe
pause
