import importlib.util
from pathlib import Path
import subprocess
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("dawn_exports", Path(__file__).parents[1] / "dawn_exports.py")
exports = importlib.util.module_from_spec(spec)
spec.loader.exec_module(exports)


class DawnExportTests(unittest.TestCase):
    def inspect(self, text, rid):
        with patch.object(exports.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, text, "")):
            return exports.inspect_exports(Path("candidate"), rid)

    def test_native_symbol_formats(self):
        for rid, text in [
            ("osx-arm64", "00001000 T _wgpuCreateInstance\n00001010 T _wgpuGetProcAddress\n"),
            ("linux-x64", "00001000 T wgpuCreateInstance\n00001010 T wgpuGetProcAddress\n"),
            ("win-x64", "  1  0 00001000 wgpuCreateInstance\n  2  1 00001010 wgpuGetProcAddress\n")]:
            with self.subTest(rid=rid):
                self.assertEqual(self.inspect(text, rid)["status"], "passed")

    def test_bundled_dependency_export_is_rejected(self):
        with self.assertRaises(ValueError):
            self.inspect("1000 T wgpuCreateInstance\n1010 T wgpuGetProcAddress\n1020 T AbslInternalSpinLockDelay\n", "linux-x64")

    def test_unrecognized_or_partial_output_is_not_a_pass(self):
        for text in ["", "unexpected symbol tool output", "1000 T _wgpuCreateInstance\n"]:
            with self.subTest(text=text), self.assertRaises(ValueError):
                self.inspect(text, "osx-arm64")
