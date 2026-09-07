#!/usr/bin/env python3
"""Build a pinned Dawn or ANGLE SDK; requires Git, CMake, Ninja and platform SDKs."""
import argparse
from dawn_exports import inspect_exports
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
LOCK_PATH = Path(__file__).with_name("dependencies.lock.json")
LOCK = json.loads(LOCK_PATH.read_text())


def sha(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run(args, cwd=None, env=None):
    print("+", subprocess.list2cmdline([str(a) for a in args]), flush=True)
    subprocess.run([str(a) for a in args], cwd=cwd, env=env, check=True)


def capture(args, cwd=None):
    return subprocess.check_output([str(a) for a in args], cwd=cwd, text=True).strip()


def host_rid():
    cpu = {"arm64": "arm64", "aarch64": "arm64", "amd64": "x64", "x86_64": "x64"}.get(platform.machine().lower())
    os_name = {"Darwin": "osx", "Windows": "win", "Linux": "linux"}.get(platform.system())
    return f"{os_name}-{cpu}"


def checkout(name, directory):
    source = LOCK["sources"][name]
    if not directory.exists():
        directory.mkdir(parents=True)
        run(["git", "init", directory])
        run(["git", "-C", directory, "remote", "add", "origin", source["repository"]])
    run(["git", "-C", directory, "config", "core.autocrlf", "false"])
    run(["git", "-C", directory, "config", "core.eol", "lf"])
    if capture(["git", "-C", directory, "remote", "get-url", "origin"]) != source["repository"]:
        raise ValueError(f"Wrong source remote: {directory}")
    current = subprocess.run(["git", "-C", str(directory), "rev-parse", "HEAD"], capture_output=True, text=True)
    if current.returncode != 0 or current.stdout.strip() != source["revision"]:
        if capture(["git", "-C", directory, "status", "--porcelain", "--untracked-files=no"]):
            raise ValueError(f"Refusing to overwrite source edits: {directory}")
        run(["git", "-C", directory, "fetch", "--depth", "1", "origin", source["revision"]])
        run(["git", "-C", directory, "checkout", "--detach", source["revision"]])
    verify_source(name, directory)


def verify_source(name, directory):
    source = LOCK["sources"][name]
    if capture(["git", "-C", directory, "rev-parse", "HEAD"]) != source["revision"]:
        raise ValueError(f"Revision mismatch: {name}")
    if capture(["git", "-C", directory, "diff", "HEAD", "--stat"]):
        raise ValueError(f"Tracked source modifications: {name}")
    if "licenseSha256" in source and sha(directory / source["licenseFile"]) != source["licenseSha256"]:
        raise ValueError(f"License checksum mismatch: {name}")


def copy_licenses(source, destination):
    # Preserve upstream and transitive notices instead of attributing all linked code to one license.
    for current, directories, files in os.walk(source):
        directories[:] = [d for d in directories if d not in {".git", "out", "node_modules", "__pycache__"}]
        for name in files:
            if name.upper().startswith(("LICENSE", "COPYING", "NOTICE", "COPYRIGHT")):
                path = Path(current) / name
                if path.is_file():
                    output = destination / path.relative_to(source)
                    output.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(path, output)


def seal(component, source, sdk, rid, settings, tools):
    verify_source(component, source)
    copy_licenses(source, sdk / "licenses")
    source_graph = {}
    for current, directories, files in os.walk(source):
        if ".git" in directories or ".git" in files:
            directory = Path(current)
            if capture(["git", "-C", directory, "diff", "HEAD", "--stat"]):
                raise ValueError(f"Modified transitive dependency: {directory}")
            source_graph[directory.relative_to(source).as_posix()] = capture(
                ["git", "-C", directory, "rev-parse", "HEAD"])
        directories[:] = [d for d in directories if d not in {".git", "out", "node_modules", "__pycache__"}]
    (sdk / "build-info/source-graph.json").write_text(json.dumps(source_graph, indent=2) + "\n")
    files = {p.relative_to(sdk).as_posix(): sha(p) for p in sorted(sdk.rglob("*"))
             if p.is_file() and p.name != "webscene-graphics-package.json"}
    manifest = {
        "schemaVersion": 1, "component": component, "rid": rid,
        "revision": LOCK["sources"][component]["revision"], "lockSha256": sha(LOCK_PATH),
        "settings": settings, "tools": tools, "files": files,
        "buildScriptSha256": sha(Path(__file__).resolve()),
        "qualification": "built; requires hardware probe and platform acceptance evidence",
    }
    (sdk / "webscene-graphics-package.json").write_text(json.dumps(manifest, indent=2) + "\n")


def dawn(args):
    source = args.sources / "dawn"
    checkout("dawn", source)
    output = args.build / f"dawn-{args.rid}"
    sdk = args.sdk / args.rid / "dawn"
    settings = dict(LOCK["dawnCmake"])
    settings["DAWN_ENABLE_" + LOCK["profiles"][args.rid]["dawnBackend"].upper()] = "ON"
    if args.rid.startswith("win-"):
        settings["CMAKE_MSVC_RUNTIME_LIBRARY"] = "MultiThreaded"
    if args.rid.startswith("osx-"):
        settings["CMAKE_OSX_ARCHITECTURES"] = "arm64"
    symbol_policy = Path(__file__).with_name("DawnSymbolBoundary.cmake").resolve()
    run(["cmake", "-S", source, "-B", output, "-G", "Ninja",
         f"-DCMAKE_INSTALL_PREFIX={sdk}", f"-DCMAKE_PROJECT_Dawn_INCLUDE={symbol_policy}",
         "-DCMAKE_SHARED_LINKER_FLAGS="] + [f"-D{k}={v}" for k, v in settings.items()])
    run(["cmake", "--build", output, "--parallel", args.jobs])
    # Old installed headers/libraries must not survive a dependency roll.
    if sdk.exists():
        shutil.rmtree(sdk)
    run(["cmake", "--install", output])
    sdk.joinpath("build-info").mkdir()
    shutil.copy2(symbol_policy, sdk / "build-info/DawnSymbolBoundary.cmake")
    binary = sdk / {"win-x64": "bin/webgpu_dawn.dll", "osx-arm64": "lib/libwebgpu_dawn.dylib",
                    "linux-x64": "lib/libwebgpu_dawn.so"}[args.rid]
    (sdk / "build-info/exports.json").write_text(json.dumps(inspect_exports(binary, args.rid), indent=2) + "\n")
    shutil.copy2(output / "CMakeCache.txt", sdk / "build-info/CMakeCache.txt")
    for name in ["CMakeCCompiler.cmake", "CMakeCXXCompiler.cmake", "CMakeSystem.cmake"]:
        metadata = max(output.glob("CMakeFiles/*/" + name), key=lambda p: p.stat().st_mtime)
        shutil.copy2(metadata, sdk / "build-info" / name)
    shutil.copy2(source / "DEPS", sdk / "build-info/DEPS")
    seal("dawn", source, sdk, args.rid, settings,
         {"cmake": capture(["cmake", "--version"]), "ninja": capture(["ninja", "--version"]),
          "python": sys.version, "host": platform.platform()})


def angle(args):
    depot = args.sources / "depot_tools"
    checkout("depot-tools", depot)
    workspace = args.sources / "angle-workspace"
    source = workspace / "angle"
    checkout("angle", source)
    solution = [{"name": "angle", "url": LOCK["sources"]["angle"]["repository"],
                 "managed": False, "custom_deps": {}, "custom_vars": LOCK["angleCustomVars"]}]
    (workspace / ".gclient").write_text("solutions = " + repr(solution) + "\n")
    env = dict(os.environ, DEPOT_TOOLS_UPDATE="0", DEPOT_TOOLS_WIN_TOOLCHAIN="0")
    # Apply LF policy to gclient's transitive Git checkouts without changing user configuration.
    env.update(GIT_CONFIG_COUNT="2", GIT_CONFIG_KEY_0="core.autocrlf", GIT_CONFIG_VALUE_0="false",
               GIT_CONFIG_KEY_1="core.eol", GIT_CONFIG_VALUE_1="lf")
    env["PATH"] = str(depot) + os.pathsep + env["PATH"]
    gclient = depot / ("gclient.bat" if os.name == "nt" else "gclient")
    run([gclient, "sync", "--shallow", "--no-history", "--revision",
         "angle@" + LOCK["sources"]["angle"]["revision"]], cwd=workspace, env=env)
    verify_source("angle", source)
    profile = LOCK["profiles"][args.rid]
    settings = dict(LOCK["angleGn"], target_cpu=profile["cpu"])
    settings["angle_enable_" + profile["angleBackend"].lower()] = True
    if args.angle_gl:
        settings["angle_enable_gl"] = True
    output = args.build / ("angle-" + args.rid + ("-gl" if args.angle_gl else ""))
    output.mkdir(parents=True, exist_ok=True)
    (output / "args.gn").write_text("\n".join(k + " = " + json.dumps(v) for k, v in settings.items()) + "\n")
    gn_os = {"win": "win", "osx": "mac", "linux": "linux64"}[args.rid.split("-")[0]]
    gn = source / "buildtools" / gn_os / ("gn.exe" if os.name == "nt" else "gn")
    run([gn, "gen", output, "--fail-on-unused-args"], cwd=source, env=env)
    run(["ninja", "-C", output, "-j", args.jobs, "libEGL", "libGLESv2"], cwd=source, env=env)
    sdk = args.sdk / args.rid / ("angle-gl" if args.angle_gl else "angle")
    if sdk.exists():
        shutil.rmtree(sdk)
    shutil.copytree(source / "include", sdk / "include")
    if args.rid.startswith("linux-"):
        vulkan_headers = source / "third_party/vulkan-headers/src/include"
        for directory in ["vulkan", "vk_video"]:
            # ANGLE already ships include/vulkan/vulkan_fuchsia_ext.h.
            shutil.copytree(vulkan_headers / directory, sdk / "include" / directory, dirs_exist_ok=True)
    (sdk / "lib").mkdir()
    suffixes = {"win": [".dll", ".lib"], "osx": [".dylib"], "linux": [".so"]}[args.rid.split("-")[0]]
    for library in ["libEGL", "libGLESv2"]:
        for suffix in suffixes:
            shutil.copy2(output / (library + suffix), sdk / "lib" / (library + suffix))
    if args.rid.startswith("osx-"):
        # GN emits ./lib*.dylib IDs. Those resolve against process cwd rather than SDK location.
        # Normalize only packaged copies and sign after modification; source/build outputs stay intact.
        for library in ["libEGL", "libGLESv2"]:
            path = sdk / "lib" / (library + ".dylib")
            run(["install_name_tool", "-id", "@rpath/" + path.name, path])
            for dependency in ["libEGL.dylib", "libGLESv2.dylib"]:
                run(["install_name_tool", "-change", "./" + dependency,
                     "@loader_path/" + dependency, path])
            run(["codesign", "--force", "--sign", "-", path])
    (sdk / "build-info").mkdir()
    shutil.copy2(output / "args.gn", sdk / "build-info/args.gn")
    (sdk / "build-info/resolved-args.gn").write_text(capture([gn, "args", output, "--list", "--short"], source) + "\n")
    shutil.copy2(source / "DEPS", sdk / "build-info/DEPS")
    clang = source / "third_party/llvm-build/Release+Asserts/bin" / ("clang.exe" if os.name == "nt" else "clang")
    seal("angle", source, sdk, args.rid, settings,
         {"gn": capture([gn, "--version"]), "ninja": capture(["ninja", "--version"]),
          "clang": capture([clang, "--version"]), "clangSha256": sha(clang),
          "depotTools": LOCK["sources"]["depot-tools"]["revision"], "host": platform.platform()})


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("component", choices=["dawn", "angle"])
    parser.add_argument("--rid", choices=list(LOCK["profiles"]), default=host_rid())
    parser.add_argument("--sources", type=Path, default=ROOT / "artifacts/graphics-src")
    parser.add_argument("--build", type=Path, default=ROOT / "artifacts/graphics-build")
    parser.add_argument("--sdk", type=Path, default=ROOT / "artifacts/graphics-sdk")
    parser.add_argument("--jobs", type=int, default=min(8, os.cpu_count() or 1))
    parser.add_argument("--angle-gl", action="store_true", help="Separate ANGLE SDK with an explicit GL backend option")
    args = parser.parse_args()
    if args.rid != host_rid():
        parser.error("These baseline builds require a native host of the requested RID; cross compilation is not qualification.")
    if args.jobs < 1 or (args.angle_gl and args.component != "angle"):
        parser.error("Jobs must be positive; --angle-gl applies only to ANGLE.")
    for key in ["sources", "build", "sdk"]:
        setattr(args, key, getattr(args, key).resolve())
    try:
        {"dawn": dawn, "angle": angle}[args.component](args)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f"Graphics build failed: {error}", file=sys.stderr)
        sys.exit(1)
