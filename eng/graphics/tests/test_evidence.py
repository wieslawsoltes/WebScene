import importlib.util
import json
from pathlib import Path
import unittest

HERE = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("graphics_run_probes", HERE / "run-probes.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class HardwareEvidenceTests(unittest.TestCase):
    def setUp(self):
        # Synthetic validator input only; these records are never hardware qualification output.
        self.result = {"schemaVersion": 1, "probe": "angle", "status": "passed",
                       "hardwareAccelerated": True, "backend": "metal", "esMajor": 3,
                       "adapter": "synthetic adapter", "driver": "synthetic driver", "verifiedPixels": 68,
                       "expectedRGBA": [51, 102, 153, 255], "tolerance": 1,
                       "diagnosticReadback": True, "webglCompatibleContext": True,
                       "robustResourceInitialization": True}

    def inspect(self, code=0):
        return runner.inspect_process("angle", "metal", 3, code, json.dumps(self.result))["status"]

    def test_complete_success_record(self):
        self.assertEqual(self.inspect(), "passed")

    def test_missing_or_software_hardware_is_not_a_pass(self):
        for value in [False, None, "true", 1]:
            with self.subTest(value=value):
                self.result["hardwareAccelerated"] = value
                self.assertEqual(self.inspect(), "failed")

    def test_wrong_backend_or_context_is_not_a_pass(self):
        for key, value in [("backend", "vulkan"), ("esMajor", 2), ("probe", "dawn"),
                           ("webglCompatibleContext", False), ("robustResourceInitialization", False)]:
            original = self.result[key]
            with self.subTest(key=key):
                self.result[key] = value
                self.assertEqual(self.inspect(), "failed")
                self.result[key] = original

    def test_pixels_and_identity_are_required(self):
        for key in ["verifiedPixels", "adapter", "driver", "expectedRGBA", "tolerance"]:
            original = self.result.pop(key)
            with self.subTest(key=key):
                self.assertEqual(self.inspect(), "failed")
                self.result[key] = original

    def test_skipped_ctest_cannot_be_reported_as_pass(self):
        self.assertEqual(self.inspect(77), "failed")
        self.result["status"] = "unavailable"
        self.assertEqual(self.inspect(77), "unavailable")
        self.assertEqual(self.inspect(0), "failed")

    def test_crash_or_missing_json_is_failed(self):
        self.assertEqual(self.inspect(-6), "failed")
        for output in ["", "some logging", "{incomplete}", "[]", "null", "42"]:
            self.assertEqual(runner.inspect_process("angle", "metal", 3, 0, output)["status"], "failed")


if __name__ == "__main__":
    unittest.main()
