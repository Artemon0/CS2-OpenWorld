<#
.SYNOPSIS
CS2 Open World dependency installer for Windows.

.DESCRIPTION
Checks and, when needed, installs the dependencies required to build the plugin:
MSVC (Visual Studio C++), CMake, Ninja, Git, PostgreSQL, and Redis. It never
installs anything silently: it asks before each change and asks for a path when a
tool is installed but missing from PATH.

.PARAMETER Check
Show a report only; make no changes.

.PARAMETER Yes
Do not ask for confirmation.

.PARAMETER NoInstall
Never invoke winget or Chocolatey.

.EXAMPLE
.\scripts\install.ps1 -Check

.EXAMPLE
.\scripts\install.ps1
#>
param(
    [switch]$Check,
    [switch]$Yes,
    [switch]$NoInstall,
    [switch]$SkipPostgres,
    [switch]$SkipRedis,
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$script:RepoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$script:Missing = New-Object System.Collections.ArrayList
$script:Notes = New-Object System.Collections.ArrayList

function Step([string]$Text) { Write-Host "`n==> $Text" -ForegroundColor Cyan }
function Ok([string]$Text) { Write-Host "  [ok] $Text" -ForegroundColor Green }
function Warn([string]$Text) { Write-Host "  [!] $Text" -ForegroundColor Yellow; [void]$script:Notes.Add($Text) }
function Bad([string]$Text) { Write-Host "  [missing] $Text" -ForegroundColor Red; [void]$script:Missing.Add($Text) }

function Ask-YesNo {
    param([string]$Question, [bool]$Default = $false)
    if ($Yes -or -not [Environment]::UserInteractive) { return $Default }
    $suffix = if ($Default) { '[Y/n]' } else { '[y/N]' }
    while ($true) {
        $answer = Read-Host "  $Question $suffix"
        if ([string]::IsNullOrWhiteSpace($answer)) { return $Default }
        switch -Regex ($answer) {
            '^(y|yes|д|да)$' { return $true }
            '^(n|no|н|нет)$' { return $false }
        }
    }
}

function Ask-Value {
    param([string]$Question, [string]$Default = '')
    if ($Yes -or -not [Environment]::UserInteractive) { return $Default }
    $answer = Read-Host "  $Question [$Default]"
    if ([string]::IsNullOrWhiteSpace($answer)) { return $Default }
    return $answer.Trim()
}

function Resolve-Tool {
    param([string]$Name, [string]$Label, [string]$VersionArg = '--version')
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    Warn "$Label ($Name) was not found in PATH"
    if ($Check) { return $null }
    if (-not (Ask-YesNo "Is $Label installed in another location?")) { return $null }
    $provided = Ask-Value "Enter the full path to $Name(.exe)" ''
    if ([string]::IsNullOrWhiteSpace($provided)) { return $null }
    if (Test-Path $provided -PathType Container) {
        $provided = Join-Path $provided "$Name.exe"
    }
    if (-not (Test-Path $provided -PathType Leaf)) {
        Bad "$provided was not found"
        return $null
    }
    Ok "$Label : $provided"
    return $provided
}

function Install-Package {
    param([string]$Id)
    if ($NoInstall -or $Check) {
        Warn "Install $Id (for example: winget install $Id)"
        return $false
    }
    if (-not (Ask-YesNo "Install $Id using winget?" $true)) { return $false }
    if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
        Warn "winget was not found; install manually: $Id"
        return $false
    }
    & winget install --id $Id --accept-package-agreements --accept-source-agreements --silent
    return ($LASTEXITCODE -eq 0)
}

function Find-VcVars64 {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $installation = & $vswhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath 2>$null
        if ($installation) {
            $candidate = Join-Path $installation 'VC\Auxiliary\Build\vcvars64.bat'
            if (Test-Path $candidate) { return $candidate }
        }
    }
    return $null
}

function New-Secret {
    $bytes = New-Object byte[] 24
    $generator = [System.Security.Cryptography.RandomNumberGenerator]::Create()
    $generator.GetBytes($bytes)
    ($bytes | ForEach-Object { $_.ToString('x2') }) -join ''
}

function Write-EnvFile {
    $example = Join-Path $script:RepoRoot '.env.example'
    $target = Join-Path $script:RepoRoot '.env'
    if (Test-Path $target) { Ok '.env already exists'; return }
    if (-not (Test-Path $example)) { Warn '.env.example was not found'; return }
    if ($Check) { Warn '.env will be created from .env.example'; return }

    $admin = New-Secret
    $migrator = New-Secret
    $app = New-Secret
    $redis = New-Secret

    $lines = Get-Content -Path $example | ForEach-Object {
        if ($_ -match '^OW_PG_ADMIN_PASSWORD=') { "OW_PG_ADMIN_PASSWORD=$admin" }
        elseif ($_ -match '^OW_PG_MIGRATOR_PASSWORD=') { "OW_PG_MIGRATOR_PASSWORD=$migrator" }
        elseif ($_ -match '^OW_PG_APP_PASSWORD=') { "OW_PG_APP_PASSWORD=$app" }
        elseif ($_ -match '^OW_PG_CONNINFO=') { "OW_PG_CONNINFO=host=127.0.0.1 port=5432 dbname=openworld user=ow_app password=$app sslmode=prefer connect_timeout=3" }
        elseif ($_ -match '^OW_REDIS_PASSWORD=') { "OW_REDIS_PASSWORD=$redis" }
        else { $_ }
    }
    Set-Content -Path $target -Value $lines -Encoding UTF8
    Ok ".env created with generated passwords"
}

Write-Host 'CS2 Open World - dependency installer (Windows)'
Write-Host "Repository: $script:RepoRoot"
if ($Check) { Write-Host 'Mode: check only (no changes will be made)' }

Step '1/6 MSVC compiler (Visual Studio C++ tools)'
$vcvars = Find-VcVars64
if ($vcvars) {
    Ok "vcvars64.bat: $vcvars"
} else {
    Warn 'MSVC was not found through vswhere'
    if (Ask-YesNo 'Is Visual Studio with the "Desktop development with C++" workload installed in a non-standard location?' $false) {
        $provided = Ask-Value 'Path to the Visual Studio directory (containing VC\Auxiliary\Build)' ''
        if ($provided) {
            $candidate = Join-Path $provided 'VC\Auxiliary\Build\vcvars64.bat'
            if (Test-Path $candidate) { $vcvars = $candidate; Ok "vcvars64.bat: $candidate" } else { Bad "$candidate was not found" }
        }
    }
    if (-not $vcvars) {
        Bad 'MSVC was not found; Windows builds require it'
        [void](Install-Package 'Microsoft.VisualStudio.2022.BuildTools')
    }
}

Step '2/6 CMake, Ninja, Git'
$cmake = Resolve-Tool -Name 'cmake' -Label 'CMake'
if ($cmake) { Ok "cmake: $cmake" } else {
    Bad 'cmake was not found'
    [void](Install-Package 'Kitware.CMake')
}

$ninja = Resolve-Tool -Name 'ninja' -Label 'Ninja'
if ($ninja) { Ok "ninja: $ninja" } else {
    Warn 'ninja was not found; the build will use the Visual Studio generator'
    [void](Install-Package 'Ninja-build.Ninja')
}

$git = Resolve-Tool -Name 'git' -Label 'Git'
if ($git) { Ok "git: $git" } else { Bad 'git was not found (required for SDK submodules)'; [void](Install-Package 'Git.Git') }

Step '3/6 SDK: hl2sdk-cs2 and metamod-source'
$sdkReady = (Test-Path (Join-Path $script:RepoRoot 'hl2sdk-cs2\game\server')) -and
            (Test-Path (Join-Path $script:RepoRoot 'metamod-source\core'))
if ($sdkReady) {
    Ok 'Submodules are already available'
} elseif ($Check) {
    Warn 'Submodules are not initialized (git submodule update --init --recursive)'
} elseif (Ask-YesNo 'Initialize SDK submodules (git submodule update --init --recursive)?' $true) {
    Push-Location $script:RepoRoot
    try {
        & git submodule update --init --recursive
        if ($LASTEXITCODE -ne 0) {
            # .gitmodules marks the SDK submodules as shallow; a pinned commit that is no longer the
            # branch tip cannot be fetched that way, so retry with full history.
            & git config --unset submodule.hl2sdk-cs2.shallow 2>$null
            & git config --unset submodule.metamod-source.shallow 2>$null
            & git submodule update --init --recursive
        }
        if ($LASTEXITCODE -eq 0) { Ok 'Submodules are ready' } else { Bad 'Failed to initialize submodules' }
    } finally { Pop-Location }
}

Step '4/6 PostgreSQL'
if ($SkipPostgres) {
    Warn 'Skipped by -SkipPostgres'
} else {
    $psql = Resolve-Tool -Name 'psql' -Label 'psql'
    if ($psql) {
        Ok "psql: $psql"
        if (-not (Ask-YesNo 'Create the roles and openworld database, then apply migrations now?' $false)) {
            Warn 'Database not initialized. Run database/bootstrap/001..003.sql and database/migrations/*.sql manually'
        } else {
            $adminUser = Ask-Value 'PostgreSQL administrator' 'postgres'
            $adminDb = Ask-Value 'Maintenance database for the connection' 'postgres'
            $hostName = Ask-Value 'PostgreSQL host' '127.0.0.1'
            $port = Ask-Value 'PostgreSQL port' '5432'
            $env:PGPASSWORD = Ask-Value 'Administrator password (empty to use trust/peer)' ''
            $bootstrap = Join-Path $script:RepoRoot 'database\bootstrap\001_create_roles_and_database.sql'
            $createDb = Join-Path $script:RepoRoot 'database\bootstrap\002_create_database.sql'
            & $psql -h $hostName -p $port -U $adminUser -d $adminDb -v ON_ERROR_STOP=1 -q -f $bootstrap
            & $psql -h $hostName -p $port -U $adminUser -d $adminDb -v ON_ERROR_STOP=1 -q -f $createDb
            $migrations = Get-ChildItem (Join-Path $script:RepoRoot 'database\migrations\*.sql') | Sort-Object Name
            foreach ($migration in $migrations) {
                & $psql -h $hostName -p $port -U $adminUser -d $adminDb -v ON_ERROR_STOP=1 -q -f $migration.FullName
                if ($LASTEXITCODE -eq 0) { Ok "Migration $($migration.Name)" } else { Bad "Migration $($migration.Name) failed" }
            }
            Remove-Item Env:\PGPASSWORD -ErrorAction SilentlyContinue
        }
    } else {
        Warn 'psql was not found; PostgreSQL is not configured (the plugin connects through libpq)'
        [void](Install-Package 'PostgreSQL.PostgreSQL.17')
    }
}

Step '5/6 Redis (only for OW_ENABLE_CACHE_FALLBACK=ON)'
if ($SkipRedis) {
    Warn 'Skipped by -SkipRedis'
} elseif (Get-Command redis-server -ErrorAction SilentlyContinue) {
    Ok 'redis-server was found'
} elseif (Get-Command docker -ErrorAction SilentlyContinue) {
    Ok 'Redis can be started with docker compose (compose.yaml -> redis service)'
} else {
    Warn 'Redis was not found; it is only needed for the experimental cache fallback'
}

Step '6/6 .env and build'
Write-EnvFile
if ($SkipBuild) {
    Warn 'Build skipped by -SkipBuild'
} elseif ($Check) {
    Warn 'Build not run (check mode)'
} elseif (-not $vcvars) {
    Bad 'Cannot build without MSVC (vcvars64.bat)'
} elseif (Ask-YesNo 'Build the plugin and run tests now?' $true) {
    $buildScript = Join-Path $script:RepoRoot 'build.ps1'
    & $buildScript -Reconfigure -RunTests
    if ($LASTEXITCODE -eq 0) { Ok 'Build and tests passed: build-windows-release\package' } else { Bad 'Build or tests failed' }
}

Write-Host "`n==> Summary" -ForegroundColor Cyan
foreach ($item in $script:Missing) { Bad $item }
foreach ($item in ($script:Notes | Select-Object -Unique)) { Warn $item }
Write-Host "`nNext steps:"
Write-Host '  * Copy package\ -> addons\cs2-openworld on the server (see PACKAGE_INSTALL.txt)'
Write-Host '  * Check gamedata: python tools\update_offsets.py --check --binary <server.dll>'
Write-Host '  * Config: configs\openworld.ini.example -> addons\cs2-openworld\configs\openworld.ini'

if ($script:Missing.Count -gt 0) { exit 1 }
exit 0
