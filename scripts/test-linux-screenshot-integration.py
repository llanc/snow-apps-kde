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


def kwin_script_result(root, compositor_log, source, marker):
    script = root / "check-screenshot-window.js"
    script.write_text(source, encoding="utf-8")
    dbus = ["qdbus6", "org.kde.KWin"]
    with Path(compositor_log.name).open(encoding="utf-8") as log:
        log.seek(0, os.SEEK_END)
        script_id = subprocess.check_output([
            *dbus, "/Scripting", "org.kde.kwin.Scripting.loadScript", str(script),
        ], text=True, timeout=5).strip()
        try:
            subprocess.run([*dbus, f"/Scripting/Script{script_id}", "org.kde.kwin.Script.run"],
                           check=True, timeout=5)
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                for line in log.readlines():
                    if marker in line:
                        return line.split(marker, 1)[1].strip()
                time.sleep(0.05)
            raise RuntimeError(f"KWin did not report: {marker}")
        finally:
            subprocess.run([*dbus, "/Scripting", "org.kde.kwin.Scripting.unloadScript", str(script)],
                           check=True, stdout=subprocess.DEVNULL, timeout=5)


def require_kwin_screenshot_window(root, pid, compositor_log, *, toolbar=False):
    if toolbar:
        expected = "toolbar at its logical size"
        condition = ("!window.fullScreen && window.output.devicePixelRatio === 1.25 && "
                     "Math.abs(frame.width - 1242) < 1 && Math.abs(frame.height - 142) < 1")
    else:
        expected = "overlay covering the full output"
        condition = ("window.fullScreen && frame.x === screen.x && frame.y === screen.y && "
                     "frame.width === screen.width && frame.height === screen.height")
    marker = f"Snow Shot verified: {expected}"
    kwin_script_result(root, compositor_log, f"""
function check(window) {{
    if (window.pid !== {pid} || window.popupWindow || window.deleted) {{
        return;
    }}
    const frame = window.frameGeometry;
    const screen = workspace.clientArea(KWin.FullScreenArea, window);
    print("Snow Shot window: " + window.fullScreen + ", frame " + frame + ", screen " + screen);
    if ({condition}) {{
        print("{marker}");
    }}
}}
workspace.windowAdded.connect(check);
workspace.windowList().forEach(check);
""", marker)


def exercise_window_selection(root, env, compositor_log, app_pid, start_capture, mode):
    windows = []
    try:
        for index, repeats in enumerate((26, 2)):
            windows.append(subprocess.Popen(
                ["kdialog", "--title", f"Snow Shot fixture {index}",
                 "--msgbox", "Synthetic test. " * repeats],
                cwd=root, env=env, stdout=compositor_log, stderr=compositor_log))
        marker = "Snow Shot fixture window geometry "
        pids = [process.pid for process in windows]
        screen, back, front, cursor = json.loads(kwin_script_result(root, compositor_log, f"""
function arrange() {{
    const windows = {pids}.map(pid => workspace.windowList().find(w => w.pid === pid && !w.deleted));
    if (windows.some(w => !w)) return;
    const screen = workspace.screens[0].geometry;
    const cursor = workspace.cursorPos;
    for (const window of windows) {{
        const frame = window.frameGeometry;
        frame.x = Math.max(screen.x, Math.min(cursor.x - frame.width / 2,
                                             screen.x + screen.width - frame.width));
        frame.y = Math.max(screen.y, Math.min(cursor.y - frame.height / 2,
                                             screen.y + screen.height - frame.height));
        window.frameGeometry = frame;
        workspace.raiseWindow(window);
    }}
    print("{marker}" + JSON.stringify([screen, ...windows.map(w => w.frameGeometry), cursor]));
}}
workspace.windowAdded.connect(arrange);
arrange();
""", marker))
        for window in (back, front):
            if not (window["x"] < cursor["x"] < window["x"] + window["width"] and
                    window["y"] < cursor["y"] < window["y"] + window["height"]):
                raise RuntimeError("The overlapping fixture windows do not cover the cursor")

        if mode == "desktop":
            # KWin resets Show Desktop as soon as its D-Bus caller disconnects.
            marker = "Snow Shot fixture requested Show Desktop"
            kwin_script_result(root, compositor_log, f"""
callDBus("org.kde.KWin", "/KWin", "org.kde.KWin", "showDesktop", true);
print("{marker}");
""", marker)
            dbus = ["qdbus6", "org.kde.KWin", "/KWin"]
            showing = subprocess.check_output([
                *dbus, "org.freedesktop.DBus.Properties.Get", "org.kde.KWin", "showingDesktop",
            ], text=True, timeout=5)
            if showing.strip() != "true":
                raise RuntimeError("The isolated compositor did not show the desktop")

        start_capture()
        require_kwin_screenshot_window(root, app_pid, compositor_log)
        preview = root / "window-selection.png"
        subprocess.run(["spectacle", "-b", "-n", "-f", "-o", str(preview)], cwd=root, env=env,
                       stdout=compositor_log, stderr=compositor_log, check=True, timeout=15)
        width, height = map(int, subprocess.check_output(
            ["magick", "identify", "-format", "%w %h", str(preview)], text=True, timeout=5).split())

        # The fake portal image is blue. Test both edges of the bright, selected
        # region so a whole screen or the larger window behind it cannot pass.
        y = round((front["y"] + front["height"] / 4 - screen["y"]) * height / screen["height"])
        left = round((front["x"] - screen["x"]) * width / screen["width"])
        right = left + round(front["width"] * width / screen["width"])
        for x, selected in ((left - 8, False), (left + 8, True),
                            (right - 8, True), (right + 8, False)):
            pixel = subprocess.check_output(
                ["magick", str(preview), "-crop", f"1x1+{x}+{y}", "+repage", "-depth", "8",
                 "rgb:-"], timeout=5)
            expected = selected and mode == "front"
            if len(pixel) != 3 or (pixel[2] > 150) != expected:
                raise RuntimeError(f"Unexpected smart selection at {x}, {y} in {mode} mode")
        if mode == "front":
            print("KDE smart selection highlights the top window at fractional scale")
        else:
            print("KDE smart selection skips windows hidden by Show Desktop")
    finally:
        for window in windows:
            if window.poll() is None:
                window.terminate()
            window.wait(timeout=5)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("application", type=Path)
    parser.add_argument("bridge", type=Path)
    parser.add_argument("fake_portal", type=Path)
    parser.add_argument("--wayland", action="store_true",
                        help="Run against an isolated virtual KWin compositor")
    parser.add_argument("--portal-size", nargs=2, type=int, metavar=("WIDTH", "HEIGHT"),
                        help="Set the synthetic portal image size")
    parser.add_argument("--qt-scale-factor", type=float,
                        help="Set the Qt screen scale for the application under test")
    parser.add_argument("--fractional-toolbar", action="store_true",
                        help="Check toolbar geometry on a virtual output at 125%% scale")
    parser.add_argument("--window-selection", nargs="?", const="front", choices=("front", "desktop"),
                        help="Verify smart selection over overlapping or desktop-hidden KDE windows")
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
    if args.portal_size and any(value <= 0 for value in args.portal_size):
        parser.error("--portal-size must be positive")
    if args.qt_scale_factor is not None and args.qt_scale_factor <= 0:
        parser.error("--qt-scale-factor must be positive")
    if args.fractional_toolbar and (not args.wayland or args.qt_scale_factor is not None):
        parser.error("--fractional-toolbar requires --wayland without --qt-scale-factor")
    if args.window_selection and (not args.fractional_toolbar or not tray_watcher):
        parser.error("--window-selection requires --fractional-toolbar and --tray-watcher")
    if os.environ.get("SNOW_LINUX_PRIVATE_TEST_BUS") != "1":
        with tempfile.TemporaryDirectory(prefix="snow-shot-linux-bus-") as temporary:
            env = isolated_environment(Path(temporary))
            env["SNOW_LINUX_PRIVATE_TEST_BUS"] = "1"
            command = ["dbus-run-session", "--", sys.executable, __file__, str(application),
                       str(bridge), str(fake_portal)]
            if args.wayland:
                command.append("--wayland")
            if args.portal_size:
                command.extend(["--portal-size", *map(str, args.portal_size)])
            if args.qt_scale_factor is not None:
                command.extend(["--qt-scale-factor", str(args.qt_scale_factor)])
            if args.fractional_toolbar:
                command.append("--fractional-toolbar")
            if args.window_selection:
                command.extend(["--window-selection", args.window_selection])
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
                    compositor_env = dict(env, XDG_CURRENT_DESKTOP="KDE", LIBGL_ALWAYS_SOFTWARE="1",
                                          QT_FORCE_STDERR_LOGGING="1")
                    compositor_command = [
                        "kwin_wayland", "--virtual", "--no-lockscreen", "--no-kactivities",
                        "--socket", "snow-test",
                        "--width", "2560" if args.fractional_toolbar else "800",
                        "--height", "1920" if args.fractional_toolbar else "600",
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
                portal_size = args.portal_size or (800, 600 if args.wayland else 800)
                portal = subprocess.Popen(
                    [str(fake_portal), str(root / "synthetic.png"), *map(str, portal_size)], cwd=root, env=env,
                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                    start_new_session=True,
                )
                readable, _, _ = select.select([portal.stdout], [], [], 15)
                if not readable or portal.stdout.readline().strip() != "ready":
                    raise RuntimeError("The synthetic screenshot portal did not start")
                if args.fractional_toolbar:
                    subprocess.run(["kscreen-doctor", "output.1.scale.1.25"], cwd=root, env=env,
                                   check=True, timeout=10)
                if args.qt_scale_factor is not None:
                    env["QT_SCALE_FACTOR"] = str(args.qt_scale_factor)
                if picker_probe:
                    subprocess.run([str(picker_probe), "--embedded-picker-only"], cwd=root,
                                   env=env, check=True, timeout=20)
                    print("Wayland screenshot color picker follows overlay coordinates")
                if picker_host_probe:
                    subprocess.run([str(picker_host_probe), "--overlay-mouse-picker-only"], cwd=root,
                                   env=env, check=True, timeout=20)
                    print("Wayland screenshot mouse events update the picker")
                    subprocess.run([str(picker_host_probe), "--linux-portal-geometry-only"], cwd=root,
                                   env=env, check=True, timeout=20)
                    print("Portal image pixels map to logical screen coordinates")
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
                    group = re.search(r"<\((\d+), \{[^}]*'label': <'Window group: [^']+'>", layout)
                    if not group:
                        raise RuntimeError(f"The tray group submenu is missing: {layout}")
                    dbus_call(menu_path, "com.canonical.dbusmenu.AboutToShow", group.group(1))
                    submenu = dbus_call(menu_path, "com.canonical.dbusmenu.GetLayout", group.group(1),
                                        "1", "['label', 'visible', 'enabled']")
                    if "New group" not in submenu:
                        raise RuntimeError(f"The tray group submenu did not populate: {submenu}")
                # The second visible capture reuses a pooled overlay after native surface release.
                presentations = ("silent", "visible", "visible") if args.wayland else ("silent", "visible")
                for presentation in presentations:
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
                    if (width, height) != tuple(portal_size):
                        raise RuntimeError(f"Screenshot canvas does not match portal pixels: {state}")
                    if args.wayland and presentation == "visible":
                        require_kwin_screenshot_window(root, application_process.pid, compositor_log)
                    selection_y = y if presentation == "visible" else y + 10
                    selected = client.tool("snow_shot_screenshot_set_selection", {
                        "session_id": session_id, "expected_revision": begun["revision"],
                        "type": "rectangle", "bounds": [x + 10, selection_y, 90, 90],
                    })["result"]
                    if args.fractional_toolbar and presentation == "visible":
                        require_kwin_screenshot_window(root, application_process.pid, compositor_log,
                                                       toolbar=True)
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
                    if args.window_selection:
                        screenshot_action = re.search(
                            r"<\((\d+), \{[^}]*'label': <'Screenshot(?:\\t[^']+)?'>", layout)
                        if not screenshot_action:
                            raise RuntimeError("The exported tray Screenshot action was not found")
                        exercise_window_selection(root, env, compositor_log, application_process.pid,
                                                  lambda: dbus_call(
                                                      menu_path, "com.canonical.dbusmenu.Event",
                                                      screenshot_action.group(1), "clicked",
                                                      "<0>", "0"), args.window_selection)
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
