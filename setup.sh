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

for command in git git-lfs curl jq; do
    require_command "$command"
done

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
