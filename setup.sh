#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
cd "$PROJECT_ROOT"

usage() {
    cat <<'EOF'
Usage: ./setup.sh [--ue-root PATH]

Prepare LunarSim-PG and save its Unreal Engine 5.7.x path.

Options:
  --ue-root PATH  Unreal Engine 5.7.x root to validate and save.
  --help           Show this help.

UE_ROOT remains supported and overrides a saved .ue_root path when --ue-root
is not supplied.
EOF
}

fail() {
    printf 'Error: %s\n' "$*" >&2
    exit 1
}

require_command() {
    command -v "$1" >/dev/null 2>&1 ||
        fail "required command not found: $1"
}

require_file() {
    [[ -f "$1" ]] || fail "required file not found: $1"
}

SETUP_UE_ROOT=""
SETUP_UE_ROOT_SUPPLIED=false
while (($# > 0)); do
    case "$1" in
        --ue-root)
            (($# >= 2)) || fail "--ue-root requires a path."
            SETUP_UE_ROOT="$2"
            SETUP_UE_ROOT_SUPPLIED=true
            shift 2
            ;;
        --ue-root=*)
            SETUP_UE_ROOT="${1#*=}"
            SETUP_UE_ROOT_SUPPLIED=true
            shift
            ;;
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
done

for command in git git-lfs curl jq python3; do
    require_command "$command"
done

if [[ "$SETUP_UE_ROOT_SUPPLIED" == true ]]; then
    UE_ROOT="$SETUP_UE_ROOT"
elif [[ -z "${UE_ROOT:-}" && -f "$PROJECT_ROOT/.ue_root" ]]; then
    UE_ROOT="$(< "$PROJECT_ROOT/.ue_root")"
fi

[[ -n "${UE_ROOT:-}" ]] ||
    fail "Unreal Engine path is not configured. Use --ue-root or set UE_ROOT."
[[ -d "$UE_ROOT" ]] ||
    fail "Unreal Engine root is not a directory: $UE_ROOT"

UE_ROOT="$(cd -- "$UE_ROOT" && pwd -P)"
BUILD_TOOL="$UE_ROOT/Engine/Build/BatchFiles/Linux/Build.sh"
EDITOR="$UE_ROOT/Engine/Binaries/Linux/UnrealEditor"
BUILD_VERSION="$UE_ROOT/Engine/Build/Build.version"

[[ -x "$BUILD_TOOL" ]] || fail "Unreal build tool not found: $BUILD_TOOL"
[[ -x "$EDITOR" ]] || fail "Unreal Editor not found: $EDITOR"
require_file "$BUILD_VERSION"

ENGINE_MAJOR="$(jq -r '.MajorVersion // empty' "$BUILD_VERSION")"
ENGINE_MINOR="$(jq -r '.MinorVersion // empty' "$BUILD_VERSION")"
[[ "$ENGINE_MAJOR.$ENGINE_MINOR" == "5.7" ]] ||
    fail "Unreal Engine root must point to version 5.7.x."

printf '%s\n' "$UE_ROOT" > "$PROJECT_ROOT/.ue_root"
export UE_ROOT

PYTHON_IMPORT_TEST='import tkinter, numpy, PIL, matplotlib, scipy'
PYTHON_APT_PACKAGES=(
    python3-tk
    python3-numpy
    python3-pil
    python3-matplotlib
    python3-scipy
)

check_python_dependencies() {
    python3 -c "$PYTHON_IMPORT_TEST" >/dev/null 2>&1
}

install_python_dependencies() {
    if check_python_dependencies; then
        printf 'LunarSim-PG terrain-tool Python dependencies are available.\n'
        return
    fi

    printf 'Installing LunarSim-PG terrain-tool Python dependencies...\n'

    [[ -r /etc/os-release ]] ||
        fail "cannot identify the operating system; install: ${PYTHON_APT_PACKAGES[*]}"

    # shellcheck disable=SC1091
    . /etc/os-release
    [[ "${ID:-}" == "ubuntu" ]] ||
        fail "automatic Python dependency installation is supported on Ubuntu only. Install: ${PYTHON_APT_PACKAGES[*]}"

    local -a privilege=()
    if [[ "$EUID" -ne 0 ]]; then
        require_command sudo
        privilege=(sudo)
    fi

    "${privilege[@]}" apt-get update
    "${privilege[@]}" apt-get install -y "${PYTHON_APT_PACKAGES[@]}"

    check_python_dependencies ||
        fail "Python dependencies were installed, but the active python3 still cannot import tkinter, numpy, PIL, matplotlib, and scipy"
}

install_python_dependencies

git lfs pull

if [[ -f .gitmodules ]]; then
    git submodule update --init --recursive
fi

for file in \
    LunarSimPG.uproject \
    Source/simulatorEditor.Target.cs \
    Plugins/TempoROS/Scripts/SyncDeps.sh \
    Plugins/TempoROS/TempoROS.uplugin \
    Plugins/unrealgt/UnrealGT.uplugin; do
    require_file "$file"
done

UNREAL_ENGINE_PATH="$UE_ROOT" Plugins/TempoROS/Scripts/SyncDeps.sh

for directory in \
    Plugins/TempoROS/Source/ThirdParty/rclcpp/Includes \
    Plugins/TempoROS/Source/ThirdParty/rclcpp/Libraries/Linux \
    Plugins/TempoROS/Source/ThirdParty/rclcpp/Binaries/Linux; do
    [[ -d "$directory" ]] ||
        fail "TempoROS setup did not create required directory: $directory"
done

printf 'Setup complete.\n'
printf 'Unreal Engine path saved to %s\n' "$PROJECT_ROOT/.ue_root"
printf 'Next: ./build.sh\n'
