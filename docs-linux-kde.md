# Linux and KDE port status

This fork has an experimental Snow Shot build for CachyOS x86-64 with KDE Plasma
on Wayland. The full application builds and starts on the development host.
Screenshot acquisition, selection and rendering have been exercised with a
synthetic desktop portal in both Qt offscreen mode and an isolated virtual KWin
Wayland session; shortcut registration and release were also tested against that
KWin session. The real desktop portal's permission flow has not yet been tested.
There is no Linux installer yet.

The in-progress screenshot path uses the XDG desktop screenshot portal and
requires a single display whose portal image dimensions match Qt's display
geometry. The portal may ask the user for permission. KDE global shortcuts use
the `org.kde.kglobalaccel` session service and do not steal conflicting keys.
Multi-display capture, recapture with window exclusions, smart window/element
selection, screen recording, global mouse actions, and Linux packaging still need
platform implementation. Cursor capture is unavailable. The built-in updater is
disabled on Linux so it cannot install a Windows release. The Rust capture API
remains unsupported on Linux for its other consumers, including the separate
direct-capture path. Image Viewer and the mini edition are not part of this build.

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
scripts/run-snow-shot-linux.sh --show-main-window
```

The first build initializes submodules, bootstraps the pinned repository-local
vcpkg and installs its Snow Shot feature dependencies; this can take a long
time. Later builds may pass `--skip-dependencies` when those dependencies are
already current, and `--jobs N` controls parallelism. The script uses a Debug
build and Clang, disables unrelated targets, and leaves generated files under
ignored directories. Set `SNOW_FFMPEG_ROOT` if the FFmpeg development files are
not under `/usr`. The workspace's existing Debug API configuration applies unless
`SNOW_APPS_API_BASE_URL` is explicitly supplied in the environment.

## Focused Linux checks

These tests use a private D-Bus session and a fake portal/shortcut service; they
do not register shortcuts or capture the real desktop. They require CMake,
Ninja, a C++20 compiler, Qt 6.12.0 Core/Gui/Widgets/DBus development files,
and `dbus-run-session`:

```sh
cmake -S test-support/linux-portal -B build/linux-portal-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/linux-portal-tests
ctest --test-dir build/linux-portal-tests -R '^snow-shot-linux-(portal|shortcut|update)-tests$' --output-on-failure
python3 scripts/test-linux-build-support.py
python3 scripts/test-sync-upstream.py
python3 scripts/test-linux-startup.py build/linux-snow-shot-clang/snow_shot/snow_shot
python3 scripts/test-linux-screenshot-integration.py \
  build/linux-snow-shot-clang/snow_shot/snow_shot \
  build/linux-snow-shot-clang/snow_shot/snow-shot-mcp \
  build/linux-portal-tests/snow-shot-linux-fake-portal \
  --wayland --kde-shortcut-probe build/linux-portal-tests/snow-shot-linux-real-shortcut-tests
```

Run Python through the configured `mise` environment on the CachyOS development
host. The final integration command requires `kwin_wayland`; it starts a virtual
compositor and a private D-Bus session, uses isolated configuration directories,
and never captures the real desktop. When configuring this host in a non-ASCII
directory, `-DCMAKE_CXX_SCAN_FOR_MODULES=OFF` avoids a compiler module-scanner
failure. The build script applies that option.

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
