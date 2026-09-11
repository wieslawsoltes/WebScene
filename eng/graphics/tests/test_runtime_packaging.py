import hashlib
import importlib.util
import io
import json
from pathlib import Path
import unittest
import zipfile

spec = importlib.util.spec_from_file_location("release_packages", Path(__file__).resolve().parents[3] / "scripts/verify-release-packages.py")
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)

class RuntimePackagingTests(unittest.TestCase):
    def package(self, rid, omit=None, corrupt=None, extra_angle=False):
        libraries = ["libwebgpu_dawn.dylib"] if rid == "osx-arm64" else ["webgpu_dawn.dll", "libEGL.dll", "libGLESv2.dll", "d3dcompiler_47.dll"]
        data = io.BytesIO()
        with zipfile.ZipFile(data, "w") as z:
            prefix = f"runtimes/{rid}/native/"
            manifest = {"rid": rid, "libraries": {n: hashlib.sha256(n.encode()).hexdigest() for n in libraries}, "components": {n: {} for n in (["dawn"] if rid == "osx-arm64" else ["dawn", "angle"])}}
            z.writestr(prefix + "webscene-graphics-runtime.json", json.dumps(manifest))
            z.writestr("buildTransitive/graphics/WebScene.NativeEngine.Graphics.targets", "<Project/>")
            for name in libraries:
                if name != omit: z.writestr(prefix + name, b"corrupt" if name == corrupt else name.encode())
            if extra_angle: z.writestr(prefix + "libEGL.dylib", b"unexpected")
        data.seek(0)
        return zipfile.ZipFile(data)

    def test_platform_dependency_sets(self):
        for rid in ("osx-arm64", "win-x64"):
            with self.package(rid) as z: release.validate_graphics_runtime(z, rid)

    def test_missing_or_corrupt_windows_compiler_is_rejected(self):
        for kw in ({"omit": "d3dcompiler_47.dll"}, {"corrupt": "d3dcompiler_47.dll"}):
            with self.package("win-x64", **kw) as z, self.assertRaises(RuntimeError):
                release.validate_graphics_runtime(z, "win-x64")

    def test_macos_angle_is_rejected(self):
        with self.package("osx-arm64", extra_angle=True) as z, self.assertRaises(RuntimeError):
            release.validate_graphics_runtime(z, "osx-arm64")

    def test_previous_non_graphics_macos_package_is_rejected(self):
        data = io.BytesIO()
        with zipfile.ZipFile(data, "w"): pass
        data.seek(0)
        with zipfile.ZipFile(data) as z, self.assertRaises(RuntimeError):
            release.validate_graphics_runtime(z, "osx-arm64")
