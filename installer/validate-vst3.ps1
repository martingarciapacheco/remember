#requires -Version 7.2
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$PluginPath,
    [Parameter(Mandatory)][string]$PluginvalPath,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [ValidateRange(5, 10)][int]$Strictness = 5
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'ReleaseTools.psm1') -Force
$lock = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'dependencies.json') -Raw | ConvertFrom-Json
$validator = Resolve-RememberTool 'pluginval.exe' $PluginvalPath
if ((Get-FileHash -LiteralPath $validator -Algorithm SHA256).Hash -ine $lock.pluginval.executableSha256) {
    throw 'pluginval executable differs from pinned dependencies.json'
}
$plugin = (Resolve-Path -LiteralPath $PluginPath).Path
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $output) { throw 'Validation output must be a new directory; stale logs cannot prove validation' }
New-Item -ItemType Directory -Path $output | Out-Null
$profile = Join-Path $output 'profile'
New-Item -ItemType Directory -Path $profile | Out-Null
$previews = Join-Path $profile 'previews'
$projects = Join-Path $profile 'projects'
New-Item -ItemType Directory -Path $previews, $projects | Out-Null
[IO.File]::WriteAllText((Join-Path $profile '.remember-validation-profile'), "Remember isolated validation profile v1`n")
Write-RememberJson (Join-Path $profile 'config.json') @{
    previewsFolder = $previews; projectsRootFolders = @($projects); recordSeconds = 15; oggQuality = 4; mono = $false
}
$savedProfile = $env:REMEMBER_VALIDATION_PROFILE
try {
    $env:REMEMBER_VALIDATION_PROFILE = $profile
    Invoke-RememberProcess $validator @('--strictness-level', "$Strictness", '--timeout-ms', '120000',
        '--random-seed', '19731007', '--sample-rates', '44100,48000,88200,96000,192000',
        '--block-sizes', '32,64,128,512,2048', '--output-dir', $output,
        '--output-filename', 'pluginval-detail.txt', '--validate', $plugin) (Join-Path $output 'pluginval.log')
} finally { $env:REMEMBER_VALIDATION_PROFILE = $savedProfile }
$pluginLog = Join-Path $profile 'logs/plugin.log'
if (!(Test-Path -LiteralPath $pluginLog -PathType Leaf) -or
    !(Select-String -LiteralPath $pluginLog -SimpleMatch 'Remember' -Quiet)) {
    throw 'Native VST3 did not initialise the isolated profile logger'
}
$detail = Join-Path $output 'pluginval-detail.txt'
if (!(Test-Path -LiteralPath $detail -PathType Leaf)) { throw 'pluginval detailed result log is missing' }
# A Windows GUI subsystem build may emit no stdout, despite writing its
# detailed result log. Keep the latter as the required validation evidence.
if ((Get-Item -LiteralPath (Join-Path $output 'pluginval.log')).Length -eq 0) {
    Copy-Item -LiteralPath $detail -Destination (Join-Path $output 'pluginval.log')
}
Assert-RememberNativeLog $detail $Strictness
Write-RememberJson (Join-Path $output 'validation.json') @{
    passed = $true; strictness = $Strictness; isolatedProfile = $true;
    validatorVersion = $lock.pluginval.version; validatorSha256 = $lock.pluginval.executableSha256;
    sampleRates = @(44100, 48000, 88200, 96000, 192000); blockSizes = @(32, 64, 128, 512, 2048)
}
