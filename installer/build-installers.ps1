#requires -Version 7.2
[CmdletBinding()]
param([string]$Ref = 'main', [switch]$Publish)
$ErrorActionPreference = 'Stop'
$repo = Join-Path $PSScriptRoot '..'
Push-Location -LiteralPath $repo
try {
    & gh workflow run release-installers.yml --repo martingarciapacheco/remember --ref $Ref -f "publish=$($Publish.IsPresent.ToString().ToLowerInvariant())"
    if ($LASTEXITCODE) { throw 'Cannot start the paired Windows/macOS installer workflow' }
    Write-Host 'Windows and macOS installers requested together. Follow Release installers in GitHub Actions.'
} finally { Pop-Location }
