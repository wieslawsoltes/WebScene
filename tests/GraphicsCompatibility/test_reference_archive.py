import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('archive', Path(__file__).with_name('verify-reference-archive.py'))
archive = importlib.util.module_from_spec(spec)
spec.loader.exec_module(archive)


class ArchiveTests(unittest.TestCase):
    def test_retained_bytes_and_rejections(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            names = ['capture-chrome-reference.mjs', 'chrome-session.mjs',
                     'reference-workloads.mjs', 'presentation-trace.mjs']
            digest = hashlib.sha256(b'source').hexdigest()
            for name in names:
                (root / name).write_bytes(b'source')
            data = {'status': 'captured', 'harness': dict.fromkeys(names, digest),
                    'harnessFiles': {n: {'file': n, 'sha256': digest, 'bytes': 6} for n in names}}
            def save():
                (root / 'reference.json').write_text(json.dumps(data))
            save()
            self.assertEqual(archive.verify(root)['referencedFiles'], 4)
            (root / names[0]).write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError, 'hash mismatch'):
                archive.verify(root)
            (root / names[0]).unlink()
            with self.assertRaises(FileNotFoundError):
                archive.verify(root)
            (root / names[0]).write_bytes(b'source')
            data['harnessFiles'][names[0]]['file'] = '../outside'
            save()
            with self.assertRaisesRegex(ValueError, 'escapes'):
                archive.verify(root)
            data['status'] = 'running'
            save()
            with self.assertRaisesRegex(ValueError, 'incomplete'):
                archive.verify(root)


if __name__ == '__main__':
    unittest.main()
