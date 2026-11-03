#!/usr/bin/env python3
"""Run the Linux executable with isolated state and a private D-Bus session."""

import argparse
import os
from pathlib import Path
import re
import signal
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("executable", type=Path)
    args = parser.parse_args()
    executable = args.executable.resolve(strict=True)
    repository = Path(__file__).resolve().parent.parent
    project_file = (repository / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(r'^set\(SNOW_SHOT_VERSION "([^"]+)"\)', project_file, re.MULTILINE)
    if match is None:
        raise RuntimeError("Could not determine the application version")

    with tempfile.TemporaryDirectory(prefix="snow-shot-linux-startup-") as temporary:
        root = Path(temporary)
        env = os.environ.copy()
        for kind in ("CONFIG", "DATA", "CACHE", "RUNTIME"):
            directory = root / kind.lower()
            directory.mkdir(mode=0o700)
            key = "XDG_RUNTIME_DIR" if kind == "RUNTIME" else f"XDG_{kind}_HOME"
            env[key] = str(directory)
        env.update(TMPDIR=str(root), QT_QPA_PLATFORM="offscreen", XDG_CURRENT_DESKTOP="TEST")

        probe = subprocess.run(
            ["dbus-run-session", "--", str(executable), "--update-probe", match.group(1)],
            cwd=root, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, timeout=60, check=False,
        )
        if probe.returncode:
            raise RuntimeError(f"Startup probe exited {probe.returncode}:\n{probe.stdout}")

        process = subprocess.Popen(
            ["dbus-run-session", "--", str(executable), "--e2e-instance-id=linuxsmoke",
             "--show-main-window"],
            cwd=root, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, start_new_session=True,
        )
        ready = False
        try:
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline and process.poll() is None:
                for log in root.rglob("*.log"):
                    try:
                        if "application.ready" in log.read_text(encoding="utf-8", errors="replace"):
                            ready = True
                            break
                    except OSError:
                        continue
                if ready:
                    break
                time.sleep(0.1)
        finally:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGTERM)
            output, _ = process.communicate(timeout=10)
        if not ready:
            raise RuntimeError(f"Normal startup did not reach application.ready:\n{output}")
        print("Linux startup probe and isolated normal startup passed")


if __name__ == "__main__":
    main()
