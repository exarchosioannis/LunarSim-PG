#!/usr/bin/env bash
set -e

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_FILE="$PROJECT_DIR/simulator_test57.uproject"
UE57="$HOME/Downloads/Linux_Unreal_Engine_5.7.4"

echo "Cleaning generated folders..."
rm -rf "$PROJECT_DIR/Binaries"
rm -rf "$PROJECT_DIR/Intermediate"
rm -rf "$PROJECT_DIR/.vs"

echo "Building project with Unreal Engine..."
"$UE57/Engine/Build/BatchFiles/Linux/Build.sh" simulatorEditor Linux Development "$PROJECT_FILE"

echo "Done."
