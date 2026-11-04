#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
build_dir="$repo_root/build/linux-snow-shot-clang"
vcpkg_root="$repo_root/.tools/vcpkg"
installed_dir="$vcpkg_root/installed/linux"
jobs=4
skip_dependencies=false
while (( $# )); do
    case "$1" in
        --jobs)
            [[ $# -ge 2 && $2 =~ ^[1-9][0-9]*$ ]] || {
                printf '%s\n' '--jobs requires a positive integer' >&2; exit 2;
            }
            jobs=$2
            shift 2
            ;;
        --skip-dependencies) skip_dependencies=true; shift ;;
        --help)
            printf '%s\n' 'Usage: scripts/build-linux-kde.sh [--jobs N] [--skip-dependencies]'
            exit 0
            ;;
        *) printf 'Unknown option: %s\n' "$1" >&2; exit 2 ;;
    esac
done

[[ $(uname -s) == Linux ]] || { printf '%s\n' 'This build requires Linux.' >&2; exit 1; }
case $(uname -m) in
    x86_64|amd64) triplet=x64-linux ;;
    aarch64|arm64) triplet=arm64-linux ;;
    *) printf '%s\n' 'Unsupported Linux architecture.' >&2; exit 1 ;;
esac

# The current development machine keeps missing host tools inside build/.
host_tools="$repo_root/build/host-tools/root/usr"
if [[ -x "$host_tools/bin/cmake" ]]; then
    export PATH="$host_tools/bin:$PATH"
    export LD_LIBRARY_PATH="$host_tools/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi
for tool in git mise cmake ninja clang clang++ pkg-config; do
    command -v "$tool" >/dev/null || {
        printf 'Missing required tool: %s. See docs-linux-kde.md.\n' "$tool" >&2; exit 1;
    }
done
cd "$repo_root"
git submodule update --init --recursive
rust_version=$(mise x python@3.14 -- python -c \
    'import json; print(json.load(open("scripts/build-toolchain.json"))["rustMinimum"])')
vcpkg_baseline=$(mise x python@3.14 -- python -c \
    'import json; print(json.load(open("vcpkg-configuration.json"))["default-registry"]["baseline"])')
[[ $vcpkg_baseline =~ ^[0-9a-f]{40}$ ]] || {
    printf '%s\n' 'Invalid vcpkg registry baseline.' >&2; exit 1;
}

if ! "$skip_dependencies"; then
    if [[ ! -d "$vcpkg_root/.git" ]]; then
        [[ ! -e "$vcpkg_root" ]] || {
            printf 'Expected a Git checkout at %s\n' "$vcpkg_root" >&2; exit 1;
        }
        git clone https://github.com/microsoft/vcpkg.git "$vcpkg_root"
    fi
    if [[ $(git -C "$vcpkg_root" rev-parse HEAD) != "$vcpkg_baseline" ]]; then
        [[ -z $(git -C "$vcpkg_root" status --porcelain --untracked-files=normal) ]] || {
            printf '%s\n' 'The repository-local vcpkg checkout has uncommitted changes.' >&2; exit 1;
        }
        git -C "$vcpkg_root" fetch origin "$vcpkg_baseline" --depth=1
        git -C "$vcpkg_root" checkout --detach "$vcpkg_baseline"
    fi
    if [[ ! -x "$vcpkg_root/vcpkg" ]]; then
        "$vcpkg_root/bootstrap-vcpkg.sh" -disableMetrics
    fi
    VCPKG_FORCE_SYSTEM_BINARIES=1 "$vcpkg_root/vcpkg" install \
        "--x-manifest-root=$repo_root" "--x-install-root=$installed_dir" \
        "--triplet=$triplet" --x-feature=snow-shot --x-feature=image-viewer \
        "--overlay-ports=$repo_root/cmake/vcpkg-overlay-ports" \
        "--overlay-triplets=$repo_root/cmake/vcpkg-overlay-triplets" --clean-after-build
fi
[[ -f "$installed_dir/vcpkg/status" ]] || {
    printf '%s\n' 'Linux vcpkg dependencies are missing. Re-run without --skip-dependencies.' >&2
    exit 1
}

export VCPKG_ROOT="$vcpkg_root" VCPKG_FORCE_SYSTEM_BINARIES=1
configure=(
    -S "$repo_root" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Debug
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_SCAN_FOR_MODULES=OFF
    "-DCMAKE_TOOLCHAIN_FILE=$vcpkg_root/scripts/buildsystems/vcpkg.cmake"
    -DVCPKG_MANIFEST_INSTALL=OFF "-DVCPKG_INSTALLED_DIR=$installed_dir"
    "-DVCPKG_TARGET_TRIPLET=$triplet" "-DSNOW_FFMPEG_ROOT=${SNOW_FFMPEG_ROOT:-/usr}"
    -DSNOW_APPS_BUILD_SNOW_SHOT=ON -DSNOW_APPS_BUILD_SNOW_SHOT_MINI=OFF
    -DSNOW_APPS_BUILD_IMAGE_VIEWER=ON -DSNOW_APPS_BUILD_TESTS=OFF
    -DSNOW_APPS_BUILD_BENCHMARKS=OFF -DSNOW_IMAGE_PROFILE=redistributable
)
if [[ -n ${SNOW_APPS_API_BASE_URL:-} ]]; then
    configure+=("-DSNOW_APPS_API_BASE_URL=$SNOW_APPS_API_BASE_URL")
fi
mise x "rust@$rust_version" python@3.14 -- cmake "${configure[@]}"
mise x "rust@$rust_version" python@3.14 -- cmake --build "$build_dir" \
    --target snow_shot snow_image_viewer -j "$jobs"
