#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="$ROOT/Soul_and_dungeon.uproject"
ENGINE="/Users/Shared/Epic Games/UE_5.7/Engine"
EDITOR="$ENGINE/Binaries/Mac/UnrealEditor"
EDITOR_APP="$ENGINE/Binaries/Mac/UnrealEditor.app"
UBT="$ENGINE/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool"
PROJECT_DYLIB="$ROOT/Binaries/Mac/UnrealEditor-Soul_and_dungeon.dylib"
PROJECT_LOG="$HOME/Library/Logs/Unreal Engine/Soul_and_dungeonEditor/Soul_and_dungeon.log"

find_editor_pids() {
  {
    pgrep -f "UnrealEditor.*Soul_and_dungeon\\.uproject" || true
    if [[ -f "$PROJECT_DYLIB" ]]; then
      lsof -t "$PROJECT_DYLIB" 2>/dev/null || true
    fi
    if [[ -f "$PROJECT_LOG" ]]; then
      lsof -t "$PROJECT_LOG" 2>/dev/null || true
    fi
  } | sort -u
}

echo "Closing existing Soul_and_dungeon editor instances..."
EDITOR_PIDS="$(find_editor_pids)"
if [[ -n "$EDITOR_PIDS" ]]; then
  kill $EDITOR_PIDS || true
fi
sleep 2

EDITOR_PIDS="$(find_editor_pids)"
if [[ -n "$EDITOR_PIDS" ]]; then
  echo "Force-closing remaining editor instances..."
  kill -9 $EDITOR_PIDS || true
  sleep 1
fi

echo "Building Soul_and_dungeonEditor Mac Development..."
"$UBT" Soul_and_dungeonEditor Mac Development "-Project=$PROJECT" -waitmutex

echo "Reopening Soul_and_dungeon..."
open -na "$EDITOR_APP" --args "$PROJECT"

echo "Done."
