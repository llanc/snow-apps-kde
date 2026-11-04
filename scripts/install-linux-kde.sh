#!/usr/bin/env bash
set -euo pipefail
repo_root=$(cd "$(dirname "$0")/.." && pwd)
exec mise x python@3.14 -- python "$repo_root/scripts/install-linux-kde.py" "$@"
