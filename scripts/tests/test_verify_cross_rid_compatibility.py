from __future__ import annotations

import hashlib
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest


REPOSITORY_ROOT = pathlib.Path(__file__).resolve().parents[2]
VERIFIER = REPOSITORY_ROOT / "scripts" / "verify-cross-rid-compatibility.py"
RIDS = ("osx-arm64", "linux-x64", "win-x64")


class CrossRidCompatibilityVerifierTests(unittest.TestCase):
    def test_accepts_complete_evidence_bound_to_normalized_profile(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = pathlib.Path(temporary_directory)
            profile = self.write_profile(root, crlf=True)
            profile_sha256 = self.normalized_sha256(profile)
            input_root = root / "evidence"
            for rid in RIDS:
                self.write_result(input_root, rid, profile_sha256)

            completed, report = self.run_verifier(root, profile, input_root, RIDS)

            self.assertEqual(0, completed.returncode, completed.stderr)
            self.assertTrue(report["passed"])
            self.assertEqual(
                "webscene-cross-rid-compatibility-evidence-v2",
                report["schema"],
            )
            self.assertEqual(profile_sha256, report["profileSha256"])
            self.assertEqual(list(RIDS), report["expectedRids"])
            self.assertTrue(all(item["passed"] for item in report["rids"]))

    def test_rejects_evidence_from_different_manifest_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = pathlib.Path(temporary_directory)
            profile = self.write_profile(root)
            input_root = root / "evidence"
            self.write_result(input_root, RIDS[0], "0" * 64)

            completed, report = self.run_verifier(
                root,
                profile,
                input_root,
                (RIDS[0],),
            )

            self.assertEqual(1, completed.returncode)
            self.assertFalse(report["passed"])
            self.assertTrue(
                any("profileSha256" in issue for issue in report["rids"][0]["issues"]),
                report,
            )

    def test_accepts_blocking_required_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = pathlib.Path(temporary_directory)
            profile = self.write_profile(root)
            profile_sha256 = self.normalized_sha256(profile)
            input_root = root / "evidence"
            self.write_result(
                input_root,
                RIDS[0],
                profile_sha256,
                selection="required",
            )

            completed, report = self.run_verifier(
                root,
                profile,
                input_root,
                (RIDS[0],),
                selection="required",
            )

            self.assertEqual(0, completed.returncode, completed.stderr)
            self.assertTrue(report["passed"])
            self.assertEqual("required", report["selection"])

    @staticmethod
    def write_profile(root: pathlib.Path, *, crlf: bool = False) -> pathlib.Path:
        profile = root / "profile.json"
        content = json.dumps(
            {
                "profile": "cross-rid-test-profile",
                "wptRevision": "test-revision",
                "runtime": "v8",
                "required": [
                    {"path": "contracts/example.html", "type": "testharness"}
                ],
                "candidate": [
                    {"path": "contracts/example.html", "type": "testharness"}
                ],
            },
            indent=2,
        ) + "\n"
        if crlf:
            content = content.replace("\n", "\r\n")
        profile.write_bytes(content.encode("utf-8"))
        return profile

    @staticmethod
    def normalized_sha256(path: pathlib.Path) -> str:
        text = path.read_text(encoding="utf-8-sig")
        normalized = text.replace("\r\n", "\n").replace("\r", "\n")
        return hashlib.sha256(normalized.encode("utf-8")).hexdigest()

    @staticmethod
    def write_result(
        input_root: pathlib.Path,
        rid: str,
        profile_sha256: str,
        *,
        selection: str = "candidate",
    ) -> None:
        artifact_directory = input_root / f"compatibility-{selection}-{rid}-1.0.0"
        artifact_directory.mkdir(parents=True)
        artifact = {
            "schema": "webscene-wpt-subset-result-v3",
            "profile": "cross-rid-test-profile",
            "profileSha256": profile_sha256,
            "wptRevision": "test-revision",
            "runtime": "v8",
            "engine": "native",
            "nativeEngineIdentity": "abi=3;sha256=" + "a" * 64,
            "selection": selection,
            "summary": {
                "tests": 1,
                "passed": 1,
                "failed": 0,
                "timedOut": 0,
                "harnessErrors": 0,
                "subtests": 0,
                "subtestsPassed": 0,
                "subtestsFailed": 0,
            },
            "results": [
                {
                    "path": "contracts/example.html",
                    "type": "testharness",
                    "status": "PASS",
                    "subtests": [],
                }
            ],
        }
        (artifact_directory / "results.json").write_text(
            json.dumps(artifact, indent=2) + "\n",
            encoding="utf-8",
        )

    @staticmethod
    def run_verifier(
        root: pathlib.Path,
        profile: pathlib.Path,
        input_root: pathlib.Path,
        rids: tuple[str, ...],
        *,
        selection: str = "candidate",
    ) -> tuple[subprocess.CompletedProcess[str], dict[str, object]]:
        output = root / "summary.json"
        command = [
            sys.executable,
            str(VERIFIER),
            "--input-root",
            str(input_root),
            "--profile",
            str(profile),
            "--selection",
            selection,
        ]
        for rid in rids:
            command.extend(("--expected-rid", rid))
        command.extend(("--output", str(output)))
        completed = subprocess.run(command, capture_output=True, text=True, check=False)
        return completed, json.loads(output.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
