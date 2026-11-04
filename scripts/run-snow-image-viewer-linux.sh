#!/usr/bin/env bash
set -euo pipefail
repo_root=$(cd "$(dirname "$0")/.." && pwd)
executable="$repo_root/build/linux-snow-shot-clang/snow_image_viewer/snow_image_viewer"
[[ -x "$executable" ]] || {
    printf '%s\n' 'Build Snow Image Viewer first with scripts/build-linux-kde.sh.' >&2
    exit 1
}
exec "$executable" "$@"
