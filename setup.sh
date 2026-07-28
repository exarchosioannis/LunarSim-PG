#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
cd "$PROJECT_ROOT"

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

for command in git git-lfs curl jq python3; do
    require_command "$command"
done

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
        printf 'MoonSim terrain-tool Python dependencies are available.\n'
        return
    fi

    printf 'Installing MoonSim terrain-tool Python dependencies...\n'

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

[[ -n "${UE_ROOT:-}" ]] ||
    fail "UE_ROOT is not set. Export it to your Unreal Engine 5.7.x directory."

BUILD_TOOL="$UE_ROOT/Engine/Build/BatchFiles/Linux/Build.sh"
EDITOR="$UE_ROOT/Engine/Binaries/Linux/UnrealEditor"
BUILD_VERSION="$UE_ROOT/Engine/Build/Build.version"

[[ -x "$BUILD_TOOL" ]] || fail "Unreal build tool not found: $BUILD_TOOL"
[[ -x "$EDITOR" ]] || fail "Unreal Editor not found: $EDITOR"
require_file "$BUILD_VERSION"

ENGINE_MAJOR="$(jq -r '.MajorVersion // empty' "$BUILD_VERSION")"
ENGINE_MINOR="$(jq -r '.MinorVersion // empty' "$BUILD_VERSION")"
[[ "$ENGINE_MAJOR.$ENGINE_MINOR" == "5.7" ]] ||
    fail "UE_ROOT must point to Unreal Engine 5.7.x."

git lfs pull

if [[ -f .gitmodules ]]; then
    git submodule update --init --recursive
fi

for file in \
    LunarSimPG.uproject \
    Source/simulatorEditor.Target.cs \
    Plugins/TempoROS/Setup.sh \
    Plugins/TempoROS/TempoROS.uplugin \
    Plugins/unrealgt/UnrealGT.uplugin; do
    require_file "$file"
done

UNREAL_ENGINE_PATH="$UE_ROOT" Plugins/TempoROS/Setup.sh

for directory in \
    Plugins/TempoROS/Source/ThirdParty/rclcpp/Includes \
    Plugins/TempoROS/Source/ThirdParty/rclcpp/Libraries/Linux \
    Plugins/TempoROS/Source/ThirdParty/rclcpp/Binaries/Linux; do
    [[ -d "$directory" ]] ||
        fail "TempoROS setup did not create required directory: $directory"
done

printf 'Setup complete.\n'
printf 'Next: ./build.sh\n'