"""Exercise SDK metadata capture without building the upstream compiler tree."""
from contextlib import ExitStack
import importlib.util
import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

HERE = Path(__file__).parents[1]
sys.path.insert(0, str(HERE))
spec = importlib.util.spec_from_file_location("graphics_build_environment", HERE / "build.py")
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


class AngleEnvironmentTests(unittest.TestCase):
    def test_capture_passes_explicit_environment_to_child_process(self):
        env = dict(os.environ, WEBSCENE_SDK_TEST_VALUE="local-toolchain")
        result = builder.capture([sys.executable, "-c",
                                  "import os; print(os.environ['WEBSCENE_SDK_TEST_VALUE'])"], env=env)
        self.assertEqual(result, "local-toolchain")

    def test_windows_metadata_uses_generation_environment(self):
        with tempfile.TemporaryDirectory() as directory, ExitStack() as stack:
            root = Path(directory)
            args = SimpleNamespace(sources=root / "sources", build=root / "build",
                                   sdk=root / "sdk", rid="win-x64", jobs=2, angle_gl=False)
            depot = args.sources / "depot_tools"
            depot.mkdir(parents=True)
            (depot / "git.bat").touch()
            source = args.sources / "angle-workspace/angle"
            source.mkdir(parents=True)
            sdk = args.sdk / args.rid / "angle"
            sdk.mkdir(parents=True)
            for name in ("checkout", "verify_source", "seal"):
                stack.enter_context(patch.object(builder, name))
            for name in ("copytree", "copy2", "rmtree"):
                stack.enter_context(patch.object(builder.shutil, name))
            stack.enter_context(patch.object(builder, "sha", return_value="test-hash"))
            run = stack.enter_context(patch.object(builder, "run"))
            capture = stack.enter_context(patch.object(builder, "capture", return_value="metadata"))
            # Exercise the one-token Windows bootstrap command on every host,
            # so command selection cannot accidentally assume an argument exists.
            run([depot / "bootstrap/win_tools.bat"], cwd=depot)
            # Simulate a caller configured to use Chromium's downloaded toolchain.
            stack.enter_context(patch.dict(os.environ, DEPOT_TOOLS_WIN_TOOLCHAIN="1"))
            builder.angle(args)
            generation = next(call for call in run.call_args_list if call.args[0][1:2] == ["gen"])
            metadata = next(call for call in capture.call_args_list if call.args[0][1:2] == ["args"])
            self.assertIs(metadata.kwargs["env"], generation.kwargs["env"])
            self.assertEqual(metadata.kwargs["env"]["DEPOT_TOOLS_WIN_TOOLCHAIN"], "0")
            self.assertEqual(metadata.kwargs["env"]["DEPOT_TOOLS_UPDATE"], "0")
            self.assertTrue(metadata.kwargs["env"]["PATH"].startswith(str(depot) + os.pathsep))
            self.assertEqual((sdk / "build-info/resolved-args.gn").read_text(), "metadata\n")
            self.assertEqual(os.environ["DEPOT_TOOLS_WIN_TOOLCHAIN"], "1")
