#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

CYCLES="1"
EPISODES="3000"
EVAL_EPISODES="500"
MAP="/Game/Maps/EnemyLearningArena"
SCENARIO_TYPE="Mixed"
MIN_START_DISTANCE="600"
MAX_START_DISTANCE="1600"
FORCE_PLAYER_MOVING="true"
USE_PROGRESS_SCORE="true"
USE_LMSTUDIO_PLAYER="false"
SEED="1234"
LLM_REVIEW="false"
LMSTUDIO_URL="http://localhost:1234/v1"
LMSTUDIO_MODEL="google/gemma-3-270m"
STABLE_POLICY="Saved/EnemyLearning/Models/enemy_intercept_policy_runtime.json"

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

usage() {
  cat <<EOF
Usage: Scripts/run-continuous-enemy-intercept-learning.sh [options]

Options:
  --cycles N
  --episodes N
  --eval-episodes N
  --map /Game/Maps/EnemyLearningArena
  --scenario-type Mixed
  --min-start-distance 600
  --max-start-distance 1600
  --force-player-moving true|false
  --use-progress-score true|false
  --use-lmstudio-player true|false
  --seed N
  --llm-review true|false
  --lmstudio-url http://localhost:1234/v1
  --lmstudio-model google/gemma-3-270m
  --stable-policy Saved/EnemyLearning/Models/enemy_intercept_policy_runtime.json
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --cycles) CYCLES="${2:?missing value for --cycles}"; shift 2 ;;
    --episodes) EPISODES="${2:?missing value for --episodes}"; shift 2 ;;
    --eval-episodes) EVAL_EPISODES="${2:?missing value for --eval-episodes}"; shift 2 ;;
    --map) MAP="${2:?missing value for --map}"; shift 2 ;;
    --scenario-type) SCENARIO_TYPE="${2:?missing value for --scenario-type}"; shift 2 ;;
    --min-start-distance) MIN_START_DISTANCE="${2:?missing value for --min-start-distance}"; shift 2 ;;
    --max-start-distance) MAX_START_DISTANCE="${2:?missing value for --max-start-distance}"; shift 2 ;;
    --force-player-moving) FORCE_PLAYER_MOVING="${2:?missing value for --force-player-moving}"; shift 2 ;;
    --use-progress-score) USE_PROGRESS_SCORE="${2:?missing value for --use-progress-score}"; shift 2 ;;
    --use-lmstudio-player) USE_LMSTUDIO_PLAYER="${2:?missing value for --use-lmstudio-player}"; shift 2 ;;
    --seed) SEED="${2:?missing value for --seed}"; shift 2 ;;
    --llm-review) LLM_REVIEW="${2:?missing value for --llm-review}"; shift 2 ;;
    --lmstudio-url) LMSTUDIO_URL="${2:?missing value for --lmstudio-url}"; shift 2 ;;
    --lmstudio-model|--model) LMSTUDIO_MODEL="${2:?missing value for --lmstudio-model}"; shift 2 ;;
    --stable-policy) STABLE_POLICY="${2:?missing value for --stable-policy}"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

run_logged() {
  local log_path="$1"
  shift
  mkdir -p "$(dirname "$log_path")"
  set +e
  "$@" 2>&1 | tee "$log_path"
  local status="${PIPESTATUS[0]}"
  set -e
  return "$status"
}

write_cycle_metadata() {
  local output_path="$1"
  local run_id="$2"
  local cycle="$3"
  local status="$4"
  local decision_path="$5"
  python3 - "$output_path" "$run_id" "$cycle" "$status" "$decision_path" <<'PY'
import json
import sys
from datetime import datetime, timezone
from pathlib import Path

output, run_id, cycle, status, decision_path = sys.argv[1:6]
decision = None
if decision_path and Path(decision_path).exists():
    try:
        decision = json.loads(Path(decision_path).read_text())
    except Exception:
        decision = None
payload = {
    "run_id": run_id,
    "cycle": int(cycle),
    "status": status,
    "timestamp": datetime.now(timezone.utc).isoformat(),
    "promotion_decision": decision.get("decision") if isinstance(decision, dict) else None,
    "promotion_reasons": decision.get("reasons") if isinstance(decision, dict) else None,
}
Path(output).parent.mkdir(parents=True, exist_ok=True)
Path(output).write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")
PY
}

print_decision_summary() {
  local decision_path="$1"
  python3 - "$decision_path" <<'PY'
import json
import sys
from pathlib import Path

path = Path(sys.argv[1])
if not path.exists():
    print("Promotion decision: missing")
    raise SystemExit(0)
d = json.loads(path.read_text())
m = d.get("metrics", {})
print(f"Promotion decision: {d.get('decision', 'unknown').upper()}")
print(
    "Summary: "
    f"learned_acc={m.get('learned_accuracy', 0):.2%}, "
    f"predict035_acc={m.get('predict035_accuracy', 0):.2%}, "
    f"learned_bal={m.get('learned_balanced_accuracy', 0):.2%}, "
    f"learned_gap={m.get('learned_avg_score_gap_from_oracle', 0):.2f}, "
    f"predict035_gap={m.get('predict035_avg_score_gap_from_oracle', 0):.2f}, "
    f"path_failures={m.get('learned_path_failures', 0)}"
)
for reason in d.get("reasons", [])[:5]:
    print(f"  - {reason}")
PY
}

RUNS_ROOT="$ROOT/Saved/EnemyLearning/InterceptRuns"
STABLE_POLICY_ABS="$(resolve_path "$STABLE_POLICY")"
mkdir -p "$RUNS_ROOT"

echo "Continuous enemy intercept learning"
echo "  cycles: $CYCLES"
echo "  train episodes: $EPISODES"
echo "  eval episodes: $EVAL_EPISODES"
echo "  map: $MAP"
echo "  scenario: $SCENARIO_TYPE"
echo "  stable policy: $STABLE_POLICY_ABS"
echo "  llm review: $LLM_REVIEW"

for ((cycle=1; cycle<=CYCLES; cycle++)); do
  timestamp="$(date -u +%Y%m%dT%H%M%SZ)"
  run_id="${timestamp}_cycle_${cycle}"
  run_dir="$RUNS_ROOT/$run_id"
  models_dir="$run_dir/Models"
  reports_dir="$run_dir/Reports"
  logs_dir="$run_dir/Logs"
  mkdir -p "$models_dir" "$reports_dir" "$logs_dir"

  dataset_csv="$run_dir/intercept_samples.csv"
  eval_csv="$run_dir/intercept_eval_samples.csv"
  training_report="$reports_dir/enemy_intercept_training_report.json"
  candidate_policy="$models_dir/enemy_intercept_policy_runtime.json"
  policy_eval_report="$reports_dir/enemy_intercept_policy_eval_report.json"
  promotion_decision="$reports_dir/enemy_intercept_promotion_decision.json"
  llm_review_output="$reports_dir/enemy_intercept_llm_review.json"
  cycle_seed="$((SEED + cycle - 1))"

  echo
  echo "=== Cycle $cycle/$CYCLES: $run_id ==="

  if ! run_logged "$logs_dir/dataset_generation.log" \
    "$ROOT/Scripts/run-enemy-intercept-dataset.sh" \
      --episodes "$EPISODES" \
      --map "$MAP" \
      --output "$dataset_csv" \
      --seed "$cycle_seed" \
      --scenario-type "$SCENARIO_TYPE" \
      --min-start-distance "$MIN_START_DISTANCE" \
      --max-start-distance "$MAX_START_DISTANCE" \
      --force-player-moving "$FORCE_PLAYER_MOVING" \
      --use-progress-score "$USE_PROGRESS_SCORE" \
      --use-lmstudio-player "$USE_LMSTUDIO_PLAYER" \
      --clear-output true; then
    write_cycle_metadata "$run_dir/cycle_metadata.json" "$run_id" "$cycle" "dataset_failed" ""
    echo "Cycle $cycle failed during dataset generation." >&2
    exit 1
  fi

  if ! run_logged "$logs_dir/training.log" \
    python3 "$ROOT/Scripts/train_enemy_intercept_model.py" \
      --csv "$dataset_csv" \
      --output-dir "$models_dir" \
      --report-dir "$reports_dir" \
      --export-runtime-json true; then
    write_cycle_metadata "$run_dir/cycle_metadata.json" "$run_id" "$cycle" "training_failed" ""
    echo "Cycle $cycle failed during training." >&2
    exit 1
  fi

  if [[ ! -f "$candidate_policy" ]]; then
    write_cycle_metadata "$run_dir/cycle_metadata.json" "$run_id" "$cycle" "missing_candidate_policy" ""
    echo "Candidate policy was not exported: $candidate_policy" >&2
    exit 1
  fi

  if ! run_logged "$logs_dir/policy_eval.log" \
    "$ROOT/Scripts/run-enemy-intercept-dataset.sh" \
      --episodes "$EVAL_EPISODES" \
      --map "$MAP" \
      --output "$eval_csv" \
      --seed "$((cycle_seed + 100000))" \
      --scenario-type "$SCENARIO_TYPE" \
      --min-start-distance "$MIN_START_DISTANCE" \
      --max-start-distance "$MAX_START_DISTANCE" \
      --force-player-moving "$FORCE_PLAYER_MOVING" \
      --use-progress-score "$USE_PROGRESS_SCORE" \
      --use-lmstudio-player false \
      --evaluate-learned-policy true \
      --learned-policy-path "$candidate_policy" \
      --policy-eval-report "$policy_eval_report" \
      --clear-output true; then
    write_cycle_metadata "$run_dir/cycle_metadata.json" "$run_id" "$cycle" "policy_eval_failed" ""
    echo "Cycle $cycle failed during policy evaluation." >&2
    exit 1
  fi

  if ! run_logged "$logs_dir/promotion.log" \
    python3 "$ROOT/Scripts/promote_enemy_intercept_policy.py" \
      --candidate-policy "$candidate_policy" \
      --eval-report "$policy_eval_report" \
      --stable-policy "$STABLE_POLICY_ABS" \
      --output-decision "$promotion_decision"; then
    write_cycle_metadata "$run_dir/cycle_metadata.json" "$run_id" "$cycle" "promotion_failed" "$promotion_decision"
    echo "Cycle $cycle failed during promotion decision." >&2
    exit 1
  fi

  if truthy "$LLM_REVIEW"; then
    run_logged "$logs_dir/llm_review.log" \
      python3 "$ROOT/Scripts/review_enemy_intercept_training_with_llm.py" \
        --eval-report "$policy_eval_report" \
        --training-report "$training_report" \
        --promotion-decision "$promotion_decision" \
        --lmstudio-url "$LMSTUDIO_URL" \
        --model "$LMSTUDIO_MODEL" \
        --output "$llm_review_output" || true
  fi

  write_cycle_metadata "$run_dir/cycle_metadata.json" "$run_id" "$cycle" "completed" "$promotion_decision"
  print_decision_summary "$promotion_decision"
  echo "Cycle folder: $run_dir"
done
