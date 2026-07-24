#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: ./open_project.sh [Unreal Editor arguments...]

Open LunarSimPG.uproject and forward additional arguments to Unreal Editor.
EOF
}

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
    usage
    exit 0
fi

PROJECT_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
PROJECT_FILE="$PROJECT_ROOT/LunarSimPG.uproject"

[[ -n "${UE_ROOT:-}" ]] || {
    printf 'Error: UE_ROOT is not set. Run ./setup.sh first.\n' >&2
    exit 1
}

EDITOR="$UE_ROOT/Engine/Binaries/Linux/UnrealEditor"
[[ -f "$PROJECT_FILE" && -x "$EDITOR" ]] || {
    printf 'Error: project or Unreal Editor is missing. Run ./setup.sh first.\n' >&2
    exit 1
}

exec "$EDITOR" "$PROJECT_FILE" "$@"
