"""Publication must reject a partial pair, mixed source and damaged installers."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('publish_pair', Path(__file__).resolve().parents[1] / 'installer/publish-pair.py')
publisher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(publisher)


class PairedReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='remember-paired-release-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.version, self.commit, self.repository = '1.0.0-beta6', 'a' * 40, 'owner/remember'
        for name in ['windows.exe', 'macos.pkg']:
            (self.root / name).write_bytes(name.encode())
        self.windows = {'version': self.version, 'source': {'commit': self.commit, 'state': 'clean'},
                        'publishable': True, 'repository': 'github.com/' + self.repository,
                        'artifacts': [{'role': 'installer', 'path': 'windows.exe', 'sha256': publisher.sha256(self.root / 'windows.exe')}]}
        self.mac = {'version': self.version, 'commit': self.commit, 'architectures': ['arm64', 'x86_64'],
                    'tests': [{'passed': 16}, {'passed': 17}], 'pluginval': {'strictness': 5},
                    'installer': {'path': 'macos.pkg', 'sha256': publisher.sha256(self.root / 'macos.pkg')}}

    def prepare(self):
        (self.root / 'release-manifest.json').write_text(json.dumps(self.windows))
        (self.root / 'macos-release-manifest.json').write_text(json.dumps(self.mac))
        return publisher.prepare(self.root, self.version, self.commit, self.repository)

    def test_complete_pair(self):
        self.assertEqual(len(self.prepare()), 4)

    def test_mixed_commit(self):
        self.mac['commit'] = 'b' * 40
        with self.assertRaisesRegex(ValueError, 'exact commit'):
            self.prepare()

    def test_missing_mac_installer(self):
        (self.root / 'macos.pkg').unlink()
        with self.assertRaisesRegex(ValueError, 'checksum or path'):
            self.prepare()

    def test_corrupt_installer(self):
        (self.root / 'windows.exe').write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'checksum or path'):
            self.prepare()

    def test_missing_architecture(self):
        self.mac['architectures'] = ['x86_64']
        with self.assertRaisesRegex(ValueError, 'universal'):
            self.prepare()

    def test_development_build(self):
        self.windows['publishable'] = False
        with self.assertRaisesRegex(ValueError, 'not publishable'):
            self.prepare()

    def test_missing_manifest(self):
        self.prepare()
        (self.root / 'macos-release-manifest.json').unlink()
        with self.assertRaisesRegex(ValueError, 'Exactly one'):
            publisher.prepare(self.root, self.version, self.commit, self.repository)

    def test_path_escape(self):
        self.mac['installer']['path'] = '../macos.pkg'
        with self.assertRaisesRegex(ValueError, 'checksum or path'):
            self.prepare()

    def test_failed_required_ci(self):
        runs = [{'path': '.github/workflows/windows-validation.yml', 'head_sha': self.commit, 'event': 'push',
                 'id': 1, 'status': 'completed', 'conclusion': 'failure', 'html_url': 'https://github.com/example/actions/runs/1'}]
        with self.assertRaisesRegex(RuntimeError, 'Required validation failed'):
            publisher.required_checks(runs, self.commit)

    def test_other_commit_does_not_satisfy_ci(self):
        runs = [{'path': '.github/workflows/windows-validation.yml', 'head_sha': 'b' * 40, 'event': 'push',
                 'id': 1, 'status': 'completed', 'conclusion': 'success'}]
        self.assertEqual(len(publisher.required_checks(runs, self.commit)), 2)

    def test_all_required_ci_passed(self):
        runs = [{'path': path, 'head_sha': self.commit, 'event': 'push', 'id': i, 'status': 'completed', 'conclusion': 'success'}
                for i, path in enumerate(['.github/workflows/windows-validation.yml', '.github/workflows/macos-native-validation.yml'])]
        self.assertEqual(publisher.required_checks(runs, self.commit), [])


if __name__ == '__main__':
    unittest.main()
