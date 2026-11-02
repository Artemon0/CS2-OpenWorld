<#
.SYNOPSIS
Builds CS2 Open World on Windows with the MSVC environment applied.

.DESCRIPTION
The Ninja build directory needs the Visual Studio developer environment (INCLUDE, LIB, PATH).
Running `cmake --build` from a plain PowerShell window fails with errors such as
`fatal error C1083: cannot open include file: 'string'`.
This script locates vcvars64.bat, imports that environment, and then invokes CMake.

.PARAMETER Configuration
Build configuration. Defaults to Release.

.PARAMETER BuildDir
CMake build directory. Defaults to build-windows-release.

.PARAMETER Reconfigure
Re-run the CMake configure step before building.

.PARAMETER RunTests
Run ctest after a successful build.

.EXAMPLE
.\build.ps1

.EXAMPLE
.\build.ps1 -Reconfigure -RunTests
#>
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [string]$BuildDir = 'build-windows-release',
    [switch]$Reconfigure,
    [switch]$RunTests
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildPath = Join-Path $root $BuildDir

function Find-VcVars64 {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $installation = & $vswhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath
        if ($installation) {
            $candidate = Join-Path $installation 'VC\Auxiliary\Build\vcvars64.bat'
            if (Test-Path $candidate) {
                return $candidate
            }
        }
    }

    $roots = @(
        $env:ProgramFiles,
        ${env:ProgramFiles(x86)},
        'C:\',
        'D:\'
    ) | Where-Object { $_ -and (Test-Path $_) }

    foreach ($rootDir in $roots) {
        $candidates = Get-ChildItem -Path $rootDir -Directory -Filter 'VisualStudio*' -ErrorAction SilentlyContinue
        foreach ($candidate in $candidates) {
            $vcvars = Join-Path $candidate.FullName 'VC\Auxiliary\Build\vcvars64.bat'
            if (Test-Path $vcvars) {
                return $vcvars
            }
        }
    }

    return $null
}

$vcvars = Find-VcVars64
if (-not $vcvars) {
    throw 'Could not find vcvars64.bat. Install the "Desktop development with C++" workload for Visual Studio.'
}

Write-Host "Using Visual Studio environment: $vcvars"

if ($Reconfigure) {
    $configure = 'cmake -S "{0}" -B "{1}" -G Ninja -DCMAKE_BUILD_TYPE={2}' -f $root, $buildPath, $Configuration
    cmd /c "call `"$vcvars`" >nul && $configure"
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

$build = 'cmake --build "{0}" --config {1}' -f $buildPath, $Configuration
cmd /c "call `"$vcvars`" >nul && $build"
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

if ($RunTests) {
    $test = 'ctest --test-dir "{0}" --output-on-failure -C {1}' -f $buildPath, $Configuration
    cmd /c "call `"$vcvars`" >nul && $test"
    exit $LASTEXITCODE
}
