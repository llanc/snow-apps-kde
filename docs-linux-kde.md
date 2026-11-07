# Linux and KDE port status

This fork has experimental Snow Shot and Snow Image Viewer builds for CachyOS
x86-64 with KDE Plasma on Wayland. Both applications build on the development host.
Screenshot acquisition, selection and rendering have been exercised with a
synthetic desktop portal in both Qt offscreen mode and an isolated virtual KWin
Wayland session; shortcut registration and release were also tested against that
KWin session. The viewer's image loading and edit worker have offscreen tests.
The real desktop portal's permission flow has not yet been tested. A per-user
menu installer is available; there is no Linux system package yet.

The in-progress screenshot path uses the XDG desktop screenshot portal and
requires a single display. Portal image pixels are mapped to the display's Qt
logical coordinates, including when Qt rounds a fractional Wayland scale. The
portal may ask the user for permission. KDE global shortcuts use
the `org.kde.kglobalaccel` session service and do not steal conflicting keys.
Multi-display capture, recapture with window exclusions, smart window/element
selection, screen recording, global mouse actions, and Linux packaging still need
platform implementation. Cursor capture is unavailable. The built-in updater is
disabled on Linux so it cannot install a Windows release. The Rust capture API
remains unsupported on Linux for its other consumers, including the separate
direct-capture path. Some viewer actions, including Windows shell integration,
remain unavailable. The mini edition is not part of this build.

Snow Shot's startup setting manages a user-level XDG autostart entry that starts
the existing build in the background. It preserves entries disabled in KDE and
does not replace files it does not manage.

## Build and run

The development host uses Qt 6.12.0 development files, Clang, CMake 4.2 or
newer, Ninja, Git, pkg-config and the system FFmpeg development libraries. First
builds of the vcpkg dependencies also need the usual native build utilities,
including zip and nasm. On this host the missing CMake, Ninja, zip and nasm
executables were provisioned under the ignored `build/host-tools/root/usr`
directory; the build script picks them up automatically when present. A fresh
checkout needs those tools on `PATH` or provisioned in that directory.

Install the repository's Rust minimum, the OCR submodule's pinned nightly Rust,
and Python with `mise`. The versions below match the current pins; if upstream
changes them, use `scripts/build-toolchain.json` and
`snow-crates/crates/simd-paddle-ocr-rs/rust-toolchain.toml` as the source of
truth.

```sh
mise install rust@1.97.1 rust@nightly-2026-09-08 python@3.14
scripts/build-linux-kde.sh
scripts/install-linux-kde.sh
scripts/run-snow-shot-linux.sh --show-main-window
scripts/run-snow-image-viewer-linux.sh /path/to/image.png
```

The first build initializes submodules, bootstraps the pinned repository-local
vcpkg and installs the Snow Shot and Image Viewer dependencies; this can take a long
time. Later builds may pass `--skip-dependencies` when those dependencies are
already current, and `--jobs N` controls parallelism. The script uses a Debug
build and Clang, disables unrelated targets, and leaves generated files under
ignored directories. Set `SNOW_FFMPEG_ROOT` if the FFmpeg development files are
not under `/usr`. The workspace's existing Debug API configuration applies unless
`SNOW_APPS_API_BASE_URL` is explicitly supplied in the environment.

## Focused Linux checks

These tests use a private D-Bus session and simulated portal/shortcut services;
they do not register shortcuts with or capture the real desktop. They require CMake,
Ninja, a C++20 compiler, Qt 6.12.0 Core/Gui/Widgets/DBus development files,
and `dbus-run-session`:

```sh
# On this host only, add the repository-local host tools when needed.
if [ -x build/host-tools/root/usr/bin/cmake ]; then
  export PATH="$PWD/build/host-tools/root/usr/bin:$PATH"
  export LD_LIBRARY_PATH="$PWD/build/host-tools/root/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi
cmake -S test-support/linux-portal -B build/linux-portal-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/linux-portal-tests
ctest --test-dir build/linux-portal-tests -R '^snow-shot-linux-(portal|shortcut|update)-tests$' --output-on-failure
mise x python@3.14 -- python scripts/test-linux-build-support.py
mise x python@3.14 -- python scripts/test-sync-upstream.py
mise x python@3.14 -- python scripts/test-linux-kde-install.py
mise x python@3.14 -- python scripts/test-linux-startup.py build/linux-snow-shot-clang/snow_shot/snow_shot
mise x python@3.14 -- python scripts/test-linux-screenshot-integration.py \
  build/linux-snow-shot-clang/snow_shot/snow_shot \
  build/linux-snow-shot-clang/snow_shot/snow-shot-mcp \
  build/linux-portal-tests/snow-shot-linux-fake-portal \
  --wayland --kde-shortcut-probe build/linux-portal-tests/snow-shot-linux-real-shortcut-tests
```

The final integration command requires `kwin_wayland` and `qdbus6`. Add
`--fractional-toolbar` to check toolbar sizing at 125% scale with `kscreen-doctor`.
The test starts a virtual compositor and a private D-Bus session, uses isolated
configuration directories, and never captures the real desktop. When configuring this host in a non-ASCII
directory, `-DCMAKE_CXX_SCAN_FOR_MODULES=OFF` avoids a compiler module-scanner
failure. The build script applies that option.

To build and run just the Linux viewer's smoke, memory-limit and editor-pipeline
checks after a normal build:

```sh
export VCPKG_ROOT="$PWD/.tools/vcpkg" VCPKG_FORCE_SYSTEM_BINARIES=1
mise x rust@1.97.1 python@3.14 -- cmake -S . -B build/linux-snow-shot-clang -DSNOW_APPS_BUILD_TESTS=ON
mise x rust@1.97.1 python@3.14 -- cmake --build build/linux-snow-shot-clang --target \
  snow_image_viewer_linux_smoke_tests snow_image_viewer_linux_worker_memory_tests \
  snow_image_viewer_pipeline_notification_lifetime_tests
ctest --test-dir build/linux-snow-shot-clang \
  -R '^snow_image_viewer_(linux_smoke|linux_worker_memory|pipeline_notification_lifetime)_tests$' --output-on-failure
```

The first command requires the repository-local tools and vcpkg environment
described above. Re-running `scripts/build-linux-kde.sh` restores the normal
tests-disabled configuration. The KWin integration command also accepts
`--viewer-probe build/linux-snow-shot-clang/snow_image_viewer/tests/snow_image_viewer_linux_smoke_tests`
after the viewer test target has been built.

## Synchronizing the fork

The `Sync upstream` GitHub Actions workflow is intended for
`llanc/snow-apps-kde`. Once committed and pushed to that repository's default
branch, with Actions enabled for the fork, it checks `mg-chao/snow-apps` `main`
every six hours and can also be run manually. It previews a merge, runs the
focused sync and Linux configuration tests, and then asks GitHub's
[fork synchronization API](https://docs.github.com/en/rest/branches/branches#sync-a-fork-branch-with-the-upstream-repository)
to merge upstream into the fork. This API requires the workflow's `contents:
write` permission and does not need a personal access token. The workflow never
force-pushes. A merge conflict, a failed test, a concurrent conflicting
update, insufficient workflow permissions, or branch protection stops the run and
requires attention in GitHub Actions. GitHub may delay scheduled runs or disable
them after repository inactivity, so the schedule is not a real-time guarantee.

To perform the same merge locally, configure the upstream remote once and run:

```sh
git remote add upstream https://github.com/mg-chao/snow-apps.git
scripts/sync-upstream.sh
```

The script only operates on a clean `main` that exactly matches `origin/main`.
By default it leaves a successful merge in the local checkout for review; pass
`--push` to publish it. The GitHub workflow updates the fork on GitHub, not an
open local checkout. Before updating a checkout, save its work and inspect the
incoming commits. Git submodule commits are tracked by the main repository;
after updating, run `git submodule update --init --recursive` to check out the
new recorded OCR revision.
