import hashlib
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

HERE = Path(__file__).parents[1]
sys.path.insert(0, str(HERE))
spec = importlib.util.spec_from_file_location("graphics_build", HERE / "build.py")
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


class CheckoutTests(unittest.TestCase):
    def test_text_attributes_keep_pinned_bytes_despite_global_crlf(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            config = root / "gitconfig"
            config.write_text("[core]\n eol = crlf\n autocrlf = false\n")
            with patch.dict(os.environ, {"GIT_CONFIG_GLOBAL": str(config), "GIT_CONFIG_NOSYSTEM": "1"}):
                source = root / "source"
                source.mkdir()
                def git(*args):
                    return subprocess.check_output(["git", "-C", str(source), *args], stderr=subprocess.DEVNULL, text=True).strip()
                git("init")
                (source / ".gitattributes").write_bytes(b"* text=auto\n")
                license_bytes = b"Pinned license\nSecond line\n"
                (source / "LICENSE").write_bytes(license_bytes)
                git("add", ".")
                git("-c", "user.name=SDK Test", "-c", "user.email=sdk-test@example.invalid", "commit", "-m", "fixture")
                lock = {"sources": {"fixture": {"repository": str(source), "revision": git("rev-parse", "HEAD"),
                        "licenseFile": "LICENSE", "licenseSha256": hashlib.sha256(license_bytes).hexdigest()}}}
                with patch.object(builder, "LOCK", lock):
                    builder.checkout("fixture", root / "checkout")
                self.assertEqual((root / "checkout/LICENSE").read_bytes(), license_bytes)
