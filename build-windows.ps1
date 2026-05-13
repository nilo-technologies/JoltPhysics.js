<#
.SYNOPSIS
    Configure + build JoltPhysics.js on Windows (Emscripten), using a local Jolt core tree instead of FetchContent Git.

.DESCRIPTION
    Default -BuildType Distribution matches ./build.sh with no args: fast Debug wasm-compat-only (ST+MT) is built
    first and renamed to debug-*.js, then full Distribution ST+MT (all npm dist outputs), then d.ts shims and
    Examples/js copy. Resulting dist/ is suitable for a single npm publish (release + debug entrypoints together).

.PARAMETER JoltPhysicsPath
    Path to the JoltPhysics C++ repo root (folder containing Build/). Passed to CMake as -DJOLT_PHYSICS_PATH=...
    Optional if $env:JOLT_PHYSICS_PATH is set, or if a sibling folder ..\JoltPhysics exists.

.PARAMETER BuildType
    Distribution (default), Release, or Debug - same primary CMAKE_BUILD_TYPE as ./build.sh $1.
    Debug skips the wasm-compat-only preamble (same as build.sh when BUILD_TYPE=Debug).

.PARAMETER EmsdkRoot
    emsdk directory containing emsdk_env.ps1. Defaults to $env:EMSDK.

.PARAMETER NiloJoltTag
    Git tag checked out in BOTH this repo (JoltPhysics.js) and the JoltPhysics (C++) clone before building.
    Default nilo-v5.5.0; override with -NiloJoltTag or $env:NILO_JOLT_TAG.

.PARAMETER SkipGitCheckout
    Do not run git fetch/checkout (e.g. non-git export).

.NOTES
    Mirrors build.sh for Windows (Ninja + cmd-safe codegen). Nilo uses paired tags (default nilo-v5.5.0).
#>
[CmdletBinding()]
param(
    [Parameter()]
    [string] $JoltPhysicsPath = "",

    [Parameter()]
    [ValidateSet("Debug", "Release", "Distribution")]
    [string] $BuildType = "Distribution",

    [Parameter()]
    [string] $EmsdkRoot = "",

    [Parameter()]
    [string] $NiloJoltTag = "",

    [Parameter()]
    [switch] $SkipGitCheckout
)

$ErrorActionPreference = "Stop"
$here = $PSScriptRoot

if ([string]::IsNullOrWhiteSpace($NiloJoltTag)) {
    if (-not [string]::IsNullOrWhiteSpace($env:NILO_JOLT_TAG)) {
        $NiloJoltTag = $env:NILO_JOLT_TAG
    }
    else {
        $NiloJoltTag = "nilo-v5.5.0"
    }
}

function Resolve-GitExecutable {
    $cmd = Get-Command git -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }
    $fallback = "C:\Program Files\Git\bin\git.exe"
    if (Test-Path -LiteralPath $fallback) {
        return $fallback
    }
    return $null
}

function Invoke-NiloPairedGitCheckout {
    param(
        [string] $GitExe,
        [string] $RepoPath,
        [string] $Tag
    )
    if ($SkipGitCheckout) {
        Write-Host "SkipGitCheckout: leaving $RepoPath branch/tag unchanged."
        return
    }
    if (-not (Test-Path -LiteralPath (Join-Path $RepoPath ".git"))) {
        Write-Warning "No .git under $RepoPath - skipped git checkout $Tag"
        return
    }
    Write-Host "Git checkout $Tag in $RepoPath"
    & $GitExe -C $RepoPath fetch --tags --prune 2>$null
    & $GitExe -C $RepoPath checkout $Tag
    if ($LASTEXITCODE -ne 0) {
        Write-Error @"
git checkout $Tag failed in $RepoPath.

Create the tag on both forks (same name), push to origin, then:
  git fetch --tags origin

Or pass -SkipGitCheckout if you intentionally build a dirty tree.
"@
    }
    $rev = (& $GitExe -C $RepoPath rev-parse --short HEAD).Trim()
    Write-Host "  -> HEAD $rev"
}

if ([string]::IsNullOrWhiteSpace($JoltPhysicsPath)) {
    if (-not [string]::IsNullOrWhiteSpace($env:JOLT_PHYSICS_PATH)) {
        $JoltPhysicsPath = $env:JOLT_PHYSICS_PATH
    }
    else {
        $sibling = Join-Path (Split-Path -Parent $here) "JoltPhysics"
        if (Test-Path -LiteralPath (Join-Path $sibling "Build\CMakeLists.txt")) {
            $JoltPhysicsPath = $sibling
        }
    }
}
if ([string]::IsNullOrWhiteSpace($JoltPhysicsPath)) {
    Write-Error @"
JoltPhysicsPath is required. Use -JoltPhysicsPath, or set `$env:JOLT_PHYSICS_PATH, or clone Jolt next to this repo.

Examples:
  .\build-windows.ps1 -JoltPhysicsPath C:\dev\JoltPhysics
  `$env:JOLT_PHYSICS_PATH = 'C:\dev\JoltPhysics'; .\build-windows.ps1
"@
}
if (-not (Test-Path -LiteralPath $JoltPhysicsPath)) {
    Write-Error "JoltPhysicsPath does not exist: $JoltPhysicsPath"
}
$coreResolved = (Resolve-Path -LiteralPath $JoltPhysicsPath).Path
if (-not (Test-Path -LiteralPath (Join-Path $coreResolved "Build\CMakeLists.txt"))) {
    Write-Error "JoltPhysicsPath must point at Jolt repo root (missing Build\CMakeLists.txt): $coreResolved"
}

$gitExe = Resolve-GitExecutable
if (-not $gitExe) {
    Write-Warning "git not found; paired tag checkout skipped. Install Git for Windows or add git to PATH."
}
else {
    Invoke-NiloPairedGitCheckout -GitExe $gitExe -RepoPath $coreResolved -Tag $NiloJoltTag
    Invoke-NiloPairedGitCheckout -GitExe $gitExe -RepoPath $here -Tag $NiloJoltTag
}

$characterVirtualHeader = Join-Path $coreResolved "Jolt\Physics\Character\CharacterVirtual.h"
if (Test-Path -LiteralPath $characterVirtualHeader) {
    $cv = Get-Content -Raw -LiteralPath $characterVirtualHeader
    if ($cv -match "OnContactAdded\(const CharacterVirtual \*inCharacter, const CharacterContact &inContact" -and
        $cv -notmatch "CharacterVirtual::Contact") {
        Write-Error @"
Jolt core at $coreResolved uses the newer CharacterContact listener API. JoltPhysics.js still targets the v5.5.0-era API.

Fix: git fetch --tags && git checkout nilo-v5.5.0 (or v5.5.0) in your Jolt clone, then re-run.
"@
    }
}

if ([string]::IsNullOrWhiteSpace($EmsdkRoot)) {
    $EmsdkRoot = $env:EMSDK
}
if ([string]::IsNullOrWhiteSpace($EmsdkRoot)) {
    Write-Error @"
EmsdkRoot / EMSDK is empty.

PowerShell:
  `$env:EMSDK = 'C:\dev\emsdk'
  .\build-windows.ps1
"@
}
$emsdkEnv = Join-Path $EmsdkRoot.TrimEnd('\', '/') "emsdk_env.ps1"
if (-not (Test-Path -LiteralPath $emsdkEnv)) {
    Write-Error "EmsdkRoot invalid (missing emsdk_env.ps1): $emsdkEnv. Set EMSDK or pass -EmsdkRoot."
}
. $emsdkEnv

foreach ($tool in @("emcmake", "cmake", "ninja", "npx")) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        Write-Error "$tool not on PATH after emsdk_env (install CMake, Ninja, Node.js)."
    }
}

$joltArg = "-DJOLT_PHYSICS_PATH=$($coreResolved -replace '\\', '/')"

function Invoke-EmcmakeBuild {
    param(
        [string] $BuildDir,
        [string] $CMakeBuildType,
        [string[]] $ExtraCmakeArgs
    )
    Write-Host "Configure $BuildDir (CMAKE_BUILD_TYPE=$CMakeBuildType) ..."
    $cmakeArgs = @(
        "-S", ".",
        "-B", $BuildDir,
        "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=$CMakeBuildType"
    ) + $ExtraCmakeArgs + $script:joltCommonArgs
    & emcmake cmake @cmakeArgs
    cmake --build $BuildDir --parallel
    if ($LASTEXITCODE -ne 0) {
        throw "cmake --build failed for $BuildDir (exit $LASTEXITCODE)"
    }
}

$script:joltCommonArgs = @($joltArg)

Push-Location $here
try {
    if (Test-Path "dist") {
        Remove-Item -Recurse -Force "dist"
    }
    New-Item -ItemType Directory -Path "dist" | Out-Null

    # --- build.sh: when BUILD_TYPE != Debug, fast Debug wasm-compat-only ST+MT, then mv to debug names ---
    if ($BuildType -ne "Debug") {
        Write-Host "=== Preamble: Debug + BUILD_WASM_COMPAT_ONLY (debug wasm-compat artifacts) ==="
        Invoke-EmcmakeBuild -BuildDir "Build/Debug/ST" -CMakeBuildType "Debug" -ExtraCmakeArgs @("-DBUILD_WASM_COMPAT_ONLY=ON")
        Invoke-EmcmakeBuild -BuildDir "Build/Debug/MT" -CMakeBuildType "Debug" -ExtraCmakeArgs @(
            "-DENABLE_MULTI_THREADING=ON",
            "-DENABLE_SIMD=ON",
            "-DBUILD_WASM_COMPAT_ONLY=ON"
        )
        if (-not (Test-Path "dist\jolt-physics.wasm-compat.js")) {
            throw "Preamble did not produce dist\jolt-physics.wasm-compat.js"
        }
        if (-not (Test-Path "dist\jolt-physics.multithread.wasm-compat.js")) {
            throw "Preamble did not produce dist\jolt-physics.multithread.wasm-compat.js"
        }
        Move-Item -Force "dist\jolt-physics.wasm-compat.js" "dist\jolt-physics.debug.wasm-compat.js"
        Move-Item -Force "dist\jolt-physics.multithread.wasm-compat.js" "dist\jolt-physics.debug.multithread.wasm-compat.js"
    }

    # --- build.sh: full primary CMAKE_BUILD_TYPE ST then MT ---
    Write-Host "=== Primary: $BuildType ST + MT (full npm dist) ==="
    Invoke-EmcmakeBuild -BuildDir "Build/$BuildType/ST" -CMakeBuildType $BuildType -ExtraCmakeArgs @()
    Invoke-EmcmakeBuild -BuildDir "Build/$BuildType/MT" -CMakeBuildType $BuildType -ExtraCmakeArgs @(
        "-DENABLE_MULTI_THREADING=ON",
        "-DENABLE_SIMD=ON"
    )

    # --- build.sh: when BUILD_TYPE = Debug, cp wasm-compat to debug names ---
    if ($BuildType -eq "Debug") {
        Copy-Item -Force "dist\jolt-physics.wasm-compat.js" "dist\jolt-physics.debug.wasm-compat.js"
        Copy-Item -Force "dist\jolt-physics.multithread.wasm-compat.js" "dist\jolt-physics.debug.multithread.wasm-compat.js"
    }

    # --- build.sh: sed debug multithread worker URL (always) ---
    $dbgMt = "dist\jolt-physics.debug.multithread.wasm-compat.js"
    if (Test-Path -LiteralPath $dbgMt) {
        $mt = Get-Content -Raw -LiteralPath $dbgMt
        $mt = $mt -replace "jolt-physics.multithread.wasm-compat.js", "jolt-physics.debug.multithread.wasm-compat.js"
        Set-Content -NoNewline -Path $dbgMt -Value $mt -Encoding utf8
    }
    else {
        throw "Missing $dbgMt after build (required for npm package)."
    }

    # --- build.sh: d.ts shims ---
    $dts = "import Jolt from ""./types"";`n`nexport default Jolt;`nexport * from ""./types"";`n`n"
    Set-Content -Path "dist\jolt-physics.d.ts" -Value $dts -Encoding utf8
    foreach ($name in @(
            "jolt-physics.wasm.d.ts",
            "jolt-physics.wasm-compat.d.ts",
            "jolt-physics.debug.wasm-compat.d.ts",
            "jolt-physics.multithread.d.ts",
            "jolt-physics.multithread.wasm.d.ts",
            "jolt-physics.multithread.wasm-compat.d.ts",
            "jolt-physics.debug.multithread.wasm-compat.d.ts")) {
        Copy-Item -Force "dist\jolt-physics.d.ts" "dist\$name"
    }

    $ex = Join-Path $here "Examples\js"
    if (Test-Path $ex) {
        Copy-Item -Force "dist\jolt-physics*.wasm-compat.js" $ex
    }

    # Validate dist matches package.json "files" (dist entries + types.d.ts)
    $requiredDist = @(
        "dist\jolt-physics.js",
        "dist\jolt-physics.d.ts",
        "dist\jolt-physics.wasm-compat.js",
        "dist\jolt-physics.wasm-compat.d.ts",
        "dist\jolt-physics.debug.wasm-compat.js",
        "dist\jolt-physics.debug.wasm-compat.d.ts",
        "dist\jolt-physics.wasm.js",
        "dist\jolt-physics.wasm.d.ts",
        "dist\jolt-physics.wasm.wasm",
        "dist\jolt-physics.multithread.wasm-compat.js",
        "dist\jolt-physics.multithread.wasm-compat.d.ts",
        "dist\jolt-physics.debug.multithread.wasm-compat.js",
        "dist\jolt-physics.debug.multithread.wasm-compat.d.ts",
        "dist\jolt-physics.multithread.wasm.js",
        "dist\jolt-physics.multithread.wasm.d.ts",
        "dist\jolt-physics.multithread.wasm.wasm",
        "dist\types.d.ts"
    )
    $missing = @()
    foreach ($p in $requiredDist) {
        if (-not (Test-Path -LiteralPath $p)) {
            $missing += $p
        }
    }
    if ($missing.Count -gt 0) {
        throw "dist/ is incomplete for npm publish. Missing:`n  " + ($missing -join "`n  ")
    }

    Write-Host "Done. dist/ is ready for npm pack / npm publish. ($($requiredDist.Count) expected artifacts present)"
}
finally {
    Pop-Location
}
