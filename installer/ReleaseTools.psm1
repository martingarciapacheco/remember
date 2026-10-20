#requires -Version 7.2
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false

$script:PluginTests = @('RememberPreviewQuality', 'RememberResampling', 'RememberFeatureCache',
    'RememberAssociation', 'RememberRegression', 'RememberConfiguration', 'RememberStorageRecovery',
    'RememberSustainedCapture', 'RememberAnalysis', 'RememberCaptureMatrix', 'RememberHostLoading', 'RememberValidationIsolation')
$script:AppTests = @('RememberAppRegression', 'RememberAppStorageRecovery', 'RememberLibraryBenchmark', 'RememberAppColdStartup', 'RememberAppShell')

function Resolve-RememberTool([string]$Name, [string]$ExplicitPath, [string[]]$Candidates = @()) {
    if ($ExplicitPath) {
        if (!(Test-Path -LiteralPath $ExplicitPath -PathType Leaf)) { throw "Tool does not exist: $ExplicitPath" }
        return (Resolve-Path -LiteralPath $ExplicitPath).Path
    }
    $command = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command) { return $command.Source }
    foreach ($candidate in $Candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return (Resolve-Path -LiteralPath $candidate).Path }
    }
    throw "Cannot locate $Name. Supply its explicit path."
}

function Invoke-RememberGit([string]$Git, [string]$Repo, [string[]]$Arguments) {
    $output = & $Git -C $Repo @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) { throw "Git failed: $($Arguments -join ' '): $output" }
    return ($output -join "`n").TrimEnd("`r", "`n")
}

function Get-RememberRepositoryFromUrl([string]$Url) {
    # Parse the original spelling rather than Uri's normalised path: dot
    # segments, credentials, query strings and extra destinations are refused.
    if ($Url -notmatch '^(?:https://github\.com/|ssh://git@github\.com/|git@github\.com:)([A-Za-z0-9][A-Za-z0-9-]*)/([A-Za-z0-9_.-]+?)(?:\.git)?$' -or
        $Matches[2] -in @('.', '..')) {
        throw 'Release origin must identify one GitHub repository without credentials or extra URL components'
    }
    return "github.com/$($Matches[1])/$($Matches[2])"
}

function Get-RememberRepository([string]$Git, [string]$Repo) {
    $fetch = @( (Invoke-RememberGit $Git $Repo @('remote', 'get-url', '--all', 'origin')) -split "`n" )
    $push = @( (Invoke-RememberGit $Git $Repo @('remote', 'get-url', '--push', '--all', 'origin')) -split "`n" )
    if ($fetch.Count -ne 1 -or $push.Count -ne 1) { throw 'Release origin must have exactly one fetch URL and one push URL' }
    $repository = Get-RememberRepositoryFromUrl $fetch[0]
    if ($repository -ine (Get-RememberRepositoryFromUrl $push[0])) { throw 'Release origin fetch and push repositories differ' }
    return $repository
}

function Get-RememberReleaseArguments([string]$Repository, $Version, [string]$Notes,
        [string]$Installer, [string]$Manifest) {
    if ($Repository -cne (Get-RememberRepositoryFromUrl "https://$Repository")) { throw 'Invalid explicit release repository' }
    $arguments = @('release', 'create', $Version.tag, '--repo', $Repository, '--verify-tag',
        '--title', "Remember $($Version.version)", '--notes-file', $Notes, $Installer, $Manifest)
    if ($Version.version.Contains('-')) { $arguments += '--prerelease' }
    return $arguments
}

function Get-RememberInstallerCompiler([string]$Iscc) {
    $lock = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'dependencies.json') -Raw | ConvertFrom-Json
    $root = Split-Path ([IO.Path]::GetFullPath($Iscc)) -Parent
    if ((Split-Path $Iscc -Leaf) -ine 'ISCC.exe') { throw 'Installer compiler must be the pinned ISCC.exe' }
    foreach ($file in $lock.inno.files.PSObject.Properties) {
        $path = Join-Path $root $file.Name
        if (!(Test-Path -LiteralPath $path -PathType Leaf) -or
            (Get-Item -LiteralPath $path).Attributes -band [IO.FileAttributes]::ReparsePoint -or
            (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ine $file.Value) {
            throw "Installer compiler dependency differs from the pinned Inno Setup $($lock.inno.version): $($file.Name)"
        }
    }
    # The official executable's Windows FileVersion is 0.0.0.0. Record the
    # engine version of the verified distribution instead of that resource.
    [pscustomobject]@{ version = $lock.inno.version; sha256 = $lock.inno.files.'ISCC.exe'; files = $lock.inno.files }
}

function Get-RememberVersion([string]$Repo) {
    $version = [IO.File]::ReadAllText((Join-Path $Repo 'VERSION')).Trim()
    if ($version -cnotmatch '^([0-9]+)\.([0-9]+)\.([0-9]+)(-(alpha|beta|rc)([0-9]+))?$') {
        throw 'Invalid VERSION; expected major.minor.patch[-alphaN|-betaN|-rcN]'
    }
    $revision = if ($Matches[6]) { $Matches[6] } else { '0' }
    [pscustomobject]@{ version = $version; numeric = "$($Matches[1]).$($Matches[2]).$($Matches[3])";
        windows = "$($Matches[1]).$($Matches[2]).$($Matches[3]).$revision"; tag = "v$version" }
}

function Get-RememberSource([string]$Git, [string]$Repo) {
    $root = [IO.Path]::GetFullPath($Repo).TrimEnd('\', '/')
    $gitRoot = (Invoke-RememberGit $Git $root @('rev-parse', '--show-toplevel')).Replace('/', '\').TrimEnd('\')
    if ($gitRoot -ine $root.Replace('/', '\')) { throw 'Source root must be the Git repository root' }
    $commit = Invoke-RememberGit $Git $root @('rev-parse', 'HEAD')
    if ($commit -cnotmatch '^[0-9a-f]{40}$') { throw 'Cannot identify full source commit' }
    $status = Invoke-RememberGit $Git $root @('status', '--porcelain', '--untracked-files=all')
    $listed = Invoke-RememberGit $Git $root @('ls-files', '-z', '--cached', '--others', '--exclude-standard', '--deduplicate')
    [string[]]$paths = $listed.Split([char]0, [StringSplitOptions]::RemoveEmptyEntries)
    [Array]::Sort($paths, [StringComparer]::Ordinal)
    if ($paths.Length -eq 0) { throw 'Empty source snapshot' }
    $records = foreach ($path in $paths) {
        $file = Join-Path $root $path
        if (Test-Path -LiteralPath $file -PathType Leaf) {
            if ((Get-Item -LiteralPath $file).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Source symlinks are not supported by the release snapshot: $path"
            }
            $hash = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
            "$path`0$hash"
        } elseif (Test-Path -LiteralPath $file) { throw "Submodules/directories need explicit provenance: $path" }
        else { "$path`0missing" }
    }
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $fingerprint = [Convert]::ToHexString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($records -join "`n"))).ToLowerInvariant() }
    finally { $sha.Dispose() }
    [pscustomobject]@{ commit = $commit; state = $(if ($status) { 'dirty' } else { 'clean' });
        fingerprint = $fingerprint; files = $paths.Length }
}

function Assert-RememberSourceUnchanged($Before, $After) {
    foreach ($field in @('commit', 'state', 'fingerprint')) {
        if ($Before.$field -cne $After.$field) { throw "Source changed during validation ($field). Rebuild before packaging." }
    }
}

function Write-RememberJson([string]$Path, $Value) {
    [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 24) + "`n", [Text.UTF8Encoding]::new($false))
}

function Invoke-RememberProcess([string]$Executable, [string[]]$Arguments, [string]$Log,
        [switch]$WithoutPath) {
    Write-Host "Running $(Split-Path $Executable -Leaf); log: $Log"
    # GUI executables such as pluginval do not reliably set PowerShell's
    # LASTEXITCODE. Wait for the actual process and retain its exit status.
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Executable
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.WorkingDirectory = (Get-Location).Path
    foreach ($argument in $Arguments) { $start.ArgumentList.Add($argument) }
    if ($WithoutPath) {
        $null = $start.Environment.Remove('Path'); $null = $start.Environment.Remove('PATH')
        # JUCE's Windows bundle icon step invokes attrib.exe. Keep system tools
        # available without inheriting unrelated compiler/runtime directories.
        $start.Environment['Path'] = [Environment]::GetFolderPath([Environment+SpecialFolder]::System)
    }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    try {
        if (!$process.Start()) { throw "Cannot start $Executable" }
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (!$process.WaitForExit(3600000)) { $process.Kill($true); throw "Process exceeded one hour: $Executable" }
        $code = $process.ExitCode
        [IO.File]::WriteAllText($Log, $stdout.GetAwaiter().GetResult() + $stderr.GetAwaiter().GetResult(), [Text.UTF8Encoding]::new($false))
    } finally { $process.Dispose() }
    if ($code -ne 0) {
        Get-Content -LiteralPath $Log -Tail 35 | Write-Host
        throw "Process failed with exit $code; see $Log"
    }
}

function Get-RememberTestEvidence([string]$XmlFile, [string]$Suite) {
    $required = switch ($Suite) { 'plugin' { $script:PluginTests }; 'app' { $script:AppTests }; default { throw 'Unknown test suite' } }
    $xml = [xml][IO.File]::ReadAllText($XmlFile)
    $cases = @($xml.SelectNodes('//testcase'))
    if ($cases.Count -eq 0) { throw "CTest returned no tests: $XmlFile" }
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($case in $cases) {
        $name = $case.GetAttribute('name')
        if (!$seen.Add($name)) { throw "Duplicate CTest case: $name" }
        if ($case.SelectSingleNode('failure|error|skipped')) { throw "CTest failed or skipped: $name" }
        if ($case.HasAttribute('status') -and $case.GetAttribute('status') -ne 'run') { throw "CTest case did not run: $name" }
    }
    foreach ($name in $required) { if (!$seen.Contains($name)) { throw "Missing required CTest case: $name" } }
    [pscustomobject]@{ suite = $Suite; passed = $true; names = @($cases | ForEach-Object { $_.GetAttribute('name') }) }
}

function Resolve-RememberArtifact([string]$Root, [string]$RelativePath) {
    if (!$RelativePath -or $RelativePath.Contains('\') -or $RelativePath.Contains(':') -or
        [IO.Path]::IsPathRooted($RelativePath) -or
        @($RelativePath.Split('/') | Where-Object { $_ -in @('', '.', '..') }).Count) {
        throw "Invalid manifest artifact path: $RelativePath"
    }
    $rootPath = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    $path = [IO.Path]::GetFullPath((Join-Path $rootPath $RelativePath))
    if (!$path.StartsWith($rootPath + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Artifact escapes manifest directory'
    }
    $cursor = $path
    while ($cursor -and $cursor.Length -ge $rootPath.Length) {
        if ((Test-Path -LiteralPath $cursor) -and
            ((Get-Item -LiteralPath $cursor).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "Artifact reparse points are not supported: $cursor"
        }
        $cursor = Split-Path $cursor -Parent
    }
    return $path
}

function Get-RememberArtifact([string]$Root, [string]$Path, [string]$Role) {
    $relative = [IO.Path]::GetRelativePath($Root, $Path).Replace('\', '/')
    $resolved = Resolve-RememberArtifact $Root $relative
    $file = Get-Item -LiteralPath $resolved
    [pscustomobject]@{ path = $relative; role = $Role; bytes = $file.Length;
        sha256 = (Get-FileHash -LiteralPath $resolved -Algorithm SHA256).Hash.ToLowerInvariant() }
}

function Assert-RememberBuildInfo($Info, $Source, $Version, [string]$BuildId) {
    if ($Info.schema -ne 1 -or $Info.productVersion -cne $Version.version -or
        $Info.numericVersion -cne $Version.numeric -or $Info.windowsVersion -cne $Version.windows -or
        $Info.commit -cne $Source.commit -or $Info.sourceState -cne $Source.state -or
        $Info.sourceFingerprint -cne $Source.fingerprint -or $Info.buildId -cne $BuildId) {
        throw 'Configured build information does not match the validated source/version/build'
    }
}

function Assert-RememberEmbeddedBuildInfo([string]$Binary, $Source, [string]$BuildId) {
    $expected = "Remember build $BuildId source $($Source.fingerprint) commit $($Source.commit) state $($Source.state)"
    $bytes = [IO.File]::ReadAllBytes($Binary)
    if (![Text.Encoding]::ASCII.GetString($bytes).Contains($expected)) {
        throw "Binary does not contain the expected compiled build provenance: $Binary"
    }
}

function Assert-RememberNativeLog([string]$Log, [int]$Strictness) {
    $text = [IO.File]::ReadAllText($Log)
    if ($text -notmatch '(?m)^SUCCESS\s*$' -or !$text.Contains('Num plugins found: 1') -or
        !$text.Contains("Strictness level: $Strictness") -or
        !$text.Contains('Completed tests in pluginval / Editor') -or
        !$text.Contains('Completed tests in pluginval / Plugin state')) {
        throw 'Native VST3 log does not prove successful scan/editor/state validation'
    }
    foreach ($rate in @(44100, 48000, 88200, 96000, 192000)) {
        foreach ($block in @(32, 64, 128, 512, 2048)) {
            if (!$text.Contains("Testing with sample rate [$rate] and block size [$block]")) {
                throw "Native VST3 matrix case is missing: $rate Hz / $block samples"
            }
        }
    }
}

function Assert-RememberTag([string]$Commit, [string]$LocalCommit, [string]$RemoteCommit) {
    foreach ($existing in @($LocalCommit, $RemoteCommit)) {
        if ($existing -and $existing -cne $Commit) { throw 'Existing version tag belongs to another commit; publication refused' }
    }
}

function Assert-RememberManifest([string]$ManifestPath, [string]$InstallerPath, $Source, $Version,
        [string]$Repository, [switch]$AllowDevelopment) {
    $manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
    if ($manifest.schema -ne 1 -or !$manifest.packaged -or
        $manifest.configuration -cne 'Release' -or $manifest.architecture -cne 'x64') {
        throw 'Validation requires a packaged Release/x64 build'
    }
    if (!$AllowDevelopment -and (!$manifest.publishable -or
        $manifest.source.state -cne 'clean' -or $Source.state -cne 'clean')) {
        throw 'Publication requires a packaged, validated Release/x64 build from clean source'
    }
    if ($AllowDevelopment -and $manifest.publishable) { throw 'Development validation requires an explicitly unpublishable manifest' }
    if (!$Repository -or $manifest.repository -ine $Repository -or
        $Repository -cne (Get-RememberRepositoryFromUrl "https://$Repository")) { throw 'Manifest repository differs from release origin' }
    Assert-RememberSourceUnchanged $manifest.source $Source
    if ($manifest.version -cne $Version.version -or $manifest.tag -cne $Version.tag) { throw 'Manifest version differs from VERSION' }
    if ($manifest.buildId -cnotmatch '^[A-Za-z0-9_.-]+$') { throw 'Invalid manifest build ID' }
    $root = Split-Path ([IO.Path]::GetFullPath($ManifestPath)) -Parent
    $paths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($artifact in $manifest.artifacts) {
        if (!$paths.Add($artifact.path)) { throw 'Duplicate artifact in manifest' }
        $path = Resolve-RememberArtifact $root $artifact.path
        if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing artifact: $path" }
        if ($artifact.sha256 -cnotmatch '^[0-9a-f]{64}$' -or (Get-Item -LiteralPath $path).Length -ne $artifact.bytes -or
            (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ine $artifact.sha256) {
            throw "Artifact content differs from validated build: $path"
        }
    }
    foreach ($role in @('installer', 'app', 'vst3', 'notes', 'plugin-build-info', 'app-build-info',
        'plugin-test-results', 'app-test-results', 'pluginval-log', 'installer-validation')) {
        if (@($manifest.artifacts | Where-Object role -eq $role).Count -ne 1) { throw "Required artifact role missing or repeated: $role" }
    }
    # Every file in the installation payload must be listed. Extra bundle files
    # or a modified moduleinfo cannot be silently added after validation.
    $payload = Join-Path $root 'payload'
    foreach ($file in Get-ChildItem -LiteralPath $payload -Recurse -File) {
        $relative = [IO.Path]::GetRelativePath($root, $file.FullName).Replace('\', '/')
        if (!$paths.Contains($relative)) { throw "Unlisted installation payload file: $relative" }
    }
    foreach ($role in @('app', 'vst3')) {
        $binary = @($manifest.artifacts | Where-Object role -eq $role)[0]
        Assert-RememberEmbeddedBuildInfo (Resolve-RememberArtifact $root $binary.path) $Source $manifest.buildId
    }
    $installer = @($manifest.artifacts | Where-Object role -eq 'installer')[0]
    if ([IO.Path]::GetFullPath($InstallerPath) -ine (Resolve-RememberArtifact $root $installer.path) -or
        (Split-Path $InstallerPath -Leaf) -cne "Remember-$($Version.version)-setup.exe") {
        throw 'Explicit installer does not match manifest'
    }
    foreach ($suite in @('plugin', 'app')) {
        $evidence = @($manifest.artifacts | Where-Object role -eq "$suite-test-results")[0]
        $null = Get-RememberTestEvidence (Resolve-RememberArtifact $root $evidence.path) $suite
        $info = @($manifest.artifacts | Where-Object role -eq "$suite-build-info")[0]
        Assert-RememberBuildInfo (Get-Content -LiteralPath (Resolve-RememberArtifact $root $info.path) -Raw | ConvertFrom-Json) $Source $Version $manifest.buildId
    }
    $lock = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'dependencies.json') -Raw | ConvertFrom-Json
    if (!$manifest.validation.nativeVst3.passed -or $manifest.validation.nativeVst3.strictness -lt 5 -or
        !$manifest.validation.nativeVst3.isolatedProfile -or
        $manifest.validation.nativeVst3.validatorSha256 -cne $lock.pluginval.executableSha256 -or
        (!$AllowDevelopment -and ($manifest.dependencies.juce.commit -cne $lock.juce.commit -or !$manifest.dependencies.juce.clean))) {
        throw 'Required native VST3/dependency validation is missing'
    }
    if ($manifest.installerCompiler.version -cne $lock.inno.version -or
        $manifest.installerCompiler.sha256 -cne $lock.inno.files.'ISCC.exe') { throw 'Installer compiler provenance differs from dependencies.json' }
    foreach ($file in $lock.inno.files.PSObject.Properties) {
        if ($manifest.installerCompiler.files.($file.Name) -cne $file.Value) { throw 'Installer compiler dependency provenance differs from dependencies.json' }
    }
    $packaging = @($manifest.artifacts | Where-Object role -eq 'installer-validation')[0]
    Assert-RememberInstallerEvidence (Get-Content -LiteralPath (Resolve-RememberArtifact $root $packaging.path) -Raw | ConvertFrom-Json) $installer $manifest.artifacts $lock
    $nativeLog = @($manifest.artifacts | Where-Object role -eq 'pluginval-log')[0]
    Assert-RememberNativeLog (Resolve-RememberArtifact $root $nativeLog.path) $manifest.validation.nativeVst3.strictness
    return $manifest
}

function Get-RememberInstallerPayload([string]$Payload) {
    foreach ($file in Get-ChildItem -LiteralPath $Payload -Recurse -File) {
        $relative = [IO.Path]::GetRelativePath($Payload, $file.FullName).Replace('\', '/')
        $destination = if ($relative.StartsWith('Remember.vst3/')) { "{commoncf64}/VST3/$relative" }
            elseif ($relative -in @('Remember.exe', 'BETA_NOTES.md', 'build-provenance.json', 'Inter-LICENSE.txt')) { "{app}/$relative" }
            else { throw "Unrecognised installer payload file: $relative" }
        $artifact = Get-RememberArtifact $Payload $file.FullName 'payload'
        [pscustomobject]@{ path = "payload/$relative"; destination = $destination; bytes = $artifact.bytes; sha256 = $artifact.sha256 }
    }
}

function Assert-RememberInstallerContents([string]$Payload, [string]$Extracted, $Version) {
    $expected = @(Get-RememberInstallerPayload $Payload)
    if ($expected.Count -lt 5) { throw 'Installer payload is incomplete' }
    $destinations = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($artifact in $expected) {
        $null = $destinations.Add($artifact.destination)
        $path = Resolve-RememberArtifact $Extracted $artifact.destination
        if (!(Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -ne $artifact.bytes -or
            (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ine $artifact.sha256) {
            throw "Extracted installer payload differs: $($artifact.destination)"
        }
    }
    foreach ($file in Get-ChildItem -LiteralPath $Extracted -Recurse -File) {
        $relative = [IO.Path]::GetRelativePath($Extracted, $file.FullName).Replace('\', '/')
        if ($relative -ne 'install_script.iss' -and !$destinations.Contains($relative)) { throw "Unexpected extracted installer file: $relative" }
    }
    $scriptText = [IO.File]::ReadAllText((Join-Path $Extracted 'install_script.iss'))
    foreach ($required in @('AppId={{C3F5A6E2-9F2D-4C8A-9F0E-1D2B3C4E5F60}}', "AppVersion=$($Version.version)",
        'ArchitecturesAllowed=x64compatible', 'ArchitecturesInstallIn64BitMode=x64compatible')) {
        if (!$scriptText.Contains($required + "`r`n") -and !$scriptText.Contains($required + "`n")) { throw "Installer setup metadata differs: $required" }
    }
    return $expected
}

function Assert-RememberInstallerEvidence($Evidence, $InstallerArtifact, $Artifacts, $Lock) {
    if ($Evidence.schema -ne 1 -or !$Evidence.passed -or $Evidence.installerSha256 -cne $InstallerArtifact.sha256 -or
        $Evidence.extractorSha256 -cne $Lock.innounp.executableSha256 -or $Evidence.payload.Count -lt 5) {
        throw 'Required installer extraction validation is missing'
    }
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($file in $Evidence.payload) {
        if (!$seen.Add($file.path)) { throw 'Duplicate installer evidence payload' }
        $matching = @($Artifacts | Where-Object path -eq $file.path)
        if ($matching.Count -ne 1 -or $matching[0].sha256 -cne $file.sha256 -or $matching[0].bytes -ne $file.bytes) {
            throw 'Installer extraction evidence differs from validated payload'
        }
    }
    foreach ($artifact in $Artifacts | Where-Object { $_.path.StartsWith('payload/') }) {
        if (!$seen.Contains($artifact.path)) { throw 'Installer extraction evidence omits payload file' }
    }
}

Export-ModuleMember -Function *-Remember*
