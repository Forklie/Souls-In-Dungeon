#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT_FILE="$PROJECT_ROOT/Soul_and_dungeon.uproject"
UE_CMD="/Users/Shared/Epic Games/UE_5.7/Engine/Binaries/Mac/UnrealEditor-Cmd"
MAP="/Game/ThirdPerson/Lvl_ThirdPerson"

CYCLES=1
TRAIN_STEPS=100
ITERATIONS=1
LM_ENDPOINT="http://localhost:1234/v1/chat/completions"
LM_MODEL=""
LM_TIMEOUT=10
FOREVER=0
PARALLEL=1

while [[ $# -gt 0 ]]; do
  case "$1" in
    --cycles) CYCLES="$2"; shift 2 ;;
    --train-steps) TRAIN_STEPS="$2"; shift 2 ;;
    --iterations) ITERATIONS="$2"; shift 2 ;;
    --lm-endpoint) LM_ENDPOINT="$2"; shift 2 ;;
    --model) LM_MODEL="$2"; shift 2 ;;
    --lm-timeout) LM_TIMEOUT="$2"; shift 2 ;;
    --forever) FOREVER=1; shift ;;
    --parallel) PARALLEL="$2"; shift 2 ;;
    *) echo "Unknown argument: $1" >&2; exit 2 ;;
  esac
done

run_cmdlet() {
  "$UE_CMD" "$PROJECT_FILE" "$@" -unattended -nop4
}

cycle_index=0
while [[ "$FOREVER" == "1" || "$cycle_index" -lt "$CYCLES" ]]; do
  run_id="$(date +"%Y%m%d_%H%M%S")_${cycle_index}"
  run_dir="$PROJECT_ROOT/Saved/EnemyLearning/Runs/$run_id"
  mkdir -p "$run_dir"

  candidate_asset="/Game/AI/Learning/Candidates/NN_EnemySteering_${run_id}"
  stable_asset="/Game/AI/Learning/NN_EnemySteering"
  candidate_file="$PROJECT_ROOT/Content/AI/Learning/Candidates/NN_EnemySteering_${run_id}.uasset"
  stable_file="$PROJECT_ROOT/Content/AI/Learning/NN_EnemySteering.uasset"

  echo "Training candidate $candidate_asset"
  run_cmdlet \
    -run=EnemyLearningTrain \
    -Map="$MAP" \
    -Steps="$TRAIN_STEPS" \
    -Iterations="$ITERATIONS" \
    -MaxEpisodeSteps=1200 \
    -UseLMStudioPlayer=true \
    -LMStudioEndpoint="$LM_ENDPOINT" \
    -LMStudioModel="$LM_MODEL" \
    -LMStudioTimeout="$LM_TIMEOUT" \
    -ParallelAgents="$PARALLEL" \
    -OutputPolicy="$candidate_asset" \
    -SummaryPath="$run_dir/training_summary.json"

  echo "Evaluating candidate"
  run_cmdlet \
    -run=EnemyLearningEvaluate \
    -Map="$MAP" \
    -Policy="$candidate_asset" \
    -Output="$run_dir/candidate_eval.json" \
    -Behavior=LMStudioEvasive \
    -UseLMStudioPlayer=true \
    -LMStudioEndpoint="$LM_ENDPOINT" \
    -LMStudioModel="$LM_MODEL" \
    -LMStudioTimeout="$LM_TIMEOUT" \
    -Episodes=3 \
    -EpisodeSteps=600 \
    -Seed=1234

  if [[ -f "$stable_file" ]]; then
    echo "Evaluating current stable policy"
    run_cmdlet \
      -run=EnemyLearningEvaluate \
      -Map="$MAP" \
      -Policy="$stable_asset" \
      -Output="$run_dir/baseline_eval.json" \
      -Behavior=LMStudioEvasive \
      -UseLMStudioPlayer=true \
      -LMStudioEndpoint="$LM_ENDPOINT" \
      -LMStudioModel="$LM_MODEL" \
      -LMStudioTimeout="$LM_TIMEOUT" \
      -Episodes=3 \
      -EpisodeSteps=600 \
      -Seed=1234
  else
    echo "No stable policy found; evaluating smoothed A* fallback baseline"
    run_cmdlet \
      -run=EnemyLearningEvaluate \
      -Map="$MAP" \
      -Output="$run_dir/baseline_eval.json" \
      -Behavior=DeterministicEvasive \
      -UseLMStudioPlayer=false \
      -Episodes=3 \
      -EpisodeSteps=600 \
      -Seed=1234
  fi

  echo "Evaluating fallback safety with LM Studio disabled"
  run_cmdlet \
    -run=EnemyLearningEvaluate \
    -Map="$MAP" \
    -Output="$run_dir/fallback_eval.json" \
    -Behavior=LMStudioEvasive \
    -UseLMStudioPlayer=false \
    -Episodes=1 \
    -EpisodeSteps=300 \
    -Seed=4321

  python3 - "$run_dir" "$candidate_file" "$stable_file" <<'PY'
import json
import shutil
import sys
from pathlib import Path

run_dir = Path(sys.argv[1])
candidate_file = Path(sys.argv[2])
stable_file = Path(sys.argv[3])

candidate = json.loads((run_dir / "candidate_eval.json").read_text())
baseline = json.loads((run_dir / "baseline_eval.json").read_text())
fallback = json.loads((run_dir / "fallback_eval.json").read_text())

reasons = []
accepted = True

if candidate.get("success_rate", 0) < baseline.get("success_rate", 0):
    accepted = False
    reasons.append("candidate success rate is lower than baseline")

candidate_time = candidate.get("average_chase_time", 999999)
baseline_time = baseline.get("average_chase_time", 999999)
if baseline_time > 0 and candidate_time > baseline_time * 1.05:
    accepted = False
    reasons.append("candidate average chase time regressed by more than 5 percent")

if candidate.get("stuck_episodes", 0) > baseline.get("stuck_episodes", 0):
    accepted = False
    reasons.append("candidate stuck episodes increased")

baseline_fallbacks = baseline.get("astar_fallbacks", 0)
allowed_fallbacks = max(baseline_fallbacks + 1, int(baseline_fallbacks * 1.10))
if candidate.get("astar_fallbacks", 0) > allowed_fallbacks:
    accepted = False
    reasons.append("candidate A* fallback count increased by more than allowed")

if fallback.get("episodes", 0) <= 0:
    accepted = False
    reasons.append("fallback safety evaluation did not run")

if not candidate_file.exists():
    accepted = False
    reasons.append("candidate policy asset was not saved")

decision = {
    "accepted": accepted,
    "reasons": reasons,
    "candidate_eval": str(run_dir / "candidate_eval.json"),
    "baseline_eval": str(run_dir / "baseline_eval.json"),
    "fallback_eval": str(run_dir / "fallback_eval.json"),
    "promoted_to": str(stable_file) if accepted else "",
}

(run_dir / "promotion_decision.json").write_text(json.dumps(decision, indent=2) + "\n")

if accepted:
    stable_file.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(candidate_file, stable_file)
    print(f"Accepted and promoted: {stable_file}")
else:
    print("Rejected candidate:")
    for reason in reasons:
        print(f"- {reason}")
PY

  echo "Decision written to $run_dir/promotion_decision.json"
  cycle_index=$((cycle_index + 1))
done
