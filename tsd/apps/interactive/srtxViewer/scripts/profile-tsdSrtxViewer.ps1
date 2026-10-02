<#
.SYNOPSIS
    Launch tsdSrtxViewer under Nsight Systems with a hotkey-triggered capture window.

.DESCRIPTION
    Wraps `nsys profile` so the viewer starts with the profiler attached but NOT yet
    recording. You connect to the server in the UI, let rendering settle, then press
    the configured hotkey (default F12) to begin the capture, and press it again (or
    close the app) to end it. This keeps the report focused on the steady-state phase
    instead of the noisy initial connect.

    The script sets the same PATH that the corresponding .vcxproj.user uses so the
    viewer finds its runtime dependencies when launched outside of Visual Studio.

    Windows trace-scoping notes (these are not bugs in the script; just how nsys works
    on Windows -- documenting here so the resulting reports aren't misread):
      * CPU IP/backtrace sampling (`-s process-tree`) is scoped to the launched process
        and its children. Reports filtered by tsdSrtxViewer's pid will only see the
        viewer.
      * Driver-level traces (`-t wddm`, `-t opengl`, `-t dx12`, etc.) are recorded
        system-wide via ETW. They will include events from every process that touches
        the GPU. Filter on the target pid in post if that bothers you.

.PARAMETER Configuration
    Build configuration to profile. Selects:
        Release  -> D:\dev\VisRTX\_build\Release\tsdSrtxViewer.exe
                    PATH includes D:\dev\usddevice-build-pipeline\_install_srtx\bin
        Debug    -> D:\dev\VisRTX\_build_debug\Debug\tsdSrtxViewer.exe
                    PATH includes D:\dev\usddevice-build-pipeline\_install_srtx\debug\bin
                    Symbol search includes the matching _build_debug PDB folders so
                    sampled stacks can be resolved to function names.

.PARAMETER OutputDir
    Directory to write the .nsys-rep into. Defaults to <repo>\_profile.

.PARAMETER Tag
    Optional suffix added to the timestamped report filename. The current
    Configuration is always appended after the timestamp so debug and release
    reports never collide.

.PARAMETER Hotkey
    Capture toggle hotkey. Press once to start, once to stop. Default: F12.

.PARAMETER Trace
    Comma-separated list passed to `nsys profile -t`. Default: nvtx,opengl,cuda.
    On Windows, OS thread scheduling / context switches are captured via ETW
    automatically and do not need to be in this list. Add 'wddm' if you want
    driver-level Present/queue events. Valid: cuda, cuda-sw, nvtx, cublas, cuDNN,
    cusolver, cusparse, nvvideo, opengl[-annotations], vulkan[-annotations],
    dx11[-annotations], dx12[-annotations], openxr[-annotations], wddm, none.

.PARAMETER Sample
    `nsys profile -s` value. 'process-tree' (default) requires the script to run
    elevated; 'none' disables CPU sampling and works without admin.

.PARAMETER DebugSymbols
    Extra directories to search for PDB files, semicolon-separated. Sensible defaults
    are picked per Configuration; anything passed here is appended. Exposed to nsys
    via _NT_SYMBOL_PATH because nsys's own --debug-symbols flag splits on ':' which
    breaks Windows drive-letter paths.

.PARAMETER Stats
    If set, runs `nsys stats` against the produced report and writes the text output
    next to the report file.

.PARAMETER Exe
    Override the exe path. Defaults are derived from -Configuration.

.EXAMPLE
    PS> .\profile-tsdSrtxViewer.ps1

.EXAMPLE
    PS> .\profile-tsdSrtxViewer.ps1 -Configuration Debug -Stats

.EXAMPLE
    PS> .\profile-tsdSrtxViewer.ps1 -Trace nvtx,opengl,cuda,wddm -Tag presents
#>

[CmdletBinding()]
param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    [string]$OutputDir,
    [string]$Tag = "",
    [string]$Hotkey = "F12",
    [string]$Trace = "nvtx,opengl,cuda",
    [ValidateSet("process-tree", "system-wide", "none")]
    [string]$Sample = "process-tree",
    [string]$DebugSymbols = "",
    [switch]$Stats,
    [string]$Exe
)

$ErrorActionPreference = "Stop"

# Repo root is five levels up from this script:
#   <repo>\tsd\apps\interactive\srtxViewer\scripts\<this>
$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..\..\..\..")

# Per-configuration paths. These mirror what the corresponding .vcxproj.user
# of tsdSrtxViewer would set when launching from Visual Studio.
switch ($Configuration)
{
    "Release"
    {
        $defaultExe = Join-Path $repoRoot "_build\Release\tsdSrtxViewer.exe"
        $runtimePathDirs = @(
            "D:\dev\usddevice-build-pipeline\_install_srtx\bin",
            "D:\dev\thirdparty\oneapi-tbb-2022.3.0\redist\intel64\vc14"
        )
        # Release binaries are built without /Zi by default, so symbol resolution
        # will likely produce hex addresses for srtx_render_library.dll. Pointing
        # at the release-side install dir lets nsys pick up any PDBs that DO ship.
        $symbolDirs = @(
            (Join-Path $repoRoot "_build\Release"),
            "D:\dev\usddevice-build-pipeline\_install_srtx\bin"
        )
    }
    "Debug"
    {
        $defaultExe = Join-Path $repoRoot "_build_debug\Debug\tsdSrtxViewer.exe"
        $runtimePathDirs = @(
            "D:\dev\usddevice-build-pipeline\_install_srtx\debug\bin",
            "D:\dev\thirdparty\oneapi-tbb-2022.3.0\redist\intel64\vc14"
        )
        # Debug PDBs live next to the .obj files in the original build trees,
        # not next to the installed DLLs. Point nsys at those build dirs so
        # samples in srtx_render_library.dll resolve to real function names.
        $symbolDirs = @(
            (Join-Path $repoRoot "_build_debug\Debug"),
            "D:\dev\usddevice-build-pipeline\_install_srtx\debug\bin",
            "D:\dev\srtx-render-library\_build_debug\Debug",
            "D:\dev\usddevice-build-pipeline\_build_srtx_debug\Debug"
        )
    }
}

if (-not $Exe)
{
    $Exe = $defaultExe
}
if (-not (Test-Path $Exe))
{
    throw "Viewer not found at $Exe. Build the $Configuration configuration first, or pass -Exe <path>."
}

# Locate nsys.exe. Prefer the 2026.x install but fall back to PATH.
$nsysCandidates = @(
    "C:\Program Files\NVIDIA Corporation\Nsight Systems 2026.2.1\target-windows-x64\nsys.exe"
)
$nsys = $null
foreach ($c in $nsysCandidates)
{
    if (Test-Path $c) { $nsys = $c; break }
}
if (-not $nsys)
{
    $cmd = Get-Command nsys.exe -ErrorAction SilentlyContinue
    if ($cmd) { $nsys = $cmd.Source }
}
if (-not $nsys)
{
    throw "nsys.exe not found. Install Nsight Systems or add it to PATH."
}

if (-not $OutputDir)
{
    $OutputDir = Join-Path $repoRoot "_profile"
}
if (-not (Test-Path $OutputDir))
{
    New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
}

$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$cfgTag = $Configuration.ToLowerInvariant()
$suffix = if ($Tag) { "_${cfgTag}_${Tag}" } else { "_${cfgTag}" }
$report = Join-Path $OutputDir ("tsdSrtxViewer_${timestamp}${suffix}.nsys-rep")

# Prepend the runtime path so the launched viewer finds its DLLs.
$env:PATH = (($runtimePathDirs | Where-Object { Test-Path $_ }) -join ";") + ";" + $env:PATH

# Build the debug-symbol search path: per-configuration defaults + any user override.
#
# nsys's own --debug-symbols flag splits its argument on ':' which makes it
# unusable for Windows drive-letter paths (e.g. D:\dev\... gets parsed as 'D'
# and '\dev\...'). Instead, expose the search path through _NT_SYMBOL_PATH,
# the Windows convention that dbghelp.dll consults; nsys's symbol resolver
# honors it. _NT_SYMBOL_PATH uses ';' as a separator.
$existingSymbolDirs = $symbolDirs | Where-Object { Test-Path $_ }
if ($DebugSymbols)
{
    $existingSymbolDirs += ($DebugSymbols -split ';' | Where-Object { $_ })
}
$ntSymbolPath = ($existingSymbolDirs -join ';')
if ($env:_NT_SYMBOL_PATH)
{
    $env:_NT_SYMBOL_PATH = $ntSymbolPath + ';' + $env:_NT_SYMBOL_PATH
}
else
{
    $env:_NT_SYMBOL_PATH = $ntSymbolPath
}

# Admin-elevation hint for CPU sampling.
$isAdmin = ([Security.Principal.WindowsPrincipal] `
    [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole( `
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if ($Sample -ne "none" -and -not $isAdmin)
{
    Write-Warning ("CPU sampling '-s $Sample' requires an elevated shell. " +
        "nsys may fall back to 'none'. Re-run this script from an Administrator PowerShell " +
        "if you want CPU IP/backtrace samples.")
}

Write-Host ""
Write-Host "  Configuration : $Configuration"
Write-Host "  Target        : $Exe"
Write-Host "  Report        : $report"
Write-Host "  Trace         : $Trace"
Write-Host "  Sample        : $Sample"
Write-Host "  Hotkey        : $Hotkey  (press once to START capture, again to STOP)"
Write-Host "  Symbol dirs   :  (exposed via _NT_SYMBOL_PATH)"
foreach ($d in $existingSymbolDirs) { Write-Host "      $d" }
Write-Host ""
Write-Host "  Workflow:"
Write-Host "    1. The viewer window opens. Capture is NOT yet recording."
Write-Host "    2. Connect to the SRTX server, let the stream settle into steady state."
Write-Host "    3. Focus the viewer window, press $Hotkey to begin recording."
Write-Host "    4. Press $Hotkey again to end the capture (or close the viewer)."
Write-Host ""

$nsysArgs = @(
    "profile",
    "-c", "hotkey",
    "--hotkey-capture", $Hotkey,
    "--capture-range-end", "stop",
    "-s", $Sample,
    "-t", $Trace,
    "-x", "true",
    "-f", "true",
    "--resolve-symbols", "true",
    "-o", $report,
    "--",
    $Exe
)

& $nsys @nsysArgs
$rc = $LASTEXITCODE

if ($rc -ne 0)
{
    Write-Warning "nsys exited with code $rc; the report may be incomplete."
}

if (Test-Path $report)
{
    Write-Host ""
    Write-Host "Report written: $report"

    if ($Stats)
    {
        $statsFile = [System.IO.Path]::ChangeExtension($report, ".stats.txt")
        Write-Host "Running 'nsys stats' -> $statsFile"
        & $nsys stats $report 2>&1 | Tee-Object -FilePath $statsFile | Out-Null
        Write-Host "Stats written:  $statsFile"
    }
}
else
{
    Write-Warning "No report produced at $report."
}
