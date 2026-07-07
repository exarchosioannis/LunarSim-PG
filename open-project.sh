#!/usr/bin/env bash
set -e

UE_BIN="$HOME/UnrealEngine_5.7.4/Engine/Binaries/Linux/UnrealEditor"
PROJECT="$HOME/Projects/UnrealProjects/simulator_test5.7/simulator_test57.uproject"

echo "Launching LunarSim-PG"

cd "$(dirname "$UE_BIN")" || exit 1
"$UE_BIN" "$PROJECT"
