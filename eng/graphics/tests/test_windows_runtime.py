"""Runtime packaging must not fall back to a developer PATH compiler."""
import importlib.util
import io
import json
import os
import stat
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

HERE = Path(__file__).parents[1]
sys.path.insert(0, str(HERE))
spec = importlib.util.spec_from_file_location("graphics_windows_runtime", HERE / "build.py")
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


class WindowsRuntimeTests(unittest.TestCase):
    @unittest.skipUnless(os.name == "nt", "Windows read-only file semantics")
    def test_rebuilding_sdk_removes_readonly_generated_licenses(self):
        with tempfile.TemporaryDirectory() as temp:
            sdk = Path(temp) / "sdk"
            sdk.mkdir()
            license_file = sdk / "LICENSE"
            license_file.write_text("notice")
            license_file.chmod(stat.S_IREAD)
            builder.remove_sdk(sdk)
            self.assertFalse(sdk.exists())

    def test_wrong_compiler_is_rejected_before_any_network_or_staging(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            compiler = root / json.loads((HERE / "windows-runtime.json").read_text())["compiler"]
            compiler.parent.mkdir(parents=True)
            compiler.write_bytes(b"foreign compiler")
            with patch.object(builder.urllib.request, "urlopen") as download:
                with self.assertRaisesRegex(ValueError, "Windows runtime requires SDK"):
                    builder.stage_windows_runtime(SimpleNamespace(windows_sdk=root), root / "sdk")
                download.assert_not_called()
            self.assertFalse((root / "sdk").exists())

    def test_wrong_license_does_not_silently_accept_changed_terms(self):
        pin = json.loads((HERE / "windows-runtime.json").read_text())
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            compiler = root / pin["compiler"]
            compiler.parent.mkdir(parents=True)
            compiler.write_bytes(b"fixture")
            sdk = root / "sdk"
            (sdk / "bin").mkdir(parents=True)
            with patch.object(builder, "sha", side_effect=[pin["sha256"], "wrong-license"]), \
                    patch.object(builder.urllib.request, "urlopen", return_value=io.BytesIO(b"changed")):
                with self.assertRaisesRegex(ValueError, "license checksum mismatch"):
                    builder.stage_windows_runtime(SimpleNamespace(windows_sdk=root), sdk)
            self.assertFalse((sdk / "webscene-graphics-package.json").exists())


if __name__ == "__main__":
    unittest.main()
