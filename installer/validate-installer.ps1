#requires -Version 7.2
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Installer,
    [Parameter(Mandatory)][string]$Payload,
    [Parameter(Mandatory)][string]$UnpackerPath,
    [Parameter(Mandatory)][string]$OutputDirectory
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'ReleaseTools.psm1') -Force
$lock = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'dependencies.json') -Raw | ConvertFrom-Json
$unpacker = Resolve-RememberTool 'innounp.exe' $UnpackerPath
if ((Get-FileHash -LiteralPath $unpacker -Algorithm SHA256).Hash -ine $lock.innounp.executableSha256) {
    throw 'Installer unpacker differs from dependencies.json'
}
$installerPath = (Resolve-Path -LiteralPath $Installer).Path
$payloadPath = (Resolve-Path -LiteralPath $Payload).Path
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $output) { throw 'Installer validation output must be new' }
New-Item -ItemType Directory -Path $output | Out-Null
$extracted = Join-Path $output 'extracted'
$version = Get-RememberVersion (Join-Path $PSScriptRoot '..')
$hashBefore = (Get-FileHash -LiteralPath $installerPath -Algorithm SHA256).Hash.ToLowerInvariant()
$resource = (Get-Item -LiteralPath $installerPath).VersionInfo
$numeric = "$($resource.FileMajorPart).$($resource.FileMinorPart).$($resource.FileBuildPart).$($resource.FilePrivatePart)"
$productNumeric = "$($resource.ProductMajorPart).$($resource.ProductMinorPart).$($resource.ProductBuildPart).$($resource.ProductPrivatePart)"
if ($numeric -cne $version.windows -or $productNumeric -cne $version.windows) { throw 'Installer Windows version differs from VERSION' }
# Only the pinned unpacker is executed. The setup executable is treated as
# archive input; neither Remember nor its uninstaller is launched here.
Invoke-RememberProcess $unpacker @('-x', '-b', '-q', '-a', '-u', "-d$extracted", $installerPath) (Join-Path $output 'extractor.log')
$files = @(Assert-RememberInstallerContents $payloadPath $extracted $version)
if ((Get-FileHash -LiteralPath $installerPath -Algorithm SHA256).Hash -ine $hashBefore) { throw 'Installer changed during extraction validation' }
Write-RememberJson (Join-Path $output 'installer-validation.json') ([ordered]@{
    schema = 1; passed = $true; method = 'archive extraction and byte comparison';
    installerSha256 = $hashBefore; windowsVersion = $numeric;
    extractorVersion = $lock.innounp.version; extractorSha256 = $lock.innounp.executableSha256;
    payload = $files; installationAcceptance = 'pending isolated Windows installation/upgrade/uninstallation'
})
Write-Host "PASS: installer version, identity, architecture and $($files.Count) extracted payload files"
