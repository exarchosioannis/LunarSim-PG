#!/usr/bin/env bash
set -e

export ROS_DOMAIN_ID=0
export ROS_LOCALHOST_ONLY=0
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp

UE_BIN="$HOME/UnrealEngine_5.7.4/Engine/Binaries/Linux/UnrealEditor"
PROJECT="$HOME/Projects/UnrealProjects/simulator_test5.7/simulator_test57.uproject"

echo "Launching Unreal with:"
echo "ROS_DOMAIN_ID=$ROS_DOMAIN_ID"
echo "ROS_LOCALHOST_ONLY=$ROS_LOCALHOST_ONLY"
echo "RMW_IMPLEMENTATION=$RMW_IMPLEMENTATION"

cd "$(dirname "$UE_BIN")" || exit 1
"$UE_BIN" "$PROJECT"
