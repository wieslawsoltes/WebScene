"""Keep headless callback measurements separate from physical presentation gates."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("resize_compare", Path(__file__).with_name("compare-native-resize-cadence.py"))
compare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compare)


class ResizeMeasurementScopeTests(unittest.TestCase):
    def test_legacy_ambiguous_measurement_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "sample.json").write_text(json.dumps({"schema": "webscene-native-resize-cadence-v1"}))
            with self.assertRaises(RuntimeError):
                compare.read_samples(root, 1)

    def test_good_cpu_cadence_cannot_pass_physical_vsync(self):
        sample = {"schema": "webscene-native-resize-cadence-v2",
                  "cpuCadenceGate": {"passed": True},
                  "renderedFramesPerSecond": 60,
                  "drawCallbackCompletionsPerSecond": 60,
                  "normalizedProcessCpuPercent": 10,
                  "layoutPassesPerAppliedResize": 1,
                  "dispatchMilliseconds": {"average": 1}}
        for name in ["renderLatencyMilliseconds", "publicationLatencyMilliseconds",
                     "publicationToRenderLatencyMilliseconds", "drawCallbackIntervalMilliseconds"]:
            sample[name] = {"p95": 1, "maximum": 2}
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ["control", "candidate"]:
                (root / name).mkdir()
                (root / name / "sample.json").write_text(json.dumps(sample))
            output = root / "comparison.json"
            args = ["compare", "--control-dir", str(root / "control"),
                    "--candidate-dir", str(root / "candidate"), "--minimum-samples", "1",
                    "--output", str(output), "--require-vsync"]
            with patch.object(sys, "argv", args), contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(compare.main(), 1)
            report = json.loads(output.read_text())
            self.assertFalse(report["passed"])
            self.assertFalse(report["physicalPresentationVerified"])
            self.assertEqual(report["candidateCpuCadencePasses"], 1)


if __name__ == "__main__":
    unittest.main()
