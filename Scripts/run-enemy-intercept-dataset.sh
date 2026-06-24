#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="$ROOT/Soul_and_dungeon.uproject"
ENGINE="${UNREAL_ENGINE_DIR:-/Users/Shared/Epic Games/UE_5.7/Engine}"
EDITOR_CMD="$ENGINE/Binaries/Mac/UnrealEditor-Cmd"

EPISODES="500"
MAP="/Game/ThirdPerson/Lvl_ThirdPerson"
OUTPUT="Saved/EnemyLearning/InterceptDataset/intercept_samples.csv"
SEED="1234"
USE_LMSTUDIO_PLAYER="false"
LMSTUDIO_URL="http://localhost:1234/v1"
MODEL="google/gemma-3-270m"
MIN_START_DISTANCE="650"
MAX_START_DISTANCE="1400"
FORCE_PLAYER_MOVING="false"
PLAYER_SPEED_SCALE_MIN="0.75"
PLAYER_SPEED_SCALE_MAX="1.25"
SCENARIO_TYPE="OpenField"
EVALUATION_WINDOW_SECONDS="6.0"
FIXED_DELTA_SECONDS="0.033333333"
USE_PROGRESS_SCORE="false"
EVALUATE_LEARNED_POLICY="false"
LEARNED_POLICY_PATH="Saved/EnemyLearning/Models/enemy_intercept_policy_runtime.json"
POLICY_EVAL_REPORT="Saved/EnemyLearning/Reports/enemy_intercept_policy_eval_report.json"
CLEAR_OUTPUT="false"
ENEMY_INTERCEPT_MODE="-1"
COMPARE_WITH_CURRENT="false"
VISUAL_DEBUG="false"

truthy() {
  local value
  value="$(printf '%s' "$1" | tr '[:upper:]' '[:lower:]')"
  case "$value" in
    1|true|yes|on) return 0 ;;
    *) return 1 ;;
  esac
}

resolve_path() {
  case "$1" in
    /*) printf '%s\n' "$1" ;;
    *) printf '%s\n' "$ROOT/$1" ;;
  esac
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --episodes)
      EPISODES="${2:?missing value for --episodes}"
      shift 2
      ;;
    --map)
      MAP="${2:?missing value for --map}"
      shift 2
      ;;
    --output)
      OUTPUT="${2:?missing value for --output}"
      shift 2
      ;;
    --seed)
      SEED="${2:?missing value for --seed}"
      shift 2
      ;;
    --use-lmstudio-player)
      USE_LMSTUDIO_PLAYER="${2:?missing value for --use-lmstudio-player}"
      shift 2
      ;;
    --lmstudio-url)
      LMSTUDIO_URL="${2:?missing value for --lmstudio-url}"
      shift 2
      ;;
    --model)
      MODEL="${2:?missing value for --model}"
      shift 2
      ;;
    --min-start-distance)
      MIN_START_DISTANCE="${2:?missing value for --min-start-distance}"
      shift 2
      ;;
    --max-start-distance)
      MAX_START_DISTANCE="${2:?missing value for --max-start-distance}"
      shift 2
      ;;
    --force-player-moving)
      FORCE_PLAYER_MOVING="${2:?missing value for --force-player-moving}"
      shift 2
      ;;
    --player-speed-scale-min)
      PLAYER_SPEED_SCALE_MIN="${2:?missing value for --player-speed-scale-min}"
      shift 2
      ;;
    --player-speed-scale-max)
      PLAYER_SPEED_SCALE_MAX="${2:?missing value for --player-speed-scale-max}"
      shift 2
      ;;
    --scenario-type)
      SCENARIO_TYPE="${2:?missing value for --scenario-type}"
      shift 2
      ;;
    --evaluation-window-seconds)
      EVALUATION_WINDOW_SECONDS="${2:?missing value for --evaluation-window-seconds}"
      shift 2
      ;;
    --fixed-delta-seconds)
      FIXED_DELTA_SECONDS="${2:?missing value for --fixed-delta-seconds}"
      shift 2
      ;;
    --use-progress-score)
      USE_PROGRESS_SCORE="${2:?missing value for --use-progress-score}"
      shift 2
      ;;
    --evaluate-learned-policy)
      EVALUATE_LEARNED_POLICY="${2:?missing value for --evaluate-learned-policy}"
      shift 2
      ;;
    --learned-policy-path)
      LEARNED_POLICY_PATH="${2:?missing value for --learned-policy-path}"
      shift 2
      ;;
    --policy-eval-report)
      POLICY_EVAL_REPORT="${2:?missing value for --policy-eval-report}"
      shift 2
      ;;
    --clear-output)
      CLEAR_OUTPUT="${2:?missing value for --clear-output}"
      shift 2
      ;;
    --append)
      if truthy "${2:?missing value for --append}"; then
        CLEAR_OUTPUT="false"
      else
        CLEAR_OUTPUT="true"
      fi
      shift 2
      ;;
    --enemy-intercept-mode)
      ENEMY_INTERCEPT_MODE="${2:?missing value for --enemy-intercept-mode}"
      shift 2
      ;;
    --compare-with-current)
      COMPARE_WITH_CURRENT="${2:?missing value for --compare-with-current}"
      shift 2
      ;;
    --visual-debug)
      VISUAL_DEBUG="${2:?missing value for --visual-debug}"
      shift 2
      ;;
    -h|--help)
      cat <<EOF
Usage: Scripts/run-enemy-intercept-dataset.sh [options]

Options:
  --episodes N
  --map /Game/ThirdPerson/Lvl_ThirdPerson
  --output Saved/EnemyLearning/InterceptDataset/intercept_samples.csv
  --seed N
  --use-lmstudio-player true|false
  --lmstudio-url http://localhost:1234/v1
  --model google/gemma-3-270m
  --min-start-distance 650
  --max-start-distance 1600
  --force-player-moving true|false
  --player-speed-scale-min 0.75
  --player-speed-scale-max 1.25
  --scenario-type OpenField|SideCross|Retreat|DiagonalRetreat|ZigZag|Circle|Mixed
  --evaluation-window-seconds 6.0
  --fixed-delta-seconds 0.033333333
  --use-progress-score true|false
  --evaluate-learned-policy true|false
  --learned-policy-path Saved/EnemyLearning/Models/enemy_intercept_policy_runtime.json
  --policy-eval-report Saved/EnemyLearning/Reports/enemy_intercept_policy_eval_report.json
  --clear-output true|false
  --append true|false
  --enemy-intercept-mode -1|0..7|Off|Deterministic|Learned|Predict035...
  --compare-with-current true|false
  --visual-debug true|false

Set UNREAL_ENGINE_DIR to override the default UE path:
  $ENGINE
EOF
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

if [[ ! -x "$EDITOR_CMD" ]]; then
  echo "UnrealEditor-Cmd not found or not executable: $EDITOR_CMD" >&2
  echo "Set UNREAL_ENGINE_DIR to the correct Unreal Engine root." >&2
  exit 1
fi

if [[ ! -f "$PROJECT" ]]; then
  echo "Project file not found: $PROJECT" >&2
  exit 1
fi

echo "Running EnemyInterceptDataset commandlet..."
echo "  episodes: $EPISODES"
echo "  map: $MAP"
echo "  output: $OUTPUT"
echo "  seed: $SEED"
echo "  use LM Studio player: $USE_LMSTUDIO_PLAYER"
echo "  scenario type: $SCENARIO_TYPE"
echo "  start distance: $MIN_START_DISTANCE..$MAX_START_DISTANCE"
echo "  player speed scale: $PLAYER_SPEED_SCALE_MIN..$PLAYER_SPEED_SCALE_MAX"
echo "  force player moving: $FORCE_PLAYER_MOVING"
echo "  progress score: $USE_PROGRESS_SCORE"
echo "  evaluate learned policy: $EVALUATE_LEARNED_POLICY"
echo "  clear output: $CLEAR_OUTPUT"
echo "  enemy intercept mode: $ENEMY_INTERCEPT_MODE"

if truthy "$CLEAR_OUTPUT"; then
  rm -f "$(resolve_path "$OUTPUT")"
fi

"$EDITOR_CMD" "$PROJECT" \
  -run=EnemyInterceptDataset \
  "-Map=$MAP" \
  "-Episodes=$EPISODES" \
  "-Output=$OUTPUT" \
  "-Seed=$SEED" \
  "-UseLMStudioPlayer=$USE_LMSTUDIO_PLAYER" \
  "-LMStudioUrl=$LMSTUDIO_URL" \
  "-LMStudioModel=$MODEL" \
  "-MinStartDistance=$MIN_START_DISTANCE" \
  "-MaxStartDistance=$MAX_START_DISTANCE" \
  "-ForcePlayerMoving=$FORCE_PLAYER_MOVING" \
  "-PlayerSpeedScaleMin=$PLAYER_SPEED_SCALE_MIN" \
  "-PlayerSpeedScaleMax=$PLAYER_SPEED_SCALE_MAX" \
  "-ScenarioType=$SCENARIO_TYPE" \
  "-EvaluationWindowSeconds=$EVALUATION_WINDOW_SECONDS" \
  "-FixedDeltaSeconds=$FIXED_DELTA_SECONDS" \
  "-UseProgressScore=$USE_PROGRESS_SCORE" \
  "-EvaluateLearnedPolicy=$EVALUATE_LEARNED_POLICY" \
  "-LearnedPolicyPath=$LEARNED_POLICY_PATH" \
  "-PolicyEvalReport=$POLICY_EVAL_REPORT" \
  "-ClearOutput=$CLEAR_OUTPUT" \
  "-EnemyInterceptMode=$ENEMY_INTERCEPT_MODE" \
  "-CompareWithCurrent=$COMPARE_WITH_CURRENT" \
  "-VisualDebug=$VISUAL_DEBUG" \
  -unattended \
  -nop4
