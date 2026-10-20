#requires -Version 7.2
[CmdletBinding()]
param([Parameter(Mandatory)][string]$TestExecutable)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$root = Join-Path ([IO.Path]::GetTempPath()) ('Remember validation profile ' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root | Out-Null
$marker = Join-Path $root '.remember-validation-profile'
$config = Join-Path $root 'config.json'
$previews = Join-Path $root 'previews'
$projects = Join-Path $root 'projects'
New-Item -ItemType Directory -Path $previews, $projects | Out-Null
function Valid-Config {
    [IO.File]::WriteAllText($config, (@{ previewsFolder = $previews; projectsRootFolders = @($projects) } | ConvertTo-Json))
}
function Probe([bool]$Valid, [string]$Expected) {
    $text = (& $TestExecutable --validation-profile-probe 2>&1) -join "`n"
    $code = $LASTEXITCODE
    if (($Valid -and ($code -ne 0 -or $text.Trim() -ine $root)) -or
        (!$Valid -and ($code -ne 2 -or !$text.Contains($Expected)))) {
        throw "Profile isolation failed: expected '$Expected', exit $code, output '$text'"
    }
    $script:checks++
}
$script:checks = 0
$saved = $env:REMEMBER_VALIDATION_PROFILE
try {
    $env:REMEMBER_VALIDATION_PROFILE = $root
    Valid-Config
    Probe $false 'validation marker'
    [IO.File]::WriteAllText($marker, 'wrong marker')
    Probe $false 'validation marker'
    [IO.File]::WriteAllText($marker, "Remember isolated validation profile v1`n")
    Probe $true $root
    [IO.File]::WriteAllText($config, '{}')
    Probe $false 'Validation preview folder'
    [IO.File]::WriteAllText($config, (@{ previewsFolder = [IO.Path]::GetTempPath(); projectsRootFolders = @($projects) } | ConvertTo-Json))
    Probe $false 'Validation preview folder'
    [IO.File]::WriteAllText($config, (@{ previewsFolder = $previews; projectsRootFolders = @([IO.Path]::GetTempPath()) } | ConvertTo-Json))
    Probe $false 'project root is outside'
    Valid-Config
    $env:REMEMBER_VALIDATION_PROFILE = 'relative/profile'
    Probe $false 'absolute isolated directory'
} finally { $env:REMEMBER_VALIDATION_PROFILE = $saved }
Write-Host "PASS: $script:checks isolated validation profile cases, including refusal of invalid overrides"
exit 0
