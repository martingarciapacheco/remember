#requires -Version 7.2
[CmdletBinding()]
param([Parameter(Mandatory)][string]$OutputDirectory)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'ReleaseTools.psm1') -Force
$lock = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'dependencies.json') -Raw | ConvertFrom-Json
$root = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $root) { throw 'Packaging tools directory must be new' }
New-Item -ItemType Directory -Path $root | Out-Null
$setup = Join-Path $root 'inno-setup.exe'
Invoke-WebRequest $lock.inno.url -OutFile $setup
if ((Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash -ine $lock.inno.archiveSha256) { throw 'Inno Setup download checksum mismatch' }
$compiler = Join-Path $root 'compiler'
# The official compiler distribution's portable mode disables uninstall
# registration, associations and shortcuts. No Remember installer is run.
Invoke-RememberProcess $setup @('/PORTABLE=1', '/VERYSILENT', '/SUPPRESSMSGBOXES', '/SP-',
    '/NORESTART', '/CURRENTUSER', '/NOICONS', '/TASKS=', "/DIR=$compiler", "/LOG=$(Join-Path $root 'portable-setup.log')") (Join-Path $root 'portable-process.log')
$iscc = Join-Path $compiler 'ISCC.exe'
$null = Get-RememberInstallerCompiler $iscc
if (Test-Path -LiteralPath (Join-Path $compiler 'unins000.exe')) { throw 'Compiler distribution did not use portable mode' }
$archive = Join-Path $root 'innounp.zip'
Invoke-WebRequest $lock.innounp.url -OutFile $archive
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ine $lock.innounp.archiveSha256) { throw 'Installer unpacker download checksum mismatch' }
$extractor = Join-Path $root 'extractor'
Expand-Archive -LiteralPath $archive -DestinationPath $extractor
$unpacker = Join-Path $extractor 'innounp.exe'
if ((Get-FileHash -LiteralPath $unpacker -Algorithm SHA256).Hash -ine $lock.innounp.executableSha256) { throw 'Installer unpacker executable checksum mismatch' }
[pscustomobject]@{ iscc = $iscc; unpacker = $unpacker }
