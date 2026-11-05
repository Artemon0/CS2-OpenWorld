<#
.SYNOPSIS
CS2 Open World dependency installer for Windows.

.DESCRIPTION
Проверяет и при необходимости устанавливает всё, что нужно для сборки плагина: MSVC
(Visual Studio C++), CMake, Ninja, Git, PostgreSQL и Redis. Ничего не ставит молча:
перед каждым изменением спрашивает, а если инструмент есть, но не в PATH — просит путь.

.PARAMETER Check
Только показать отчёт, ничего не менять.

.PARAMETER Yes
Не задавать подтверждающих вопросов.

.PARAMETER NoInstall
Никогда не вызывать winget/choco.

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
function Bad([string]$Text) { Write-Host "  [нет] $Text" -ForegroundColor Red; [void]$script:Missing.Add($Text) }

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
    Warn "$Label ($Name) не найден в PATH"
    if ($Check) { return $null }
    if (-not (Ask-YesNo "У тебя уже установлен $Label в другом месте?")) { return $null }
    $provided = Ask-Value "Укажи полный путь к $Name(.exe)" ''
    if ([string]::IsNullOrWhiteSpace($provided)) { return $null }
    if (Test-Path $provided -PathType Container) {
        $provided = Join-Path $provided "$Name.exe"
    }
    if (-not (Test-Path $provided -PathType Leaf)) {
        Bad "$provided не найден"
        return $null
    }
    Ok "$Label : $provided"
    return $provided
}

function Install-Package {
    param([string]$Id)
    if ($NoInstall -or $Check) {
        Warn "нужно установить $Id (например: winget install $Id)"
        return $false
    }
    if (-not (Ask-YesNo "Установить $Id через winget?" $true)) { return $false }
    if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
        Warn "winget не найден; установи вручную: $Id"
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
    if (Test-Path $target) { Ok '.env уже существует'; return }
    if (-not (Test-Path $example)) { Warn '.env.example не найден'; return }
    if ($Check) { Warn '.env будет создан из .env.example'; return }

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
    Ok ".env создан со сгенерированными паролями"
}

Write-Host 'CS2 Open World — установка зависимостей (Windows)'
Write-Host "Репозиторий: $script:RepoRoot"
if ($Check) { Write-Host 'Режим: только проверка (ничего не меняется)' }

Step '1/6 Компилятор MSVC (Visual Studio C++ tools)'
$vcvars = Find-VcVars64
if ($vcvars) {
    Ok "vcvars64.bat: $vcvars"
} else {
    Warn 'MSVC не найден через vswhere'
    if (Ask-YesNo 'У тебя установлена Visual Studio с workload «Desktop development with C++» в нестандартном месте?' $false) {
        $provided = Ask-Value 'Путь к каталогу Visual Studio (где VC\Auxiliary\Build)' ''
        if ($provided) {
            $candidate = Join-Path $provided 'VC\Auxiliary\Build\vcvars64.bat'
            if (Test-Path $candidate) { $vcvars = $candidate; Ok "vcvars64.bat: $candidate" } else { Bad "$candidate не найден" }
        }
    }
    if (-not $vcvars) {
        Bad 'MSVC не найден — без него Windows-сборка невозможна'
        [void](Install-Package 'Microsoft.VisualStudio.2022.BuildTools')
    }
}

Step '2/6 CMake, Ninja, Git'
$cmake = Resolve-Tool -Name 'cmake' -Label 'CMake'
if ($cmake) { Ok "cmake: $cmake" } else {
    Bad 'cmake не найден'
    [void](Install-Package 'Kitware.CMake')
}

$ninja = Resolve-Tool -Name 'ninja' -Label 'Ninja'
if ($ninja) { Ok "ninja: $ninja" } else {
    Warn 'ninja не найден; сборка пойдёт через Visual Studio generator'
    [void](Install-Package 'Ninja-build.Ninja')
}

$git = Resolve-Tool -Name 'git' -Label 'Git'
if ($git) { Ok "git: $git" } else { Bad 'git не найден (нужен для сабмодулей SDK)'; [void](Install-Package 'Git.Git') }

Step '3/6 SDK: hl2sdk-cs2 и metamod-source'
$sdkReady = (Test-Path (Join-Path $script:RepoRoot 'hl2sdk-cs2\game\server')) -and
            (Test-Path (Join-Path $script:RepoRoot 'metamod-source\core'))
if ($sdkReady) {
    Ok 'сабмодули уже на месте'
} elseif ($Check) {
    Warn 'сабмодули не инициализированы (git submodule update --init --recursive)'
} elseif (Ask-YesNo 'Инициализировать сабмодули SDK (git submodule update --init --recursive)?' $true) {
    Push-Location $script:RepoRoot
    try {
        & git submodule update --init --recursive
        if ($LASTEXITCODE -eq 0) { Ok 'сабмодули готовы' } else { Bad 'не удалось инициализировать сабмодули' }
    } finally { Pop-Location }
}

Step '4/6 PostgreSQL'
if ($SkipPostgres) {
    Warn 'пропущено по -SkipPostgres'
} else {
    $psql = Resolve-Tool -Name 'psql' -Label 'psql'
    if ($psql) {
        Ok "psql: $psql"
        if (-not (Ask-YesNo 'Создать роли, базу openworld и накатить миграции сейчас?' $false)) {
            Warn 'База не инициализирована. Вручную: database/bootstrap/001..003.sql и database/migrations/*.sql'
        } else {
            $adminUser = Ask-Value 'Администратор PostgreSQL' 'postgres'
            $adminDb = Ask-Value 'Служебная база для подключения' 'postgres'
            $hostName = Ask-Value 'Хост PostgreSQL' '127.0.0.1'
            $port = Ask-Value 'Порт PostgreSQL' '5432'
            $env:PGPASSWORD = Ask-Value 'Пароль администратора (пусто — через trust/peer)' ''
            $bootstrap = Join-Path $script:RepoRoot 'database\bootstrap\001_create_roles_and_database.sql'
            $createDb = Join-Path $script:RepoRoot 'database\bootstrap\002_create_database.sql'
            & $psql -h $hostName -p $port -U $adminUser -d $adminDb -v ON_ERROR_STOP=1 -q -f $bootstrap
            & $psql -h $hostName -p $port -U $adminUser -d $adminDb -v ON_ERROR_STOP=1 -q -f $createDb
            $migrations = Get-ChildItem (Join-Path $script:RepoRoot 'database\migrations\*.sql') | Sort-Object Name
            foreach ($migration in $migrations) {
                & $psql -h $hostName -p $port -U $adminUser -d $adminDb -v ON_ERROR_STOP=1 -q -f $migration.FullName
                if ($LASTEXITCODE -eq 0) { Ok "migration $($migration.Name)" } else { Bad "migration $($migration.Name) не применилась" }
            }
            Remove-Item Env:\PGPASSWORD -ErrorAction SilentlyContinue
        }
    } else {
        Warn 'psql не найден — PostgreSQL не настроен (плагин подключается через libpq)'
        [void](Install-Package 'PostgreSQL.PostgreSQL.17')
    }
}

Step '5/6 Redis (только для OW_ENABLE_CACHE_FALLBACK=ON)'
if ($SkipRedis) {
    Warn 'пропущено по -SkipRedis'
} elseif (Get-Command redis-server -ErrorAction SilentlyContinue) {
    Ok 'redis-server найден'
} elseif (Get-Command docker -ErrorAction SilentlyContinue) {
    Ok 'redis можно поднять через docker compose (compose.yaml -> сервис redis)'
} else {
    Warn 'Redis не найден; нужен только для экспериментального cache fallback'
}

Step '6/6 .env и сборка'
Write-EnvFile
if ($SkipBuild) {
    Warn 'сборка пропущена по -SkipBuild'
} elseif ($Check) {
    Warn 'сборка не запускалась (режим -Check)'
} elseif (-not $vcvars) {
    Bad 'сборка невозможна без MSVC (vcvars64.bat)'
} elseif (Ask-YesNo 'Собрать плагин и прогнать тесты сейчас?' $true) {
    $buildScript = Join-Path $script:RepoRoot 'build.ps1'
    & $buildScript -Reconfigure -RunTests
    if ($LASTEXITCODE -eq 0) { Ok 'сборка и тесты прошли: build-windows-release\package' } else { Bad 'сборка или тесты упали' }
}

Write-Host "`n==> Итог" -ForegroundColor Cyan
foreach ($item in $script:Missing) { Bad $item }
foreach ($item in ($script:Notes | Select-Object -Unique)) { Warn $item }
Write-Host "`nДальше:"
Write-Host '  * package\ -> addons\cs2-openworld на сервере (см. PACKAGE_INSTALL.txt)'
Write-Host '  * gamedata: python tools\update_offsets.py --check --binary <server.dll>'
Write-Host '  * конфиг: configs\openworld.ini.example -> addons\cs2-openworld\configs\openworld.ini'

if ($script:Missing.Count -gt 0) { exit 1 }
exit 0
