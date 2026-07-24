#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: ./build.sh [--clean]

Compile simulatorEditor for Linux Development.

Options:
  --clean  Clean the target before building.
  --help   Show this help.
EOF
}
CLEAN=false
while (($# > 0)); do
    case "$1" in
        --clean) CLEAN=true ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            printf 'Error: unknown option: %s\n' "$1" >&2
            usage >&2
            exit 2
            ;;
    esac
    shift
done
PROJECT_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
PROJECT_FILE="$PROJECT_ROOT/LunarSimPG.uproject"
[[ -n "${UE_ROOT:-}" ]] || {
    printf 'Error: UE_ROOT is not set. Run ./setup.sh first.\n' >&2
    exit 1
}
BUILD_TOOL="$UE_ROOT/Engine/Build/BatchFiles/Linux/Build.sh"
[[ -f "$PROJECT_FILE" && -x "$BUILD_TOOL" ]] || {
    printf 'Error: project or Unreal build tool is missing. Run ./setup.sh first.\n' >&2
    exit 1
}
BUILD_COMMAND=(
    "$BUILD_TOOL"
    simulatorEditor
    Linux
    Development
    "$PROJECT_FILE"
)

if [[ "$CLEAN" == true ]]; then
    printf 'Clean command:'
    printf ' %q' "${BUILD_COMMAND[@]}" -clean
    printf '\n'
    "${BUILD_COMMAND[@]}" -clean
fi

printf 'Build command:'
printf ' %q' "${BUILD_COMMAND[@]}"
printf '\n'
"${BUILD_COMMAND[@]}"
