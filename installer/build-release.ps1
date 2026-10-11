#requires -Version 7.2
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$JucePath,
    [Parameter(Mandatory)][string]$PluginvalPath,
    [string]$CmakePath,
    [string]$GitPath,
    [string]$IsccPath,
    [string]$UnpackerPath,
    [string]$Generator,
    [string]$BuildRoot,
    [switch]$AllowDirty,
    [switch]$SkipPackage
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'ReleaseTools.psm1') -Force
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$git = Resolve-RememberTool 'git.exe' $GitPath @('C:/Program Files/Git/cmd/git.exe')
$cmake = Resolve-RememberTool 'cmake.exe' $CmakePath
$ctest = Resolve-RememberTool 'ctest.exe' (Join-Path (Split-Path $cmake -Parent) 'ctest.exe')
$version = Get-RememberVersion $repo
$source = Get-RememberSource $git $repo
$repository = Get-RememberRepository $git $repo
if ($source.state -ne 'clean' -and !$AllowDirty) { throw 'Working tree is dirty. Commit the source or use -AllowDirty for a development build.' }
$lock = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'dependencies.json') -Raw | ConvertFrom-Json
$juce = (Resolve-Path -LiteralPath $JucePath).Path
$coreHeader = [IO.File]::ReadAllText((Join-Path $juce 'modules/juce_core/juce_core.h'))
if ($coreHeader -notmatch 'version:\s*([0-9]+\.[0-9]+\.[0-9]+)' -or $Matches[1] -ne $lock.juce.version) {
    throw 'JUCE version differs from dependencies.json'
}
$juceCommit = 'archive'; $juceClean = $false
if (Test-Path -LiteralPath (Join-Path $juce '.git')) {
    $juceCommit = Invoke-RememberGit $git $juce @('rev-parse', 'HEAD')
    $juceClean = !(Invoke-RememberGit $git $juce @('status', '--porcelain', '--untracked-files=normal'))
}
if (!$AllowDirty -and ($juceCommit -cne $lock.juce.commit -or !$juceClean)) {
    throw 'Release requires the pinned clean JUCE commit, not an unverified source archive'
}
$iscc = $null
$compilerInfo = $null
$unpacker = $null
if (!$SkipPackage) {
    $iscc = Resolve-RememberTool 'ISCC.exe' $IsccPath @(
        "$env:LOCALAPPDATA/Programs/Inno Setup 6/ISCC.exe", "$env:ProgramFiles/Inno Setup 6/ISCC.exe",
        "${env:ProgramFiles(x86)}/Inno Setup 6/ISCC.exe", "${env:ProgramFiles(x86)}/Inno Setup 7/ISCC.exe")
    $compilerInfo = Get-RememberInstallerCompiler $iscc
    $unpacker = Resolve-RememberTool 'innounp.exe' $UnpackerPath
    if ((Get-FileHash -LiteralPath $unpacker -Algorithm SHA256).Hash -ine $lock.innounp.executableSha256) {
        throw 'Installer unpacker differs from dependencies.json'
    }
}
$buildId = (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssZ') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
if (!$BuildRoot) { $BuildRoot = Join-Path $repo "out/release/$buildId" }
$buildRootPath = [IO.Path]::GetFullPath($BuildRoot)
if (Test-Path -LiteralPath $buildRootPath) { throw 'BuildRoot must be new; release validation cannot reuse an old build tree' }
New-Item -ItemType Directory -Path $buildRootPath | Out-Null
$logs = Join-Path $buildRootPath 'logs'
$artifactRoot = Join-Path $buildRootPath 'artifacts'
New-Item -ItemType Directory -Path $logs, $artifactRoot | Out-Null
$null = Invoke-RememberGit $git $repo @('check-ignore', $buildRootPath)
Write-Host "Remember $($version.version), commit $($source.commit), $($source.state), build $buildId"
$testEvidence = @()
foreach ($target in @('Plugin', 'App')) {
    $build = Join-Path $buildRootPath $target
    $configure = @('-S', (Join-Path $repo $target), '-B', $build, '-A', 'x64',
        "-DJUCE_PATH=$juce", "-DGIT_EXECUTABLE=$git", '-DREMEMBER_BUILD_TESTS=ON',
        "-DREMEMBER_BUILD_ID=$buildId", "-DREMEMBER_SOURCE_FINGERPRINT=$($source.fingerprint)")
    if ($target -eq 'Plugin') {
        $configure += @('-DREMEMBER_COPY_PLUGIN_AFTER_BUILD=OFF', "-DREMEMBER_PWSH_EXECUTABLE=$((Get-Process -Id $PID).Path)")
    } else { $configure += '-DREMEMBER_ENABLE_ASAN=OFF' }
    if ($Generator) { $configure += @('-G', $Generator) }
    Invoke-RememberProcess $cmake $configure (Join-Path $logs "$target-configure.log") -WithoutPath
    $buildTargets = if ($target -eq 'Plugin') { @('RememberPlugin_VST3', 'RememberTests') } else { @('RememberApp', 'RememberAppTests') }
    Invoke-RememberProcess $cmake (@('--build', $build, '--config', 'Release', '--target') + $buildTargets + @('--', '/nr:false')) (Join-Path $logs "$target-build.log") -WithoutPath
    $suite = $target.ToLowerInvariant()
    $junit = Join-Path $logs "$suite-tests.xml"
    try {
        Invoke-RememberProcess $ctest @('--test-dir', $build, '-C', 'Release', '--output-on-failure',
            '--no-tests=error', '--output-junit', $junit) (Join-Path $logs "$suite-tests.log")
    } finally {
        $fullLog = Join-Path $build 'Testing/Temporary/LastTest.log'
        if (Test-Path -LiteralPath $fullLog) { Copy-Item -LiteralPath $fullLog -Destination (Join-Path $logs "$suite-tests-full.log") }
    }
    $testEvidence += Get-RememberTestEvidence $junit $suite
    $testWorkspace = [IO.File]::ReadAllText((Join-Path $build 'generated/test-workspace.txt'))
    $images = Join-Path $testWorkspace 'artifacts'
    if (Test-Path -LiteralPath $images -PathType Container) { Copy-Item -LiteralPath $images -Destination (Join-Path $artifactRoot "$suite-ui-artifacts") -Recurse }
    $infoPath = Join-Path $build 'generated/build-info.json'
    Assert-RememberBuildInfo (Get-Content -LiteralPath $infoPath -Raw | ConvertFrom-Json) $source $version $buildId
    Copy-Item -LiteralPath $infoPath -Destination (Join-Path $artifactRoot "$suite-build-info.json")
}
$payload = Join-Path $artifactRoot 'payload'
New-Item -ItemType Directory -Path $payload | Out-Null
Copy-Item -LiteralPath (Join-Path $buildRootPath 'App/RememberApp_artefacts/Release/Remember.exe') -Destination $payload
Copy-Item -LiteralPath (Join-Path $buildRootPath 'Plugin/RememberPlugin_artefacts/Release/VST3/Remember.vst3') -Destination $payload -Recurse
Copy-Item -LiteralPath (Join-Path $repo 'BETA_NOTES.md') -Destination $payload
$bundle = Join-Path $payload 'Remember.vst3'
Assert-RememberEmbeddedBuildInfo (Join-Path $payload 'Remember.exe') $source $buildId
Assert-RememberEmbeddedBuildInfo (Join-Path $bundle 'Contents/x86_64-win/Remember.vst3') $source $buildId
$beforeValidation = @(Get-ChildItem -LiteralPath $bundle -Recurse -File | ForEach-Object { Get-RememberArtifact $artifactRoot $_.FullName 'bundle' })
$nativeOutput = Join-Path $artifactRoot 'validation'
& (Join-Path $PSScriptRoot 'validate-vst3.ps1') -PluginPath $bundle -PluginvalPath $PluginvalPath -OutputDirectory $nativeOutput
foreach ($file in $beforeValidation) {
    $path = Resolve-RememberArtifact $artifactRoot $file.path
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ine $file.sha256) { throw 'Validator changed the staged VST3' }
}
if (@(Get-ChildItem -LiteralPath $bundle -Recurse -File).Count -ne $beforeValidation.Count) {
    throw 'Validator added or removed staged VST3 files'
}
foreach ($file in Get-ChildItem -LiteralPath $logs -File) { Copy-Item -LiteralPath $file.FullName -Destination $artifactRoot }
Assert-RememberSourceUnchanged $source (Get-RememberSource $git $repo)
$native = Get-Content -LiteralPath (Join-Path $nativeOutput 'validation.json') -Raw | ConvertFrom-Json
$provenance = [ordered]@{ schema = 1; product = 'Remember'; version = $version.version; tag = $version.tag;
    repository = $repository; buildId = $buildId; configuration = 'Release'; architecture = 'x64'; source = $source;
    dependencies = @{ juce = @{ version = $lock.juce.version; commit = $juceCommit; clean = $juceClean } };
    validation = @{ ctest = $testEvidence; nativeVst3 = $native } }
Write-RememberJson (Join-Path $payload 'build-provenance.json') $provenance
if (!$SkipPackage) {
    Invoke-RememberProcess $iscc @("/DMyAppVersion=$($version.version)", "/DMyNumericVersion=$($version.windows)",
        "/DPayloadDir=$payload", "/DReleaseOutputDir=$artifactRoot", (Join-Path $PSScriptRoot 'Remember.iss')) (Join-Path $artifactRoot 'installer-build.log')
    $installer = Join-Path $artifactRoot "Remember-$($version.version)-setup.exe"
    if (!(Test-Path -LiteralPath $installer -PathType Leaf)) { throw 'Expected explicit installer was not produced' }
    $null = Get-RememberInstallerCompiler $iscc
    if (![IO.File]::ReadAllText((Join-Path $artifactRoot 'installer-build.log')).Contains("Compiler engine version: Inno Setup $($lock.inno.version)")) {
        throw 'Installer compiler did not report the pinned engine version'
    }
    $packagingOutput = Join-Path $buildRootPath 'installer-validation'
    & (Join-Path $PSScriptRoot 'validate-installer.ps1') -Installer $installer -Payload $payload `
        -UnpackerPath $unpacker -OutputDirectory $packagingOutput
    Copy-Item -LiteralPath (Join-Path $packagingOutput 'installer-validation.json'), (Join-Path $packagingOutput 'extractor.log') -Destination $artifactRoot
}
Assert-RememberSourceUnchanged $source (Get-RememberSource $git $repo)
$artifacts = @(Get-ChildItem -LiteralPath $artifactRoot -Recurse -File | Where-Object {
    !$_.FullName.StartsWith((Join-Path $nativeOutput 'profile') + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)
} | ForEach-Object {
    $relative = [IO.Path]::GetRelativePath($artifactRoot, $_.FullName).Replace('\', '/')
    $role = switch -Regex ($relative) {
        '^Remember-.+-setup\.exe$' { 'installer'; break }
        '^payload/Remember\.exe$' { 'app'; break }
        '^payload/Remember\.vst3/Contents/x86_64-win/Remember\.vst3$' { 'vst3'; break }
        '^payload/BETA_NOTES\.md$' { 'notes'; break }
        '^plugin-build-info\.json$' { 'plugin-build-info'; break }
        '^app-build-info\.json$' { 'app-build-info'; break }
        '^plugin-tests\.xml$' { 'plugin-test-results'; break }
        '^app-tests\.xml$' { 'app-test-results'; break }
        '^validation/pluginval\.log$' { 'pluginval-log'; break }
        '^installer-validation\.json$' { 'installer-validation'; break }
        default { 'evidence' }
    }
    Get-RememberArtifact $artifactRoot $_.FullName $role
})
$provenance['packaged'] = !$SkipPackage
$provenance['publishable'] = !$SkipPackage -and !$AllowDirty -and $source.state -eq 'clean' -and $juceClean
$provenance['artifacts'] = $artifacts
if ($iscc) { $provenance['installerCompiler'] = $compilerInfo }
$manifest = Join-Path $artifactRoot 'release-manifest.json'
Write-RememberJson $manifest $provenance
if (!$SkipPackage) { $null = Assert-RememberManifest $manifest $installer $source $version $repository -AllowDevelopment:$AllowDirty }
Write-Host "Validated manifest: $manifest"
Write-Host "Publishable: $($provenance.publishable)"
