#requires -Version 7.2
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot '../installer/ReleaseTools.psm1') -Force
$git = Resolve-RememberTool 'git.exe' '' @('C:/Program Files/Git/cmd/git.exe')
$root = Join-Path ([IO.Path]::GetTempPath()) ('Remember release contracts ' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root | Out-Null
$script:checks = 0
function Check([bool]$Passed, [string]$Message) {
    if (!$Passed) { throw $Message }; $script:checks++
}
function Reject([scriptblock]$Action, [string]$Expected) {
    $rejected = $false
    try { & $Action | Out-Null } catch {
        if ($_.Exception.Message -notlike "*$Expected*") { throw "Unexpected rejection: $($_.Exception.Message), expected $Expected" }
        $rejected = $true
    }
    Check $rejected "Accepted invalid release: $Expected"
}
function Write-Fixture([string]$Name, [string]$Text) {
    $path = Join-Path $root $Name
    $parent = Split-Path $path -Parent
    if (!(Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent | Out-Null }
    [IO.File]::WriteAllText($path, $Text, [Text.UTF8Encoding]::new($false))
    return $path
}

$repo = Join-Path $root 'source'
New-Item -ItemType Directory -Path $repo | Out-Null
$null = Invoke-RememberGit $git $repo @('init')
[IO.File]::WriteAllText((Join-Path $repo 'VERSION'), '1.0.0-beta3')
[IO.File]::WriteAllText((Join-Path $repo '.gitignore'), "out/`n")
[IO.File]::WriteAllText((Join-Path $repo 'source with spaces ñ.cpp'), 'before')
$null = Invoke-RememberGit $git $repo @('add', '.')
$null = Invoke-RememberGit $git $repo @('-c', 'user.name=Remember fixture', '-c', 'user.email=fixture@example.invalid',
    'commit', '-m', 'Isolated release fixture')
$baseline = Get-RememberSource $git $repo
Check ($baseline.state -eq 'clean') 'Fixture did not start clean'
$null = Invoke-RememberGit $git $repo @('remote', 'add', 'origin', 'https://github.com/remember-fixture/release.git')
$repository = Get-RememberRepository $git $repo
foreach ($url in @('https://github.com/remember-fixture/release.git', 'https://github.com/remember-fixture/release',
    'git@github.com:remember-fixture/release.git', 'ssh://git@github.com/remember-fixture/release.git')) {
    Check ((Get-RememberRepositoryFromUrl $url) -ceq $repository) 'GitHub repository URL mapping failed'
}
foreach ($url in @('http://github.com/owner/repo', 'https://other.invalid/owner/repo',
    'https://token@github.com/owner/repo', 'https://github.com/owner/repo?redirect=other',
    'https://github.com/owner/../repo', 'https://github.com/owner/..', 'https://github.com/owner/repo/extra')) {
    Reject { Get-RememberRepositoryFromUrl $url } 'Release origin must identify'
}
$null = Invoke-RememberGit $git $repo @('config', 'remote.origin.pushurl', 'https://github.com/other-fixture/release.git')
Reject { Get-RememberRepository $git $repo } 'repositories differ'
$null = Invoke-RememberGit $git $repo @('config', '--add', 'remote.origin.pushurl', 'https://github.com/remember-fixture/release.git')
Reject { Get-RememberRepository $git $repo } 'exactly one'
$null = Invoke-RememberGit $git $repo @('config', '--unset-all', 'remote.origin.pushurl')
$null = Invoke-RememberGit $git $repo @('config', '--add', 'remote.origin.url', 'https://github.com/other-fixture/release.git')
Reject { Get-RememberRepository $git $repo } 'exactly one'
$null = Invoke-RememberGit $git $repo @('config', '--unset-all', 'remote.origin.url')
$null = Invoke-RememberGit $git $repo @('config', 'remote.origin.url', 'https://github.com/remember-fixture/release.git')
New-Item -ItemType Directory -Path (Join-Path $repo 'out') | Out-Null
[IO.File]::WriteAllText((Join-Path $repo 'out/ignored.log'), 'generated')
Assert-RememberSourceUnchanged $baseline (Get-RememberSource $git $repo)
$script:checks++
[IO.File]::WriteAllText((Join-Path $repo 'source with spaces ñ.cpp'), 'after!')
$changed = Get-RememberSource $git $repo
Check ($changed.state -eq 'dirty' -and $changed.fingerprint -ne $baseline.fingerprint) 'Same-size source edit was missed'
Reject { Assert-RememberSourceUnchanged $baseline $changed } 'Source changed'
[IO.File]::WriteAllText((Join-Path $repo 'source with spaces ñ.cpp'), 'before')
[IO.File]::WriteAllText((Join-Path $repo 'new untracked.cpp'), 'new')
Check ((Get-RememberSource $git $repo).fingerprint -ne $baseline.fingerprint) 'Untracked source omitted'
Remove-Item -LiteralPath (Join-Path $repo 'new untracked.cpp')
Remove-Item -LiteralPath (Join-Path $repo 'source with spaces ñ.cpp')
Check ((Get-RememberSource $git $repo).fingerprint -ne $baseline.fingerprint) 'Deleted source omitted'
[IO.File]::WriteAllText((Join-Path $repo 'source with spaces ñ.cpp'), 'before')
Assert-RememberSourceUnchanged $baseline (Get-RememberSource $git $repo)
$version = Get-RememberVersion $repo
Check ($version.windows -eq '1.0.0.3' -and $version.tag -eq 'v1.0.0-beta3') 'Shared version mapping failed'
$oldGhRepo = $env:GH_REPO
try {
    $env:GH_REPO = 'github.com/unrelated/repository'
    $arguments = @(Get-RememberReleaseArguments $repository $version 'notes with spaces.md' 'setup.exe' 'manifest.json')
    Check ($arguments[$arguments.IndexOf('--repo') + 1] -ceq $repository -and $arguments.Contains('--verify-tag') -and
        $arguments.Contains('--prerelease') -and $arguments.Contains('notes with spaces.md')) 'Release arguments depend on implicit gh selection'
} finally { $env:GH_REPO = $oldGhRepo }
[IO.File]::WriteAllText((Join-Path $repo 'VERSION'), '1.0.0')
Check ((Get-RememberVersion $repo).windows -eq '1.0.0.0') 'Stable version mapping failed'
[IO.File]::WriteAllText((Join-Path $repo 'VERSION'), 'bad-version')
Reject { Get-RememberVersion $repo } 'Invalid VERSION'
[IO.File]::WriteAllText((Join-Path $repo 'VERSION'), '1.0.0-beta3')
Reject { Assert-RememberTag $baseline.commit ('0' * 40) '' } 'another commit'
Reject { Assert-RememberTag $baseline.commit '' ('1' * 40) } 'another commit'
Assert-RememberTag $baseline.commit $baseline.commit $baseline.commit
$script:checks++

$pluginTests = @('RememberPreviewQuality', 'RememberResampling', 'RememberFeatureCache', 'RememberAssociation',
    'RememberRegression', 'RememberConfiguration', 'RememberStorageRecovery', 'RememberSustainedCapture',
    'RememberAnalysis', 'RememberCaptureMatrix', 'RememberHostLoading', 'RememberValidationIsolation')
$appTests = @('RememberAppRegression', 'RememberAppStorageRecovery', 'RememberLibraryBenchmark', 'RememberAppColdStartup', 'RememberAppShell')
foreach ($suite in @('plugin', 'app')) {
    $names = if ($suite -eq 'plugin') { $pluginTests } else { $appTests }
    $xml = '<testsuite>' + (($names | ForEach-Object { "<testcase name=`"$_`" status=`"run`" time=`"1`"/>" }) -join '') + '</testsuite>'
    $path = Write-Fixture "$suite-tests.xml" $xml
    Check ((Get-RememberTestEvidence $path $suite).passed) 'Valid test evidence rejected'
    foreach ($requiredName in $names) {
        [IO.File]::WriteAllText($path, $xml.Replace("<testcase name=`"$requiredName`" status=`"run`" time=`"1`"/>", ''))
        Reject { Get-RememberTestEvidence $path $suite } 'Missing required CTest'
    }
    foreach ($bad in @('failure', 'skipped', 'error')) {
        [IO.File]::WriteAllText($path, $xml.Replace('time="1"/>', "time=`"1`"><$bad/></testcase>"))
        Reject { Get-RememberTestEvidence $path $suite } 'failed or skipped'
    }
    [IO.File]::WriteAllText($path, $xml.Replace('</testsuite>', "<testcase name=`"$($names[0])`"/></testsuite>"))
    Reject { Get-RememberTestEvidence $path $suite } 'Duplicate CTest'
    [IO.File]::WriteAllText($path, $xml)
}
$buildId = 'contract-20261007'
$info = @{ schema = 1; productVersion = $version.version; numericVersion = $version.numeric; windowsVersion = $version.windows;
    commit = $baseline.commit; sourceState = 'clean'; sourceFingerprint = $baseline.fingerprint; buildId = $buildId }
foreach ($suite in @('plugin', 'app')) { Write-RememberJson (Join-Path $root "$suite-build-info.json") $info }
$installer = Write-Fixture 'Remember-1.0.0-beta3-setup.exe' 'installer fixture only'
$embedded = "Remember build $buildId source $($baseline.fingerprint) commit $($baseline.commit) state $($baseline.state)"
$app = Write-Fixture 'payload/Remember.exe' $embedded
$vst = Write-Fixture 'payload/Remember.vst3/Contents/x86_64-win/Remember.vst3' $embedded
$notes = Write-Fixture 'payload/BETA_NOTES.md' 'Validated notes'
$moduleInfo = Write-Fixture 'payload/Remember.vst3/Contents/Resources/moduleinfo.json' '{"Name":"Remember"}'
$provenance = Write-Fixture 'payload/build-provenance.json' '{"schema":1}'
$nativeText = "Strictness level: 5`nNum plugins found: 1`nCompleted tests in pluginval / Editor`nCompleted tests in pluginval / Plugin state`nSUCCESS`n"
foreach ($rate in @(44100, 48000, 88200, 96000, 192000)) {
    foreach ($block in @(32, 64, 128, 512, 2048)) { $nativeText += "Testing with sample rate [$rate] and block size [$block]`n" }
}
$nativeLog = Write-Fixture 'validation/pluginval.log' $nativeText
Reject { Assert-RememberNativeLog $app 5 } 'does not prove'
$lock = Get-Content -LiteralPath (Join-Path $PSScriptRoot '../installer/dependencies.json') -Raw | ConvertFrom-Json
Reject { Get-RememberInstallerCompiler $app } 'pinned ISCC.exe'
$extracted = Join-Path $root 'extracted'
$payload = Join-Path $root 'payload'
foreach ($file in Get-RememberInstallerPayload $payload) {
    $null = Write-Fixture "extracted/$($file.destination)" ([IO.File]::ReadAllText((Join-Path $root $file.path)))
}
$setupText = "AppId={{C3F5A6E2-9F2D-4C8A-9F0E-1D2B3C4E5F60}}`nAppVersion=1.0.0-beta3`nArchitecturesAllowed=x64compatible`nArchitecturesInstallIn64BitMode=x64compatible`n"
$setupScript = Write-Fixture 'extracted/install_script.iss' $setupText
Check (@(Assert-RememberInstallerContents $payload $extracted $version).Count -eq 5) 'Valid installer extraction rejected'
$extractedApp = Join-Path $extracted '{app}/Remember.exe'
[IO.File]::WriteAllText($extractedApp, 'corrupt')
Reject { Assert-RememberInstallerContents $payload $extracted $version } 'payload differs'
[IO.File]::WriteAllText($extractedApp, $embedded)
$extraExtracted = Write-Fixture 'extracted/{app}/extra.exe' 'unexpected'
Reject { Assert-RememberInstallerContents $payload $extracted $version } 'Unexpected extracted'
Remove-Item -LiteralPath $extraExtracted
[IO.File]::WriteAllText($setupScript, $setupText.Replace('beta3', 'beta2'))
Reject { Assert-RememberInstallerContents $payload $extracted $version } 'setup metadata differs'
[IO.File]::WriteAllText($setupScript, $setupText)
$installerEvidence = @{ schema = 1; passed = $true; installerSha256 = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant();
    extractorSha256 = $lock.innounp.executableSha256; payload = @(Get-RememberInstallerPayload $payload) }
$packagingReport = Join-Path $root 'installer-validation.json'
Write-RememberJson $packagingReport $installerEvidence
$manifest = @{ schema = 1; version = $version.version; tag = $version.tag; buildId = $buildId;
    repository = $repository; installerCompiler = @{ version = $lock.inno.version; sha256 = $lock.inno.files.'ISCC.exe'; files = $lock.inno.files };
    packaged = $true; publishable = $true; configuration = 'Release'; architecture = 'x64'; source = $baseline;
    dependencies = @{ juce = @{ commit = $lock.juce.commit; clean = $true } };
    validation = @{ nativeVst3 = @{ passed = $true; strictness = 5; isolatedProfile = $true;
        validatorSha256 = $lock.pluginval.executableSha256 } }; artifacts = @(
        (Get-RememberArtifact $root $installer 'installer'), (Get-RememberArtifact $root $app 'app'),
        (Get-RememberArtifact $root $vst 'vst3'), (Get-RememberArtifact $root $notes 'notes'),
        (Get-RememberArtifact $root $nativeLog 'pluginval-log'),
        (Get-RememberArtifact $root $moduleInfo 'evidence'), (Get-RememberArtifact $root $provenance 'evidence'),
        (Get-RememberArtifact $root $packagingReport 'installer-validation'),
        (Get-RememberArtifact $root (Join-Path $root 'plugin-tests.xml') 'plugin-test-results'),
        (Get-RememberArtifact $root (Join-Path $root 'app-tests.xml') 'app-test-results'),
        (Get-RememberArtifact $root (Join-Path $root 'plugin-build-info.json') 'plugin-build-info'),
        (Get-RememberArtifact $root (Join-Path $root 'app-build-info.json') 'app-build-info')) }
$manifestPath = Join-Path $root 'release-manifest.json'
Write-RememberJson $manifestPath $manifest
$original = [IO.File]::ReadAllText($manifestPath)
Check ((Assert-RememberManifest $manifestPath $installer $baseline $version $repository).publishable) 'Valid fixture release rejected'
function Reset-Manifest { [IO.File]::WriteAllText($manifestPath, $original) }
function Change-Manifest([scriptblock]$Mutation) {
    $value = $original | ConvertFrom-Json
    & $Mutation $value
    Write-RememberJson $manifestPath $value
}
foreach ($file in @($installer, $app, $vst, $notes, $nativeLog)) {
    $bytes = [IO.File]::ReadAllBytes($file)
    [IO.File]::WriteAllText($file, 'modified after validation')
    Reject { Assert-RememberManifest $manifestPath $installer $baseline $version $repository } 'content differs'
    [IO.File]::WriteAllBytes($file, $bytes)
}
Change-Manifest { param($m) $m.publishable = $false }
Reject { Assert-RememberManifest $manifestPath $installer $baseline $version $repository } 'Publication requires'
Check (!(Assert-RememberManifest $manifestPath $installer $baseline $version $repository -AllowDevelopment).publishable) 'Development installer rejected'
Change-Manifest { param($m) $m.publishable = $false; $m.dependencies.juce.commit = 'archive'; $m.dependencies.juce.clean = $false }
Check (!(Assert-RememberManifest $manifestPath $installer $baseline $version $repository -AllowDevelopment).publishable) 'Development archive dependency rejected'
Change-Manifest { param($m) $m.publishable = $false; $m.validation.nativeVst3.passed = $false }
Reject { Assert-RememberManifest $manifestPath $installer $baseline $version $repository -AllowDevelopment } 'native VST3'
Reset-Manifest
Reject { Assert-RememberManifest $manifestPath $installer $baseline $version $repository -AllowDevelopment } 'explicitly unpublishable'
Change-Manifest { param($m) $m.repository = 'github.com/unrelated/release' }
Reject { Assert-RememberManifest $manifestPath $installer $baseline $version $repository } 'repository differs'
Change-Manifest { param($m) $m.installerCompiler.version = 'wrong' }
Reject { Assert-RememberManifest $manifestPath $installer $baseline $version $repository } 'compiler provenance'
Change-Manifest { param($m) $m.installerCompiler.files.'ISCmplr.dll' = '0' * 64 }
Reject { Assert-RememberManifest $manifestPath $installer $baseline $version $repository } 'compiler dependency provenance'
Change-Manifest { param($m) $m.source.fingerprint = '0' * 64 }
Reject { Assert-RememberManifest $manifestPath $installer $baseline $version $repository } 'Source changed'
Change-Manifest { param($m) $m.tag = 'v9.9.9' }
Reject { Assert-RememberManifest $manifestPath $installer $baseline $version $repository } 'version differs'
Change-Manifest { param($m) $m.validation.nativeVst3.passed = $false }
Reject { Assert-RememberManifest $manifestPath $installer $baseline $version $repository } 'native VST3'
Change-Manifest { param($m) $m.validation.nativeVst3.strictness = 4 }
Reject { Assert-RememberManifest $manifestPath $installer $baseline $version $repository } 'native VST3'
Change-Manifest { param($m) $m.dependencies.juce.clean = $false }
Reject { Assert-RememberManifest $manifestPath $installer $baseline $version $repository } 'native VST3'
Change-Manifest { param($m) $m.artifacts += $m.artifacts[0] }
Reject { Assert-RememberManifest $manifestPath $installer $baseline $version $repository } 'Duplicate artifact'
Change-Manifest { param($m) $m.artifacts = @($m.artifacts | Where-Object role -ne 'vst3') }
Reject { Assert-RememberManifest $manifestPath $installer $baseline $version $repository } 'Required artifact role'
Change-Manifest { param($m) $m.artifacts[0].path = '../escape.exe' }
Reject { Assert-RememberManifest $manifestPath $installer $baseline $version $repository } 'Invalid manifest artifact path'
Reset-Manifest
$installerEvidence.installerSha256 = '0' * 64
Reject { Assert-RememberInstallerEvidence $installerEvidence $manifest.artifacts[0] $manifest.artifacts $lock } 'extraction validation'
$installerEvidence.installerSha256 = $manifest.artifacts[0].sha256
$installerEvidence.payload = @($installerEvidence.payload | Where-Object path -ne 'payload/Remember.exe')
Reject { Assert-RememberInstallerEvidence $installerEvidence $manifest.artifacts[0] $manifest.artifacts $lock } 'extraction validation'
Reject { Assert-RememberEmbeddedBuildInfo $app $baseline 'wrong-build' } 'compiled build provenance'
$extra = Write-Fixture 'payload/Remember.vst3/extra.json' 'unlisted bundle file'
Reject { Assert-RememberManifest $manifestPath $installer $baseline $version $repository } 'Unlisted installation payload'
Remove-Item -LiteralPath $extra
Reject { Assert-RememberManifest $manifestPath (Join-Path $root 'other-installer.exe') $baseline $version $repository } 'Explicit installer'
$info.buildId = 'other-build'
Write-RememberJson (Join-Path $root 'plugin-build-info.json') $info
$manifest.artifacts = @($manifest.artifacts | Where-Object role -ne 'plugin-build-info') + (Get-RememberArtifact $root (Join-Path $root 'plugin-build-info.json') 'plugin-build-info')
Write-RememberJson $manifestPath $manifest
Reject { Assert-RememberManifest $manifestPath $installer $baseline $version $repository } 'build information'
$pwsh = (Get-Process -Id $PID).Path
Reject { Invoke-RememberProcess $pwsh @('-NoProfile', '-Command', 'exit 7') (Join-Path $root 'failed-tool.log') } 'exit 7'
Write-Host "PASS: $script:checks release pipeline contract checks; fixture: $root"
