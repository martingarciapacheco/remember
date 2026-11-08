#requires -Version 7.2
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Installer,
    [Parameter(Mandatory)][string]$Manifest,
    [string]$GitPath,
    [string]$GhPath,
    [switch]$ValidateOnly
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
Import-Module (Join-Path $PSScriptRoot 'ReleaseTools.psm1') -Force
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$git = Resolve-RememberTool 'git.exe' $GitPath @('C:/Program Files/Git/cmd/git.exe')
$source = Get-RememberSource $git $repo
$version = Get-RememberVersion $repo
$repository = Get-RememberRepository $git $repo
$release = Assert-RememberManifest $Manifest $Installer $source $version $repository
$localCommit = ''
$null = & $git -C $repo show-ref --verify --quiet "refs/tags/$($version.tag)"
if ($LASTEXITCODE -eq 0) { $localCommit = Invoke-RememberGit $git $repo @('rev-parse', "$($version.tag)^{commit}") }
elseif ($LASTEXITCODE -ne 1) { throw 'Cannot inspect local version tag' }
$remoteLines = Invoke-RememberGit $git $repo @('ls-remote', '--tags', 'origin', "refs/tags/$($version.tag)", "refs/tags/$($version.tag)^{}")
$remoteCommit = ''
if ($remoteLines) {
    $lines = $remoteLines -split "`n"
    $peeled = @($lines | Where-Object { $_.EndsWith('^{}') })
    $remoteCommit = $(if ($peeled.Count) { $peeled[0] } else { $lines[0] }) -split '\s+' | Select-Object -First 1
}
Assert-RememberTag $source.commit $localCommit $remoteCommit
Write-Host "Validated installer $Installer, tag $($version.tag), commit $($source.commit)"
if ($ValidateOnly) { Write-Host 'Validation only; no tag, push or release created.'; return }
throw 'Single-platform publication is disabled. Use installer/build-installers.ps1 -Publish to build and publish both Windows and macOS installers from the same commit.'
