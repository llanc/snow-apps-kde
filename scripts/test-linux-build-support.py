#!/usr/bin/env python3
"""Focused checks for the Linux workspace build configuration."""

from pathlib import Path
import subprocess
import unittest


ROOT = Path(__file__).resolve().parent.parent
FIXTURE = ROOT / "test-support/linux-build/default-triplet.cmake"


class LinuxBuildSupportTests(unittest.TestCase):
    def check_triplet(self, processor, expected, override=None):
        command = ["cmake", f"-DTEST_SOURCE_DIR={ROOT}", f"-DTEST_PROCESSOR={processor}",
                   f"-DTEST_EXPECTED={expected}"]
        if override is not None:
            command.append(f"-DTEST_OVERRIDE={override}")
        return subprocess.run([*command, "-P", str(FIXTURE)], text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)

    def test_supported_architectures(self):
        for processor, expected in (("x86_64", "x64-linux"), ("AMD64", "x64-linux"),
                                    ("aarch64", "arm64-linux"), ("arm64", "arm64-linux")):
            with self.subTest(processor=processor):
                result = self.check_triplet(processor, expected)
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_unknown_architecture_requires_explicit_triplet(self):
        result = self.check_triplet("riscv64", "")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Unsupported Linux architecture", result.stderr)
        result = self.check_triplet("riscv64", "custom-linux", "custom-linux")
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
