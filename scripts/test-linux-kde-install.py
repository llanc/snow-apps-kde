#!/usr/bin/env python3
"""Test user menu installation with temporary files and a private D-Bus."""

import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import unittest


SPEC = importlib.util.spec_from_file_location(
    "snow_linux_installer", Path(__file__).with_name("install-linux-kde.py"))
INSTALLER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(INSTALLER)


class LinuxKdeInstallTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="snow-kde-install-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.repo = self.root / 'checkout % " $ ` \\ with spaces'
        self.data = self.root / "xdg-data"
        for name in ("snow_shot/snow_shot", "snow_image_viewer/snow_image_viewer"):
            executable = self.repo / "build/linux-snow-shot-clang" / name
            executable.parent.mkdir(parents=True, exist_ok=True)
            executable.write_text('#!/bin/sh\nprintf "%s\\n" "$@" > "$SNOW_INSTALL_TEST_MARKER"\n',
                                  encoding="utf-8")
            executable.chmod(0o755)
        for name in ("snow_shot", "snow_image_viewer"):
            icon = self.repo / name / "resources/app-icon.svg"
            icon.parent.mkdir(parents=True, exist_ok=True)
            icon.write_text('<svg xmlns="http://www.w3.org/2000/svg"/>\n', encoding="utf-8")

    def test_install_and_launch_with_special_paths(self):
        targets = INSTALLER.install_files(self.repo, self.data, refresh=False)
        self.assertEqual(len(targets), 4)
        original = {path: path.read_bytes() for path in targets}
        INSTALLER.install_files(self.repo, self.data, refresh=False)
        self.assertEqual(original, {path: path.read_bytes() for path in targets})
        if not shutil.which("gio") or not shutil.which("dbus-run-session"):
            self.skipTest("gio and dbus-run-session are required for desktop launch validation")

        image = self.root / "image with spaces %.png"
        image.touch()
        environment = os.environ.copy()
        environment.update(XDG_DATA_HOME=str(self.data), XDG_CONFIG_HOME=str(self.root / "config"),
                           XDG_CURRENT_DESKTOP="TEST")
        for desktop, arguments, expected in (
            (targets[0], [], "--show-main-window"),
            (targets[1], [str(image)], str(image)),
        ):
            marker = self.root / (desktop.stem + ".txt")
            environment["SNOW_INSTALL_TEST_MARKER"] = str(marker)
            launched = subprocess.run(
                ["dbus-run-session", "--", "gio", "launch", str(desktop), *arguments],
                env=environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                text=True, check=False, timeout=15)
            self.assertEqual(launched.returncode, 0, launched.stderr)
            deadline = time.monotonic() + 5
            while not marker.exists() and time.monotonic() < deadline:
                time.sleep(0.02)
            self.assertTrue(marker.exists(), f"{desktop.name} did not execute")
            self.assertEqual(marker.read_text(encoding="utf-8").strip(), expected)

    def test_foreign_files_and_symlinks_are_not_overwritten(self):
        target = self.data / "applications/io.github.llanc.SnowShotKDE.desktop"
        target.parent.mkdir(parents=True)
        target.write_text("not managed", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "unrelated file"):
            INSTALLER.install_files(self.repo, self.data, refresh=False)
        self.assertEqual(target.read_text(encoding="utf-8"), "not managed")
        self.assertFalse((target.parent / "io.github.llanc.SnowImageViewerKDE.desktop").exists())
        target.unlink()
        target.symlink_to(self.root / "missing")
        with self.assertRaisesRegex(ValueError, "symbolic link"):
            INSTALLER.install_files(self.repo, self.data, refresh=False)
        self.assertTrue(target.is_symlink())

    def test_dry_run_and_missing_build_do_not_write(self):
        paths = INSTALLER.install_files(self.repo, self.data, dry_run=True)
        self.assertEqual(len(paths), 4)
        self.assertFalse(self.data.exists())
        (self.repo / "build/linux-snow-shot-clang/snow_shot/snow_shot").unlink()
        with self.assertRaisesRegex(ValueError, "run scripts/build-linux-kde.sh"):
            INSTALLER.install_files(self.repo, self.data)
        self.assertFalse(self.data.exists())


if __name__ == "__main__":
    unittest.main()
