#!/usr/bin/env python3
"""Publish the Windows/macOS pair produced by the same successful workflow."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepare(root, version, commit, repository):
    windows_paths = list(root.rglob('release-manifest.json'))
    mac_paths = list(root.rglob('macos-release-manifest.json'))
    if len(windows_paths) != 1 or len(mac_paths) != 1:
        raise ValueError('Exactly one validated Windows and macOS manifest is required')
    windows_path, mac_path = windows_paths[0], mac_paths[0]
    windows = json.loads(windows_path.read_text(encoding='utf-8-sig'))
    mac = json.loads(mac_path.read_text())
    if windows['version'] != version or mac['version'] != version:
        raise ValueError('Installer version mismatch')
    if windows['source']['commit'] != commit or mac['commit'] != commit:
        raise ValueError('Both installers must come from this exact commit')
    if not windows['publishable'] or windows['source']['state'] != 'clean':
        raise ValueError('Windows installer is not publishable')
    if windows['repository'].removeprefix('github.com/') != repository:
        raise ValueError('Installer repository mismatch')
    if mac['architectures'] != ['arm64', 'x86_64'] or len(mac['tests']) != 2:
        raise ValueError('macOS universal build or test evidence missing')
    if mac['pluginval']['strictness'] != 5 or not all(t['passed'] > 0 for t in mac['tests']):
        raise ValueError('macOS validation incomplete')
    installers = [a for a in windows['artifacts'] if a['role'] == 'installer']
    if len(installers) != 1:
        raise ValueError('Windows installer evidence missing')
    payloads = [(windows_path.parent, installers[0]), (mac_path.parent, mac['installer'])]
    assets = []
    for parent, item in payloads:
        path = (parent / item['path']).resolve()
        if not path.is_relative_to(parent.resolve()) or not path.is_file() or sha256(path) != item['sha256'].lower():
            raise ValueError('Installer checksum or path mismatch')
        assets.append(path)
    return [*assets, windows_path, mac_path]


def required_checks(runs, commit):
    required = ['.github/workflows/windows-validation.yml', '.github/workflows/macos-native-validation.yml']
    pending = []
    for workflow in required:
        matches = [r for r in runs if r.get('path') == workflow and r.get('head_sha') == commit and r.get('event') == 'push']
        if not matches:
            pending.append(workflow)
            continue
        latest = max(matches, key=lambda r: r['id'])
        if latest['status'] != 'completed':
            pending.append(workflow)
        elif latest['conclusion'] != 'success':
            raise RuntimeError('Required validation failed: ' + latest['html_url'])
    return pending


def wait_for_checks(repository, commit):
    deadline = time.monotonic() + 45 * 60
    while True:
        result = subprocess.check_output(['gh', 'api', f'repos/{repository}/actions/runs?head_sha={commit}&event=push&per_page=100'], text=True)
        pending = required_checks(json.loads(result)['workflow_runs'], commit)
        if not pending:
            return
        if time.monotonic() >= deadline:
            raise RuntimeError('Required validation did not finish before the publication deadline')
        print('Waiting for required validation: ' + ', '.join(pending), flush=True)
        time.sleep(30)


def main():
    root = Path(sys.argv[1]).resolve()
    version = Path('VERSION').read_text().strip()
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
    repository = os.environ['GH_REPO']
    assets = prepare(root, version, commit, repository)
    wait_for_checks(repository, commit)
    ref = os.environ.get('GITHUB_REF', '')
    if ref.startswith('refs/heads/'):
        current = subprocess.check_output(['git', 'ls-remote', 'origin', ref], text=True).split()
        if not current or current[0] != commit:
            raise RuntimeError('Release branch advanced during validation; build the current source before publishing')
    tag = 'v' + version
    # Existing tags are immutable; never move a released version to another commit.
    remote = subprocess.check_output(['git', 'ls-remote', 'origin', f'refs/tags/{tag}', f'refs/tags/{tag}^{{}}'], text=True).strip()
    if remote:
        if remote.splitlines()[-1].split()[0] != commit:
            raise RuntimeError('Release tag belongs to a different commit')
    else:
        subprocess.run(['git', 'tag', tag, commit], check=True)
        subprocess.run(['git', 'push', 'origin', f'refs/tags/{tag}'], check=True)
    command = ['gh', 'release', 'create', tag, '--repo', repository, '--verify-tag',
               '--title', 'Remember ' + version, '--notes-file', 'BETA_NOTES.md', *map(str, assets)]
    if '-' in version:
        command.append('--prerelease')
    subprocess.run(command, check=True)


if __name__ == '__main__':
    main()
