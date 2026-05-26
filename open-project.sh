#!/usr/bin/env bash

UE_BIN="$HOME/Downloads/Linux_Unreal_Engine_5.7.4/Engine/Binaries/Linux/UnrealEditor"
PROJECT="$HOME/Projects/UnrealProjects/simulator_test5.7/simulator_test57.uproject"

cd "$(dirname "$UE_BIN")" || exit 1

"$UE_BIN" "$PROJECT" -VulkanNoSync
