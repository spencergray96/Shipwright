# agent-env.ps1 - build environment for the Shipwright checkout this file sits in.
#
# Usage: dot-source it, then call the helpers.
#
#     . .\agent-env.ps1
#     Invoke-SohBuild
#
# WHY THIS EXISTS (issue #137)
# ---------------------------
# `/MP` is applied with no process count - CMakeLists.txt:80 plus three per-target
# sites - so the compiler spawns one process per logical core and nothing caps it.
# On a many-core machine that makes the desktop unusable for the duration of a
# build. Lowering the priority class fixes the symptom without capping throughput:
# the scheduler hands cores back the moment something in the foreground wants them,
# and the build soaks whatever is left. A hard core cap would cost build time even
# when the machine is otherwise idle; this does not.
#
# Node reuse is disabled here too. docs/BUILD_GUIDE.md records MSBuild sitting alive
# for 17 minutes with no compiler children, cured by turning it off.
#
# TREE SCOPING (issue #135)
# -------------------------
# Every path below derives from $PSScriptRoot, so the tree this file lives in is the
# tree it acts on. That makes a second checkout self-contained: copy this file there
# and it operates on that checkout, with no shared state to get out of sync. Agent
# tool calls do not carry shell state between invocations, so the path you invoke has
# to be what selects the tree - not an environment variable set earlier.

$script:SohTreeRoot = $PSScriptRoot
$script:SohBuildDir = Join-Path $PSScriptRoot 'build\x64'

# Resolve cmake rather than hardcoding an install path.
$script:SohCMake = (Get-Command cmake.exe -ErrorAction SilentlyContinue).Source
if (-not $script:SohCMake) {
    $fallback = Join-Path $env:ProgramFiles 'CMake\bin\cmake.exe'
    if (Test-Path $fallback) { $script:SohCMake = $fallback }
}
if (-not $script:SohCMake) {
    throw "cmake.exe not found on PATH or under '$env:ProgramFiles\CMake\bin'. Install CMake or add it to PATH."
}

$env:MSBUILDDISABLENODEREUSE = '1'

# Default priority for every build launched through these helpers. BelowNormal keeps
# the desktop responsive at almost no cost to build time; Low ('Idle') is stronger
# and slower - reach for it only if BelowNormal still lags.
$script:SohDefaultPriority = 'BelowNormal'

function Get-SohEnv {
    <#
    .SYNOPSIS
    Report which tree these helpers are bound to. Useful for confirming, from inside
    an agent session, that the expected checkout is the one about to be built.
    #>
    [PSCustomObject]@{
        TreeRoot        = $script:SohTreeRoot
        BuildDir        = $script:SohBuildDir
        CMake           = $script:SohCMake
        DefaultPriority = $script:SohDefaultPriority
        NodeReuse       = if ($env:MSBUILDDISABLENODEREUSE -eq '1') { 'disabled' } else { 'enabled' }
    }
}

function Invoke-SohCMake {
    <#
    .SYNOPSIS
    Run cmake at a lowered priority class, streaming its output, and return its exit code.

    .NOTES
    cmake inherits this console's handles, so its output goes straight to the console
    and PowerShell-level redirection does not see it: `Invoke-SohBuild *> build.log`
    captures only the [agent-env] lines below, not the compiler's. Anything capturing
    the whole session's stdout - an agent tool call, a shell transcript - gets all of
    it. To capture the compiler output to a file specifically, redirect the outer
    PowerShell process rather than this function.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string[]]$Arguments,
        [ValidateSet('BelowNormal', 'Low', 'Normal')][string]$Priority = $script:SohDefaultPriority
    )

    # PriorityClass is inherited at process creation, so cmake's msbuild and cl.exe
    # children all pick it up. cmake.exe itself runs a few milliseconds at Normal
    # before the assignment lands, which costs nothing - it is a launcher.
    $priorityClass = switch ($Priority) {
        'Low'    { 'Idle' }
        'Normal' { 'Normal' }
        default  { 'BelowNormal' }
    }

    # Write-Host, not Write-Output: an informational line written to the success
    # stream would be joined onto this function's return value.
    Write-Host "[agent-env] $(Split-Path $script:SohTreeRoot -Leaf): cmake $($Arguments -join ' ')"
    Write-Host "[agent-env] priority=$priorityClass nodeReuse=disabled"

    $started = Get-Date
    $proc = Start-Process -FilePath $script:SohCMake -ArgumentList $Arguments `
        -WorkingDirectory $script:SohTreeRoot -NoNewWindow -PassThru

    # Touching .Handle caches it. Without this, ExitCode reads back as $null after
    # the process exits, because the object Start-Process hands back does not retain
    # the handle it needs to query it.
    $null = $proc.Handle

    try { $proc.PriorityClass = $priorityClass }
    catch { Write-Host "[agent-env] WARNING: could not set priority: $($_.Exception.Message)" }

    $proc.WaitForExit()
    $elapsed = (Get-Date) - $started
    Write-Host ("[agent-env] exit={0} elapsed={1:hh\:mm\:ss}" -f $proc.ExitCode, $elapsed)

    $proc.ExitCode
}

function Invoke-SohBuild {
    <#
    .SYNOPSIS
    Build this tree. The everyday command - see docs/BUILD_GUIDE.md.
    #>
    [CmdletBinding()]
    param(
        [string]$Target,
        [ValidateSet('BelowNormal', 'Low', 'Normal')][string]$Priority = $script:SohDefaultPriority
    )
    $a = @('--build', $script:SohBuildDir)
    if ($Target) { $a += @('--target', $Target) }
    $a += @('--', '/nodeReuse:false')
    Invoke-SohCMake -Arguments $a -Priority $Priority
}

function Invoke-SohReconfigure {
    <#
    .SYNOPSIS
    Regenerate the Visual Studio project files. Needed after adding, deleting or
    renaming a source file, or editing a CMakeLists.txt.
    #>
    [CmdletBinding()]
    param([ValidateSet('BelowNormal', 'Low', 'Normal')][string]$Priority = $script:SohDefaultPriority)
    Invoke-SohCMake -Priority $Priority -Arguments @(
        '-S', $script:SohTreeRoot,
        '-B', $script:SohBuildDir,
        '-G', 'Visual Studio 17 2022',
        '-T', 'v143',
        '-A', 'x64'
    )
}

function Invoke-SohAssets {
    <#
    .SYNOPSIS
    Regenerate soh.o2r and copy it next to soh.exe. Only needed after changing
    something under soh/assets/custom/.

    .DESCRIPTION
    The copy is not optional and no build step does it: soh.exe runs from x64\Debug
    and searches its own directory. Skipping it does not fall back to the old
    texture - the renderer draws garbage. See docs/reference/ASSET_PIPELINE.md.
    #>
    [CmdletBinding()]
    param(
        [ValidateSet('Debug', 'Release')][string]$Config = 'Debug',
        [ValidateSet('BelowNormal', 'Low', 'Normal')][string]$Priority = $script:SohDefaultPriority
    )
    $code = Invoke-SohBuild -Target 'GenerateSohOtr' -Priority $Priority
    if ($code -ne 0) {
        Write-Host "[agent-env] GenerateSohOtr failed; not copying soh.o2r"
        return $code
    }

    $src = Join-Path $script:SohBuildDir 'soh\soh.o2r'
    $dstDir = Join-Path $script:SohTreeRoot "x64\$Config"
    if (-not (Test-Path $dstDir)) {
        Write-Host "[agent-env] no $dstDir yet - build once before copying assets"
        return 1
    }
    Copy-Item $src (Join-Path $dstDir 'soh.o2r') -Force
    Write-Host "[agent-env] copied soh.o2r into x64\$Config"
    0
}

# ---------------------------------------------------------------------------
# Issue #135 step 9 extends this file: SHIPWRIGHT_PATH and SOH_APP_DIR derived
# from $PSScriptRoot, and a copy of this file at the second checkout's root, so
# each tree's runtime paths and grid-tool export target follow its own wrapper.
# ---------------------------------------------------------------------------
