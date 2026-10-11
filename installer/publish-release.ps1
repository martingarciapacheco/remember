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
$gh = Resolve-RememberTool 'gh.exe' $GhPath @('C:/Program Files/GitHub CLI/gh.exe')
Assert-RememberSourceUnchanged $source (Get-RememberSource $git $repo)
if ((Get-RememberRepository $git $repo) -ine $repository) { throw 'Release origin changed during validation' }
if (!$localCommit) { $null = Invoke-RememberGit $git $repo @('tag', '-a', $version.tag, $source.commit, '-m', "Release $($version.tag)") }
$actualTag = Invoke-RememberGit $git $repo @('rev-parse', "$($version.tag)^{commit}")
Assert-RememberTag $source.commit $actualTag $remoteCommit
$null = Invoke-RememberGit $git $repo @('push', 'origin', "refs/tags/$($version.tag):refs/tags/$($version.tag)")
$root = Split-Path ([IO.Path]::GetFullPath($Manifest)) -Parent
$notesArtifact = @($release.artifacts | Where-Object role -eq 'notes')[0]
$notes = Resolve-RememberArtifact $root $notesArtifact.path
$releaseArgs = Get-RememberReleaseArguments $repository $version $notes $Installer $Manifest
Push-Location $repo
try {
    & $gh @releaseArgs
    if ($LASTEXITCODE -ne 0) { throw 'GitHub release creation failed; no success reported' }
} finally { Pop-Location }
