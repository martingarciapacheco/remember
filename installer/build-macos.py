#!/usr/bin/env python3
"""Native macOS universal release builder. Run through release-installers.yml."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import zipfile
import xml.etree.ElementTree as ET


def run(*args, **kwargs):
    print('+', ' '.join(map(str, args)), flush=True)
    return subprocess.run(list(map(str, args)), check=True, **kwargs)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--juce', required=True, type=Path)
    parser.add_argument('--pluginval-archive', required=True, type=Path)
    parser.add_argument('--output', default='out/macos-release', type=Path)
    args = parser.parse_args()
    if platform.system() != 'Darwin':
        parser.error('This builder requires macOS and Xcode command line tools')
    repo = Path(__file__).resolve().parent.parent
    os.chdir(repo)
    if subprocess.check_output(['git', 'status', '--porcelain']).strip():
        raise RuntimeError('Release requires clean source')
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
    version = (repo / 'VERSION').read_text().strip()
    lock = json.loads((repo / 'installer/dependencies.json').read_text())
    juce = args.juce.resolve()
    if subprocess.check_output(['git', '-C', str(juce), 'rev-parse', 'HEAD'], text=True).strip() != lock['juce']['commit']:
        raise RuntimeError('JUCE commit differs from dependency lock')
    if subprocess.check_output(['git', '-C', str(juce), 'status', '--porcelain']).strip():
        raise RuntimeError('JUCE source is dirty')
    if digest(args.pluginval_archive) != lock['pluginvalMac']['archiveSha256']:
        raise RuntimeError('pluginval macOS checksum mismatch')
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=False)
    artifacts = root / 'artifacts'
    artifacts.mkdir()
    tests = []
    for source, targets in [('Plugin', ['RememberPlugin_VST3', 'RememberTests']),
                            ('App', ['RememberApp', 'RememberAppTests'])]:
        build = root / source
        run('cmake', '-S', source, '-B', build, '-G', 'Xcode',
            f'-DJUCE_PATH={juce}', '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64',
            '-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0', '-DREMEMBER_BUILD_TESTS=ON',
            '-DREMEMBER_COPY_PLUGIN_AFTER_BUILD=OFF', f'-DREMEMBER_BUILD_ID=macos-{commit[:12]}')
        run('cmake', '--build', build, '--config', 'Release', '--parallel', '3', '--target', *targets)
        report = root / f'{source.lower()}-tests.xml'
        run('ctest', '--test-dir', build, '-C', 'Release', '--output-on-failure',
            '--no-tests=error', '--output-junit', report)
        cases = ET.parse(report).findall('.//testcase')
        if not cases or any(c.find('failure') is not None or c.find('skipped') is not None for c in cases):
            raise RuntimeError('Incomplete or failed test report')
        tests.append({'suite': source, 'passed': len(cases), 'reportSha256': digest(report)})
        shutil.copy2(build / 'generated/build-info.json', artifacts / f'{source.lower()}-build-info.json')
        workspace = Path((build / 'generated/test-workspace.txt').read_text().strip())
        if (workspace / 'artifacts').is_dir():
            shutil.copytree(workspace / 'artifacts', artifacts / f'{source.lower()}-ui-artifacts')

    payload = root / 'payload'
    app = payload / 'Applications/Remember.app'
    vst = payload / 'Library/Audio/Plug-Ins/VST3/Remember.vst3'
    shutil.copytree(root / 'App/RememberApp_artefacts/Release/Remember.app', app, symlinks=True)
    shutil.copytree(root / 'Plugin/RememberPlugin_artefacts/Release/VST3/Remember.vst3', vst, symlinks=True)
    for bundle in (app, vst):
        executable = bundle / 'Contents/MacOS/Remember'
        run('lipo', '-verify_arch', 'arm64', 'x86_64', executable)
        # Ad-hoc signatures are required for native Apple Silicon loading. They
        # are not Developer ID signatures and do not imply notarization.
        run('codesign', '--force', '--deep', '--sign', '-', bundle)
        run('codesign', '--verify', '--deep', '--strict', bundle)
    validator = root / 'validator'
    with zipfile.ZipFile(args.pluginval_archive) as archive:
        archive.extractall(validator)
    validator_app = next(validator.rglob('pluginval.app'))
    validator_exe = validator_app / 'Contents/MacOS/pluginval'
    validator_exe.chmod(0o755)
    run('xattr', '-cr', validator_app)
    profile = root / 'validation-profile'
    profile.mkdir()
    (profile / '.remember-validation-profile').write_text('Remember isolated validation profile v1')
    (profile / 'previews').mkdir()
    (profile / 'config.json').write_text(json.dumps({'previewsFolder': str(profile / 'previews'), 'projectsRootFolders': []}))
    env = dict(os.environ, REMEMBER_VALIDATION_PROFILE=str(profile))
    with (root / 'pluginval.log').open('w') as log:
        run(validator_exe, '--strictness-level', '5', '--validate', vst, env=env, stdout=log, stderr=subprocess.STDOUT)
    docs = app / 'Contents/Resources'
    docs.mkdir(exist_ok=True)
    shutil.copy2(repo / 'BETA_NOTES.md', docs)
    # Adding resources changes the sealed bundle: sign the final payload again.
    run('codesign', '--force', '--deep', '--sign', '-', app)
    run('codesign', '--verify', '--deep', '--strict', app)
    numeric = version.split('-')[0] + '.' + (version.split('beta')[1] if 'beta' in version else '0')
    package = artifacts / f'Remember-{version}-macOS-universal.pkg'
    run('pkgbuild', '--root', payload, '--identifier', 'com.martingarciapacheco.remember',
        '--version', numeric, '--install-location', '/', '--ownership', 'recommended', package)
    expanded = root / 'expanded'
    run('pkgutil', '--expand-full', package, expanded)
    extracted = expanded / 'Payload'
    # Compare every installed file with the signed payload, including both bundles.
    files = sorted(p.relative_to(payload) for p in payload.rglob('*') if p.is_file())
    extracted_files = sorted(p.relative_to(extracted) for p in extracted.rglob('*') if p.is_file())
    if files != extracted_files or any(digest(payload / p) != digest(extracted / p) for p in files):
        raise RuntimeError('Extracted installer differs from the validated payload')
    run('codesign', '--verify', '--deep', '--strict', extracted / 'Applications/Remember.app')
    run('codesign', '--verify', '--deep', '--strict', extracted / 'Library/Audio/Plug-Ins/VST3/Remember.vst3')
    manifest = {'version': version, 'commit': commit, 'platform': 'macOS', 'architectures': ['arm64', 'x86_64'],
                'minimumOS': '11.0', 'signing': 'ad-hoc; installer unsigned; not notarized',
                'tests': tests, 'pluginval': {'version': lock['pluginvalMac']['version'], 'strictness': 5,
                'logSha256': digest(root / 'pluginval.log')},
                'installer': {'path': package.name, 'sha256': digest(package)},
                'payload': [{'path': str(p), 'sha256': digest(payload / p)} for p in files]}
    (artifacts / 'macos-release-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    shutil.copy2(repo / 'BETA_NOTES.md', artifacts)


if __name__ == '__main__':
    main()
