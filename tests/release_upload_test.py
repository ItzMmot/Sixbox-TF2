"""Exercise actual upload orchestration against a fake GitHub release API."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('release_upload', ROOT/'.github/scripts/upload-release-assets.py')
upload = importlib.util.module_from_spec(spec)
spec.loader.exec_module(upload)


class Server:
    def __init__(self, path):
        self.path = path
        self.stored = [{'id': 1, 'name': 'unrelated.zip', 'state': 'uploaded', 'size': 7}]
        self.calls = []
        self.failures = []
        self.corrupt = False

    def asset(self, state='uploaded'):
        return {'id': 100 + len(self.calls), 'name': self.path.name, 'state': state,
                'size': self.path.stat().st_size,
                'digest': 'sha256:' + hashlib.sha256(self.path.read_bytes()).hexdigest()}

    def __call__(self, *args):
        self.calls.append(args)
        if args[:3] == ('api', '--paginate', '--slurp'):
            return json.dumps([self.stored[:1], self.stored[1:]])
        if args[:3] == ('api', '--method', 'DELETE'):
            identity = int(args[3].rsplit('/', 1)[1])
            self.stored = [a for a in self.stored if a['id'] != identity]
            return ''
        if args[:2] == ('release', 'upload'):
            if self.failures:
                failure = self.failures.pop(0)
                if failure == 'accepted':
                    self.stored.append(self.asset())
                    raise upload.UploadError('connection reset')
                if '401' not in failure:
                    self.stored.append(self.asset('starter'))
                raise upload.UploadError(failure)
            asset = self.asset()
            if self.corrupt:
                asset['digest'] = 'sha256:wrong'
            self.stored.append(asset)
            return ''
        raise AssertionError(args)


class UploadTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name)/'SixBoxx64ReleaseFreetypeAVX2.pdb'
        self.path.write_bytes(b'new symbols')
        self.server = Server(self.path)
        self.delays = []

    def run_upload(self):
        with patch.object(upload, 'gh', self.server):
            upload.upload('owner/repo', 'nightly', 10, self.path, sleep=self.delays.append)

    def test_replaces_only_matching_name(self):
        self.server.stored.append(dict(self.server.asset(), digest='sha256:old'))
        self.run_upload()
        self.assertEqual([a['name'] for a in self.server.stored], ['unrelated.zip', self.path.name])
        self.assertEqual(self.delays, [])

    def test_duplicate_and_starter_recovery(self):
        for error in ['HTTP 422: Validation Failed; ReleaseAsset.name already exists', 'HTTP 502: Bad Gateway']:
            with self.subTest(error=error):
                self.server = Server(self.path)
                self.server.failures = [error]
                self.delays = []
                self.run_upload()
                self.assertEqual(self.delays, [2])
                self.assertEqual(len(self.server.stored), 2)
                self.assertEqual(self.server.stored[-1]['state'], 'uploaded')

    def test_lost_response_does_not_reupload_verified_bytes(self):
        self.server.failures = ['accepted']
        self.run_upload()
        self.assertEqual(sum(call[:2] == ('release', 'upload') for call in self.server.calls), 1)
        self.assertFalse(any(call[:3] == ('api', '--method', 'DELETE') for call in self.server.calls))

    def test_identical_file_is_idempotent(self):
        self.server.stored.append(self.server.asset())
        self.run_upload()
        self.assertEqual(len(self.server.calls), 1)

    def test_authentication_failure_is_not_retried(self):
        self.server.failures = ['HTTP 401: Bad credentials']
        with self.assertRaisesRegex(upload.UploadError, '401'):
            self.run_upload()
        self.assertEqual(self.delays, [])

    def test_persistent_conflict_fails_with_bounded_retries(self):
        self.server.failures = ['HTTP 422: already_exists'] * 4
        with self.assertRaisesRegex(upload.UploadError, '422'):
            self.run_upload()
        self.assertEqual(self.delays, [2, 4, 8])

    def test_digest_mismatch_never_reports_success(self):
        self.server.corrupt = True
        with self.assertRaisesRegex(upload.UploadError, 'Verification failed'):
            self.run_upload()
        self.assertEqual(self.delays, [2, 4, 8])

    def test_other_validation_errors_are_not_retried(self):
        self.server.failures = ['HTTP 422: Invalid name']
        with self.assertRaisesRegex(upload.UploadError, 'Invalid name'):
            self.run_upload()
        self.assertEqual(self.delays, [])


if __name__ == '__main__':
    unittest.main(verbosity=2)
