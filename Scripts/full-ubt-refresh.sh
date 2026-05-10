#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT_FILE="$PROJECT_ROOT/Soul_and_dungeon.uproject"
ENGINE_ROOT="${UE_ENGINE_ROOT:-/Users/Shared/Epic Games/UE_5.7/Engine}"
UBT="$ENGINE_ROOT/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool"
EDITOR="$ENGINE_ROOT/Binaries/Mac/UnrealEditor"
TARGET="${UE_TARGET:-Soul_and_dungeonEditor}"
PLATFORM="${UE_PLATFORM:-Mac}"
CONFIG="${UE_CONFIG:-Development}"
REOPEN=true
FORCE_CLOSE=false

usage() {
  cat <<'EOF'
Usage: Scripts/full-ubt-refresh.sh [--no-reopen] [--force-close]

Close Unreal Editor, run a full UBT editor build, then reopen the project.
Use this after changes Live Coding cannot safely reload:
  - headers with UCLASS/USTRUCT/UFUNCTION/UPROPERTY or layout changes
  - new/deleted source files
  - *.Build.cs, *.Target.cs, *.uplugin, or plugin module changes

Environment overrides:
  UE_ENGINE_ROOT=/path/to/UE/Engine
  UE_TARGET=Soul_and_dungeonEditor
  UE_PLATFORM=Mac
  UE_CONFIG=Development
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --no-reopen)
      REOPEN=false
      shift
      ;;
    --force-close)
      FORCE_CLOSE=true
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      usage >&2
      exit 2
      ;;
  esac
done

if [[ ! -x "$UBT" ]]; then
  echo "UnrealBuildTool not found or not executable: $UBT" >&2
  exit 1
fi

if [[ ! -x "$EDITOR" ]]; then
  echo "UnrealEditor not found or not executable: $EDITOR" >&2
  exit 1
fi

editor_pids() {
  pgrep -f "$EDITOR.*$PROJECT_FILE|UnrealEditor.*Soul_and_dungeon" || true
}

wait_for_editor_exit() {
  local timeout_seconds=45
  local elapsed=0

  while [[ -n "$(editor_pids)" && "$elapsed" -lt "$timeout_seconds" ]]; do
    sleep 1
    elapsed=$((elapsed + 1))
  done

  [[ -z "$(editor_pids)" ]]
}

pids="$(editor_pids)"
if [[ -n "$pids" ]]; then
  echo "Closing Unreal Editor for full UBT build..."
  osascript -e 'tell application "UnrealEditor" to quit' >/dev/null 2>&1 || true

  if ! wait_for_editor_exit; then
    if [[ "$FORCE_CLOSE" == true ]]; then
      echo "Editor did not close cleanly; force-closing remaining UnrealEditor process..."
      kill $pids >/dev/null 2>&1 || true
      sleep 3
    else
      echo "Unreal Editor is still running. Save/close it, or rerun with --force-close." >&2
      exit 1
    fi
  fi
fi

echo "Running full UBT build: $TARGET $PLATFORM $CONFIG"
"$UBT" "$TARGET" "$PLATFORM" "$CONFIG" "-Project=$PROJECT_FILE" -waitmutex

if [[ "$REOPEN" == true ]]; then
  echo "Reopening Unreal Editor..."
  nohup "$EDITOR" "$PROJECT_FILE" >/dev/null 2>&1 &
fi

echo "Full UBT refresh complete."
