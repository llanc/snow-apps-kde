#!/usr/bin/env python3
"""Install per-user KDE menu entries for the existing development build."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


DESKTOP_MARKER = "X-SnowAppsKde-Managed=true"
ICON_MARKER = b"<!-- Managed by Snow Apps KDE -->"


def desktop_argument(value):
    """Quote one literal argument for both desktop-file and Exec parsing."""
    if any(character in value for character in "\x00\r\n"):
        raise ValueError("Desktop entry paths cannot contain control characters")
    value = value.replace("%", "%%")
    for character in ("\\", '"', "$", "`"):
        value = value.replace(character, "\\" + character)
    return '"' + value.replace("\\", "\\\\") + '"'


def install_files(repo_root, data_home, *, dry_run=False, refresh=True):
    repo_root = Path(repo_root).resolve(strict=True)
    data_home = Path(data_home)
    if not data_home.is_absolute():
        raise ValueError("XDG_DATA_HOME must be an absolute path")
    shot = repo_root / "build/linux-snow-shot-clang/snow_shot/snow_shot"
    viewer = repo_root / "build/linux-snow-shot-clang/snow_image_viewer/snow_image_viewer"
    shot_icon = repo_root / "snow_shot/resources/app-icon.svg"
    viewer_icon = repo_root / "snow_image_viewer/resources/app-icon.svg"
    for executable in (shot, viewer):
        if not executable.is_file() or not os.access(executable, os.X_OK):
            raise ValueError(f"Missing executable: {executable}; run scripts/build-linux-kde.sh first")
    for icon in (shot_icon, viewer_icon):
        if not icon.is_file():
            raise ValueError(f"Missing application icon: {icon}")

    applications = data_home / "applications"
    icons = data_home / "icons/hicolor/scalable/apps"
    shot_desktop = applications / "io.github.llanc.SnowShotKDE.desktop"
    viewer_desktop = applications / "io.github.llanc.SnowImageViewerKDE.desktop"
    shot_icon_target = icons / "io.github.llanc.SnowShotKDE.svg"
    viewer_icon_target = icons / "io.github.llanc.SnowImageViewerKDE.svg"
    common = "Type=Application\nTerminal=false\nCategories=Graphics;2DGraphics;\n" + DESKTOP_MARKER
    entries = {
        shot_desktop: (
            "[Desktop Entry]\nName=Snow Shot (KDE)\n" + common +
            "\nIcon=io.github.llanc.SnowShotKDE\nExec=/usr/bin/env " + desktop_argument(str(shot)) +
            " --show-main-window\n"
        ).encode("utf-8"),
        viewer_desktop: (
            "[Desktop Entry]\nName=Snow Image Viewer (KDE)\n" + common +
            "\nIcon=io.github.llanc.SnowImageViewerKDE\nExec=/usr/bin/env " + desktop_argument(str(viewer)) +
            " %f\nMimeType=image/png;image/jpeg;image/gif;image/webp;image/avif;image/heif;image/jxl;\n"
        ).encode("utf-8"),
        shot_icon_target: shot_icon.read_bytes() + b"\n" + ICON_MARKER + b"\n",
        viewer_icon_target: viewer_icon.read_bytes() + b"\n" + ICON_MARKER + b"\n",
    }
    for path in entries:
        if path.is_symlink():
            raise ValueError(f"Refusing to replace a symbolic link: {path}")
        if path.exists():
            marker = DESKTOP_MARKER.encode() if path.suffix == ".desktop" else ICON_MARKER
            if not path.is_file() or marker not in path.read_bytes():
                raise ValueError(f"Refusing to replace an unrelated file: {path}")
    if dry_run:
        return list(entries)

    staged = []
    try:
        for path, content in entries.items():
            path.parent.mkdir(parents=True, exist_ok=True)
            with tempfile.NamedTemporaryFile(dir=path.parent, prefix=".snow-kde-", suffix=path.suffix,
                                             delete=False) as file:
                temporary = Path(file.name)
                staged.append(temporary)
                file.write(content)
            temporary.chmod(0o644)
            if path.suffix == ".desktop" and shutil.which("desktop-file-validate"):
                subprocess.run(["desktop-file-validate", str(temporary)], check=True)
        for temporary, path in zip(staged, entries, strict=True):
            os.replace(temporary, path)
    finally:
        for temporary in staged:
            temporary.unlink(missing_ok=True)

    if refresh:
        for command in (["update-desktop-database", str(applications)], ["kbuildsycoca6"]):
            if shutil.which(command[0]):
                try:
                    subprocess.run(command, check=True, timeout=20, stdout=subprocess.DEVNULL)
                except (OSError, subprocess.SubprocessError) as error:
                    print(f"Could not refresh {command[0]}: {error}", file=sys.stderr)
    return list(entries)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dry-run", action="store_true", help="Show target paths without writing")
    args = parser.parse_args()
    data_home = os.environ.get("XDG_DATA_HOME") or str(Path.home() / ".local/share")
    try:
        paths = install_files(Path(__file__).resolve().parent.parent, data_home, dry_run=args.dry_run)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"Installation failed: {error}", file=sys.stderr)
        return 1
    print("Would install:" if args.dry_run else "Installed KDE menu entries:")
    for path in paths:
        print(f"  {path}")
    if not args.dry_run:
        print("The menu entries use this checkout's build; keep the project directory in place.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
