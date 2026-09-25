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

# vcpkg builds ~16 static ports into build/x64/vcpkg (2.28 GB). A second checkout
# would rebuild all of them, so borrow a sibling's tree when this one has none.
# automate-vcpkg.cmake:84-90 takes VCPKG_ROOT from the environment whenever the CMake
# variable is not already set.
#
# Resolution order - own tree first, then the first sibling checkout that has one -
# so this file stays identical in every tree and can be copied rather than edited.
#
# CAUTION: two trees sharing one vcpkg tree must not *configure* at the same time.
# Building concurrently is fine (the shared tree is read-only once populated), but a
# concurrent bootstrap would have both writing to it.
$script:SohVcpkgRoot = $null
$ownVcpkg = Join-Path $PSScriptRoot 'build\x64\vcpkg'
if (Test-Path (Join-Path $ownVcpkg 'scripts\buildsystems\vcpkg.cmake')) {
    $script:SohVcpkgRoot = $ownVcpkg
}
else {
    $siblingRoot = Split-Path $PSScriptRoot -Parent
    foreach ($dir in Get-ChildItem $siblingRoot -Directory -ErrorAction SilentlyContinue) {
        if ($dir.FullName -eq $PSScriptRoot) { continue }
        $candidate = Join-Path $dir.FullName 'build\x64\vcpkg'
        if (Test-Path (Join-Path $candidate 'scripts\buildsystems\vcpkg.cmake')) {
            $script:SohVcpkgRoot = $candidate
            break
        }
    }
}
if ($script:SohVcpkgRoot) { $env:VCPKG_ROOT = $script:SohVcpkgRoot }

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
        VcpkgRoot       = if ($script:SohVcpkgRoot) { $script:SohVcpkgRoot } else { '(none found - will bootstrap)' }
        VcpkgShared     = if ($script:SohVcpkgRoot -and -not $script:SohVcpkgRoot.StartsWith($PSScriptRoot)) { 'yes - borrowed from a sibling tree' } else { 'no - this tree owns it' }
        DefaultPriority = $script:SohDefaultPriority
        NodeReuse       = if ($env:MSBUILDDISABLENODEREUSE -eq '1') { 'disabled' } else { 'enabled' }
        ShipwrightPath  = $env:SHIPWRIGHT_PATH
        SohAppDir       = $env:SOH_APP_DIR
        AgentLoopLoaded = $script:SohAgentLoaded
    }
}

# ---------------------------------------------------------------------------
# Tree claims.
#
# A claim is a DECLARATION, not an inference. Availability cannot be read off git
# state: a tree with uncommitted work may be perfectly free to build in (the human
# is editing maps and does not mind), and a tree that looks clean may be held by a
# session that is between edits. Only the person or agent using a tree knows, so
# they say so.
#
# There is deliberately no identity here - no session id, no pid. The `for=` line
# is the identity, and it is legible to a human. That is what makes this survive
# the things that sank a keyed lease: CLAUDE_CODE_SESSION_ID is inherited by
# subagents and rotates mid-conversation.
#
# Distinct from .agent-build.lock, which is mechanical and lives for one build.
# A claim covers the whole task, including the gaps between builds, and is only
# ever released by whoever took it.
# ---------------------------------------------------------------------------
$script:SohClaimFile = '.agent-claim'

function Get-SohTreeRoots {
    # Every checkout beside this one: a sibling directory carrying its own wrapper.
    # Discovered rather than listed, so a third tree needs no edit here.
    $parent = Split-Path $script:SohTreeRoot -Parent
    $roots = @()
    foreach ($dir in Get-ChildItem $parent -Directory -ErrorAction SilentlyContinue) {
        if (Test-Path (Join-Path $dir.FullName 'agent-env.ps1')) { $roots += $dir.FullName }
    }
    $roots
}

function Read-SohClaim {
    param([Parameter(Mandatory)][string]$TreeRoot)
    $f = Join-Path $TreeRoot $script:SohClaimFile
    if (-not (Test-Path $f)) { return $null }
    $for = ''
    $since = ''
    foreach ($line in (Get-Content $f -ErrorAction SilentlyContinue)) {
        if ($line -match '^for=(.*)$') { $for = $Matches[1] }
        if ($line -match '^since=(.*)$') { $since = $Matches[1] }
    }
    [PSCustomObject]@{ For = $for; Since = $since }
}

function Get-SohTrees {
    <#
    .SYNOPSIS
    Every checkout, who holds it, and what state it is in. Run this before claiming.
    #>
    foreach ($root in Get-SohTreeRoots) {
        $claim = Read-SohClaim -TreeRoot $root
        # Nothing expires a claim automatically - a leftover one is a question for a
        # human, not something to clear so a tree can be taken. So how long it has
        # been held is the only staleness signal there is, and it has to read at a
        # glance: an hours-or-days claim is almost certainly a session that ended.
        $age = ''
        if ($claim -and $claim.Since) {
            try {
                $span = (Get-Date) - [datetime]::Parse($claim.Since)
                if ($span.TotalMinutes -lt 60) { $age = '{0:0}m' -f $span.TotalMinutes }
                elseif ($span.TotalHours -lt 24) { $age = '{0:0}h' -f $span.TotalHours }
                else { $age = '{0:0}d - stale?' -f $span.TotalDays }
            }
            catch { $age = '?' }
        }
        [PSCustomObject]@{
            Tree     = Split-Path $root -Leaf
            Claimed  = if ($claim) { $claim.For } else { '(free)' }
            Held     = $age
            Branch   = (git -C $root branch --show-current 2>$null)
            Dirty    = @(git -C $root status --porcelain 2>$null).Count
            Building = (Test-Path (Join-Path $root '.agent-build.lock'))
        }
    }
}

function Claim-SohTree {
    <#
    .SYNOPSIS
    Take the first unclaimed checkout - or a named one - and say what for.

    .EXAMPLE
    $t = Claim-SohTree -For 'issue 140: quest journal sort order'
    . $t.Root\agent-env.ps1

    .EXAMPLE
    # The human reserving a tree for themselves, so agents route elsewhere:
    Claim-SohTree -For 'mine - grid tool, please leave alone' -Tree Shipwright
    #>
    param(
        [Parameter(Mandatory)][string]$For,
        [string]$Tree
    )
    $candidates = Get-SohTreeRoots
    if ($Tree) {
        $candidates = @($candidates | Where-Object { (Split-Path $_ -Leaf) -eq $Tree })
        if ($candidates.Count -eq 0) {
            throw "No checkout named '$Tree' beside $(Split-Path $script:SohTreeRoot -Parent)."
        }
    }
    foreach ($root in $candidates) {
        $f = Join-Path $root $script:SohClaimFile
        $stream = $null
        try {
            # CreateNew is atomic, so two agents claiming at once cannot both win.
            $stream = [System.IO.File]::Open($f, [System.IO.FileMode]::CreateNew,
                                             [System.IO.FileAccess]::Write,
                                             [System.IO.FileShare]::Read)
        }
        catch [System.IO.IOException] {
            continue
        }
        $writer = New-Object System.IO.StreamWriter($stream)
        $writer.WriteLine("for=$For")
        $writer.WriteLine("since=$((Get-Date).ToUniversalTime().ToString('o'))")
        $writer.Dispose()
        $stream.Dispose()
        Write-Host "[agent-env] claimed $(Split-Path $root -Leaf) for: $For"
        return [PSCustomObject]@{
            Tree  = Split-Path $root -Leaf
            Root  = $root
            Enter = ". '$(Join-Path $root 'agent-env.ps1')'"
        }
    }
    $held = (Get-SohTrees | ForEach-Object { "  $($_.Tree) - $($_.Claimed)" }) -join [Environment]::NewLine
    throw ("Every checkout is already claimed:" + [Environment]::NewLine + $held +
           [Environment]::NewLine +
           "Ask which tree is yours, or wait for one to be released. Do not just take one - " +
           "two sessions editing a tree between builds is the case nothing detects.")
}

function Release-SohTree {
    <#
    .SYNOPSIS
    Give a checkout back. Release what you claimed, when the task is done.
    #>
    param([string]$Tree)
    $root = $script:SohTreeRoot
    if ($Tree) {
        $match = @(Get-SohTreeRoots | Where-Object { (Split-Path $_ -Leaf) -eq $Tree })
        if ($match.Count -eq 0) { throw "No checkout named '$Tree'." }
        $root = $match[0]
    }
    $f = Join-Path $root $script:SohClaimFile
    if (-not (Test-Path $f)) {
        Write-Host "[agent-env] $(Split-Path $root -Leaf) was not claimed"
        return
    }
    $claim = Read-SohClaim -TreeRoot $root
    Remove-Item -LiteralPath $f -Force
    Write-Host "[agent-env] released $(Split-Path $root -Leaf) (was: $($claim.For))"
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

    # Surface the claim rather than refusing on it: this may well be your own claim,
    # and nothing here can tell. An agent that reads a `for=` it does not recognise
    # should stop and ask rather than build over someone's task.
    $ownClaim = Read-SohClaim -TreeRoot $script:SohTreeRoot
    if ($ownClaim) { Write-Host "[agent-env] this tree is claimed for: $($ownClaim.For)" }
    else { Write-Host "[agent-env] this tree is unclaimed" }

    # Start-Process joins ArgumentList with spaces and quotes nothing, so an argument
    # that contains whitespace arrives at the callee split into several. That turns
    # -G "Visual Studio 17 2022" into four arguments and cmake reports it cannot create
    # a generator named "Visual". Quote them here rather than relying on the caller.
    $quoted = $Arguments | ForEach-Object {
        if ($_ -match '\s') { '"' + $_ + '"' } else { $_ }
    }

    # Take the tree's build lock. Two sessions building one tree write the same
    # soh.exe from different sources and neither is told: the binary matches neither
    # checkout, and nothing downstream can tell. "One task per tree" is a convention
    # agents are asked to honour; this is the mechanism that does not depend on them.
    #
    # The lock lives exactly as long as the build, so the cmake process is its own
    # identity - no session id to rotate or be inherited, no staleness heuristic
    # beyond "is that pid still alive".
    $lockFile = Join-Path $script:SohTreeRoot '.agent-build.lock'
    $lockStream = $null
    try {
        # CreateNew throws if the file exists: atomic, so two builds racing here
        # cannot both believe they won.
        $lockStream = [System.IO.File]::Open($lockFile, [System.IO.FileMode]::CreateNew,
                                             [System.IO.FileAccess]::Write,
                                             [System.IO.FileShare]::Read)
    }
    catch [System.IO.IOException] {
        $holder = try { (Get-Content $lockFile -Raw -ErrorAction Stop).Trim() } catch { '(unreadable)' }
        $holderPid = if ($holder -match 'pid=(\d+)') { [int]$Matches[1] } else { 0 }
        $alive = $holderPid -gt 0 -and (Get-Process -Id $holderPid -ErrorAction SilentlyContinue)
        if ($alive) {
            throw ("A build is already running in $(Split-Path $script:SohTreeRoot -Leaf): $holder" +
                   [Environment]::NewLine +
                   "Wait for it, or build in the other checkout. Two builds in one tree overwrite " +
                   "the same soh.exe from different sources.")
        }
        # Holder is gone - a killed or crashed build. Reclaim rather than wedge the tree.
        Write-Host "[agent-env] clearing a stale build lock from $holder"
        Remove-Item -LiteralPath $lockFile -Force
        $lockStream = [System.IO.File]::Open($lockFile, [System.IO.FileMode]::CreateNew,
                                             [System.IO.FileAccess]::Write,
                                             [System.IO.FileShare]::Read)
    }

    $writer = New-Object System.IO.StreamWriter($lockStream)
    $writer.WriteLine("pid=$PID started=$((Get-Date).ToUniversalTime().ToString('o')) tree=$(Split-Path $script:SohTreeRoot -Leaf)")
    $writer.Flush()

    try {

    $started = Get-Date
    $proc = Start-Process -FilePath $script:SohCMake -ArgumentList $quoted `
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
    finally {
        # finally, not a trailing statement: a failed build, a thrown error or Ctrl+C
        # must all release the tree. A lock that outlives its build wedges the checkout.
        if ($writer) { $writer.Dispose() }
        if ($lockStream) { $lockStream.Dispose() }
        Remove-Item -LiteralPath $lockFile -Force -ErrorAction SilentlyContinue
    }
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
# Runtime and tooling paths for this tree (issue #135).
#
# SHIPWRIGHT_PATH  - where the grid tool exports scene C and runs its verify
#                    scripts. Unset, those resolve to a sibling by convention and
#                    a second tree silently reads the first one's files.
# SOH_APP_DIR      - the game's working directory, which is also where soh.exe,
#                    agent-commands.txt, agent-log.txt, the engine log and
#                    shipofharkinian.json live. The game has no environment
#                    variable of its own for this: libultraship's app dir is
#                    literally the process working directory, so separate working
#                    directories are the whole isolation mechanism.
#
# Both derive from $PSScriptRoot, so they cannot disagree about which tree they
# mean. Pointing them at different trees would export to one and test the other,
# which presents as "my change did not take effect".
# ---------------------------------------------------------------------------
$env:SHIPWRIGHT_PATH = $script:SohTreeRoot
$env:SOH_APP_DIR = Join-Path $script:SohTreeRoot 'x64\Debug'

# soh-agent.ps1 reads SOH_APP_DIR at dot-source time, not per call, so it has to
# be set above this line.
$script:SohAgentScript = Join-Path $script:SohTreeRoot '..\sturdy-bassoon\.claude\skills\soh-agent-test\soh-agent.ps1'
if (Test-Path $script:SohAgentScript) {
    . $script:SohAgentScript
    $script:SohAgentLoaded = $true
}
else {
    $script:SohAgentLoaded = $false
    Write-Host "[agent-env] note: agent test loop not found beside this tree; build helpers only"
}
