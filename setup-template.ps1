param(
    [string]$Name,
    [string]$Alias,
    [string]$DisplayName,
    [string]$Description,
    [string]$Author,
    [string]$Url,
    [string]$LogTag,
    [string]$License,
    [string]$Version,
    [switch]$Force
)

$ErrorActionPreference = "Stop"

function Get-FieldValue {
    param(
        [string]$Value,
        [string]$Label,
        [string]$DefaultValue,
        [bool]$Required = $true
    )

    if (-not [string]::IsNullOrWhiteSpace($Value)) {
        return $Value.Trim()
    }
    if (-not $Required) {
        return ""
    }

    while ($true) {
        $prompt = $Label
        if (-not [string]::IsNullOrWhiteSpace($DefaultValue)) {
            $prompt += " [$DefaultValue]"
        }
        $answer = Read-Host $prompt
        if ([string]::IsNullOrWhiteSpace($answer)) {
            if (-not [string]::IsNullOrWhiteSpace($DefaultValue)) {
                return $DefaultValue
            }
            if (-not $Required) {
                return ""
            }
            Write-Host "$Label is required."
            continue
        }
        return $answer.Trim()
    }
}

$Name = Get-FieldValue $Name "Plugin name (lowercase identifier)" ""
if ($Name -notmatch '^[a-z0-9_-]+$') {
    throw "Plugin name may contain only lowercase letters, numbers, underscores, and hyphens."
}

$Alias = Get-FieldValue $Alias "Metamod alias" $Name
if ($Alias -notmatch '^[A-Za-z0-9_-]+$') {
    throw "Metamod alias may contain only letters, numbers, underscores, and hyphens."
}

$DisplayName = Get-FieldValue $DisplayName "Display name" $Name
$Description = Get-FieldValue $Description "Description" ""
$Author = Get-FieldValue $Author "Author" ""
$Url = Get-FieldValue $Url "Project URL (optional)" "" $false
$defaultLogTag = $Name.ToUpperInvariant()
$LogTag = Get-FieldValue $LogTag "Log tag" $defaultLogTag
$License = Get-FieldValue $License "License identifier" ""
$Version = Get-FieldValue $Version "Version (major.minor.patch)" "1.0.0"
if ($Version -notmatch '^\d+\.\d+\.\d+$') {
    throw "Version must use major.minor.patch format, for example 1.0.0."
}

$metadataPath = Join-Path $PSScriptRoot "plugin-metadata.json"
if ((Test-Path $metadataPath) -and -not $Force) {
    throw "plugin-metadata.json already exists. Use -Force to overwrite it."
}

$metadata = [ordered]@{
    name = $Name
    alias = $Alias
    display_name = $DisplayName
    description = $Description
    author = $Author
    url = $Url
    log_tag = $LogTag
    license = $License
    version = $Version
}
$json = ConvertTo-Json -InputObject $metadata -Depth 3
[System.IO.File]::WriteAllText(
    $metadataPath,
    $json + [Environment]::NewLine,
    [System.Text.UTF8Encoding]::new($false)
)

Write-Host "Created $metadataPath"
