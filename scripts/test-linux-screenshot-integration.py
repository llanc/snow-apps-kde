#!/usr/bin/env python3
"""Exercise the real screenshot workflow with a synthetic image and private D-Bus."""

import argparse
import base64
import json
import os
from pathlib import Path
import re
import select
import signal
import struct
import subprocess
import sys
import tempfile
import time


def isolated_environment(root):
    env = os.environ.copy()
    for kind in ("CONFIG", "DATA", "CACHE", "RUNTIME"):
        directory = root / kind.lower()
        directory.mkdir(mode=0o700)
        key = "XDG_RUNTIME_DIR" if kind == "RUNTIME" else f"XDG_{kind}_HOME"
        env[key] = str(directory)
    env.update(TMPDIR=str(root), QT_QPA_PLATFORM="offscreen", XDG_CURRENT_DESKTOP="TEST")
    return env


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("application", type=Path)
    parser.add_argument("bridge", type=Path)
    parser.add_argument("fake_portal", type=Path)
    parser.add_argument("--wayland", action="store_true",
                        help="Run against an isolated virtual KWin compositor")
    parser.add_argument("--kde-shortcut-probe", type=Path,
                        help="Also test KDE shortcuts using this probe executable")
    parser.add_argument("--viewer-probe", type=Path,
                        help="Also open a synthetic image with the viewer test executable")
    parser.add_argument("--picker-probe", type=Path,
                        help="Also exercise the overlay color picker")
    parser.add_argument("--picker-host-probe", type=Path,
                        help="Also exercise mouse movements through the overlay")
    parser.add_argument("--tray-watcher", type=Path,
                        help="Also exercise the exported KDE tray menu with a fake watcher")
    args = parser.parse_args()
    application = args.application.resolve(strict=True)
    bridge = args.bridge.resolve(strict=True)
    fake_portal = args.fake_portal.resolve(strict=True)
    shortcut_probe = args.kde_shortcut_probe.resolve(strict=True) if args.kde_shortcut_probe else None
    viewer_probe = args.viewer_probe.resolve(strict=True) if args.viewer_probe else None
    picker_probe = args.picker_probe.resolve(strict=True) if args.picker_probe else None
    picker_host_probe = args.picker_host_probe.resolve(strict=True) if args.picker_host_probe else None
    tray_watcher = args.tray_watcher.resolve(strict=True) if args.tray_watcher else None
    if shortcut_probe and not args.wayland:
        parser.error("--kde-shortcut-probe requires --wayland")
    if (picker_probe or picker_host_probe or tray_watcher) and not args.wayland:
        parser.error("--picker-probe, --picker-host-probe and --tray-watcher require --wayland")
    if os.environ.get("SNOW_LINUX_PRIVATE_TEST_BUS") != "1":
        with tempfile.TemporaryDirectory(prefix="snow-shot-linux-bus-") as temporary:
            env = isolated_environment(Path(temporary))
            env["SNOW_LINUX_PRIVATE_TEST_BUS"] = "1"
            command = ["dbus-run-session", "--", sys.executable, __file__, str(application),
                       str(bridge), str(fake_portal)]
            if args.wayland:
                command.append("--wayland")
            if shortcut_probe:
                command.extend(["--kde-shortcut-probe", str(shortcut_probe)])
            if viewer_probe:
                command.extend(["--viewer-probe", str(viewer_probe)])
            if picker_probe:
                command.extend(["--picker-probe", str(picker_probe)])
            if picker_host_probe:
                command.extend(["--picker-host-probe", str(picker_host_probe)])
            if tray_watcher:
                command.extend(["--tray-watcher", str(tray_watcher)])
            return subprocess.call(command, env=env)

    sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "snow_shot/tests"))
    from mcp_fixture_tests import Client

    with tempfile.TemporaryDirectory(prefix="snow-shot-linux-capture-") as temporary:
        root = Path(temporary)
        env = isolated_environment(root)
        descriptor = root / "descriptor.json"
        env["SNOW_SHOT_MCP_DESCRIPTOR"] = str(descriptor)
        config = root / "data/SnowShot/snow_shot-e2e-linuxcapture/config.json"
        config.parent.mkdir(parents=True)
        config.write_text(json.dumps({
            "storage": {"schema_version": 4}, "mcp": {"enabled": True},
            "tray": {"enabled": bool(tray_watcher),
                     "menu_options": ["quick.screenshot", "tray.window-grouping",
                                      "tray.show-main-window", "tray.exit"]},
            "interface": {"language": "en_US"},
        }), encoding="utf-8")

        compositor = None
        portal = None
        watcher = None
        application_process = None
        client = None
        with (root / "application.log").open("w+", encoding="utf-8") as log, \
                (root / "compositor.log").open("w+", encoding="utf-8") as compositor_log:
            try:
                if args.wayland:
                    compositor_env = dict(env, XDG_CURRENT_DESKTOP="KDE", LIBGL_ALWAYS_SOFTWARE="1")
                    compositor_command = [
                        "kwin_wayland", "--virtual", "--no-lockscreen", "--no-kactivities",
                        "--socket", "snow-test", "--width", "800", "--height", "600",
                    ]
                    if not shortcut_probe:
                        compositor_command.append("--no-global-shortcuts")
                    compositor = subprocess.Popen(
                        compositor_command, cwd=root, env=compositor_env,
                        stdout=compositor_log, stderr=compositor_log, start_new_session=True,
                    )
                    socket = root / "runtime/snow-test"
                    deadline = time.monotonic() + 20
                    while not socket.exists() and compositor.poll() is None:
                        if time.monotonic() >= deadline:
                            break
                        time.sleep(0.05)
                    if not socket.exists():
                        raise RuntimeError("The isolated KWin compositor did not start")
                    env.update(QT_QPA_PLATFORM="wayland", XDG_SESSION_TYPE="wayland",
                               XDG_CURRENT_DESKTOP="KDE", WAYLAND_DISPLAY="snow-test")
                    if shortcut_probe:
                        subprocess.run([str(shortcut_probe)], cwd=root,
                                       env=dict(env, XDG_CURRENT_DESKTOP="KDE"),
                                       check=True, timeout=20)
                        print("KDE global shortcut registration and release passed")
                if tray_watcher:
                    watcher = subprocess.Popen([str(tray_watcher)], cwd=root, env=env,
                                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                               text=True, start_new_session=True)
                    readable, _, _ = select.select([watcher.stdout], [], [], 15)
                    if not readable or watcher.stdout.readline().strip() != "ready":
                        raise RuntimeError("The fake status notifier watcher did not start")
                portal = subprocess.Popen(
                    [str(fake_portal), str(root / "synthetic.png"), "800",
                     "600" if args.wayland else "800"], cwd=root, env=env,
                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                    start_new_session=True,
                )
                readable, _, _ = select.select([portal.stdout], [], [], 15)
                if not readable or portal.stdout.readline().strip() != "ready":
                    raise RuntimeError("The synthetic screenshot portal did not start")
                if picker_probe:
                    subprocess.run([str(picker_probe), "--embedded-picker-only"], cwd=root,
                                   env=env, check=True, timeout=20)
                    print("Wayland screenshot color picker follows overlay coordinates")
                if picker_host_probe:
                    subprocess.run([str(picker_host_probe), "--overlay-mouse-picker-only"], cwd=root,
                                   env=env, check=True, timeout=20)
                    print("Wayland screenshot mouse events update the picker")
                if viewer_probe:
                    subprocess.run([str(viewer_probe)], cwd=root, env=env, check=True, timeout=20)
                    print("Image viewer opened a synthetic PNG")
                application_process = subprocess.Popen(
                    [str(application), "--e2e-instance-id=linuxcapture"], cwd=root, env=env,
                    stdout=log, stderr=log, start_new_session=True,
                )
                deadline = time.monotonic() + 30
                while not descriptor.is_file() and application_process.poll() is None:
                    if time.monotonic() >= deadline:
                        break
                    time.sleep(0.05)
                if not descriptor.is_file():
                    log.flush()
                    raise RuntimeError("MCP endpoint did not start:\n" + log.name)
                client = Client(bridge, descriptor)
                if watcher:
                    readable, _, _ = select.select([watcher.stdout], [], [], 15)
                    registration = watcher.stdout.readline().strip() if readable else ""
                    parts = registration.split()
                    if len(parts) != 3 or parts[0] != "registered":
                        raise RuntimeError(f"The application did not register a tray icon: {registration}")
                    service, item_path = parts[1:]
                    def dbus_call(path, method, *arguments):
                        return subprocess.check_output([
                            "gdbus", "call", "--session", "--dest", service, "--object-path", path,
                            "--method", method, *arguments], text=True, timeout=10)
                    item_menu = dbus_call(item_path, "org.freedesktop.DBus.Properties.Get",
                                          "org.kde.StatusNotifierItem", "Menu")
                    menu_path_match = re.search(r"objectpath '([^']+)'", item_menu)
                    if not menu_path_match:
                        raise RuntimeError(f"The tray icon did not export a menu: {item_menu}")
                    menu_path = menu_path_match.group(1)
                    dbus_call(menu_path, "com.canonical.dbusmenu.AboutToShow", "0")
                    layout = dbus_call(menu_path, "com.canonical.dbusmenu.GetLayout", "0", "1",
                                       "['label', 'visible', 'enabled']")
                    if "Screenshot" not in layout or "Exit" not in layout:
                        raise RuntimeError(f"The KDE tray menu has no expected actions: {layout}")
                    group = re.search(r"<\((\d+), \{[^}]*'label': <'Window Group: [^']+'>", layout)
                    if not group:
                        raise RuntimeError(f"The tray group submenu is missing: {layout}")
                    dbus_call(menu_path, "com.canonical.dbusmenu.AboutToShow", group.group(1))
                    submenu = dbus_call(menu_path, "com.canonical.dbusmenu.GetLayout", group.group(1),
                                        "1", "['label', 'visible', 'enabled']")
                    if "New Group" not in submenu:
                        raise RuntimeError(f"The tray group submenu did not populate: {submenu}")
                for presentation in ("silent", "visible"):
                    begun = client.tool("snow_shot_screenshot_begin", {
                        "presentation": presentation, "target": "current_monitor",
                        "capture_cursor": False,
                    })["result"]
                    session_id = begun["session_id"]
                    state = client.tool("snow_shot_screenshot_state", {
                        "session_id": session_id,
                    })["result"]
                    x, y, width, height = state["canvas_bounds"]
                    if not state["active"] or width < 100 or height < 100:
                        raise RuntimeError(f"Invalid screenshot session: {state}")
                    selected = client.tool("snow_shot_screenshot_set_selection", {
                        "session_id": session_id, "expected_revision": begun["revision"],
                        "type": "rectangle", "bounds": [x + 10, y + 10, 90, 90],
                    })["result"]
                    rendered = client.request("tools/call", {
                        "name": "snow_shot_screenshot_render",
                        "arguments": {"session_id": session_id,
                                      "expected_revision": selected["revision"]},
                    })
                    if rendered.get("isError"):
                        raise RuntimeError(f"Screenshot render failed: {rendered}")
                    block = next(item for item in rendered["content"] if item["type"] == "image")
                    png = base64.b64decode(block["data"], validate=True)
                    if png[:8] != b"\x89PNG\r\n\x1a\n" or struct.unpack(">II", png[16:24]) != (90, 90):
                        raise RuntimeError("The selected synthetic screenshot did not render")
                    client.tool("snow_shot_screenshot_cancel", {"session_id": session_id})
                if watcher:
                    # Execute an exported action as a tray host, using only isolated state.
                    match = re.search(r"<\((\d+), \{[^}]*'label': <'Exit'>[^}]*\}", layout)
                    if not match:
                        raise RuntimeError(f"The exported tray Exit action was not found: {layout}")
                    dbus_call(menu_path, "com.canonical.dbusmenu.Event", match.group(1), "clicked",
                              "<0>", "0")
                    if application_process.wait(timeout=15) != 0:
                        raise RuntimeError("The exported tray Exit action did not close the app")
                    print("KDE tray menu exports and activates its configured actions")
                backend = "virtual KWin Wayland" if args.wayland else "Qt offscreen"
                print(f"Linux screenshot acquisition, selection and render passed ({backend})")
            except Exception:
                log.flush()
                log.seek(0)
                print(log.read()[-5000:], file=sys.stderr)
                compositor_log.flush()
                compositor_log.seek(0)
                print(compositor_log.read()[-2000:], file=sys.stderr)
                raise
            finally:
                if client is not None:
                    client.close()
                for process in (application_process, portal, watcher, compositor):
                    if process is not None and process.poll() is None:
                        os.killpg(process.pid, signal.SIGTERM)
                    if process is not None:
                        process.wait(timeout=10)
    return 0


if __name__ == "__main__":
    sys.exit(main())
