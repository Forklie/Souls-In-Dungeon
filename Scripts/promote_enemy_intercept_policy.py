#!/usr/bin/env python3
import argparse
import hashlib
import json
import shutil
from datetime import datetime, timezone
from pathlib import Path


EXPECTED_FEATURES = [
    "PlayerSpeed",
    "EnemySpeed",
    "DistanceToPlayer",
    "ZDelta",
    "LineOfSight",
    "DotPlayerMoveWithEnemyDirection",
    "RecentPlayerTurnAmount",
    "TimeSinceLastPlayerDirectionChange",
    "PlayerVelocityX",
    "PlayerVelocityY",
    "PlayerVelocityZ",
    "EnemyVelocityX",
    "EnemyVelocityY",
    "EnemyVelocityZ",
    "EnemyLocationX",
    "EnemyLocationY",
    "EnemyLocationZ",
    "PlayerLocationX",
    "PlayerLocationY",
    "PlayerLocationZ",
]

MODE_NAMES = ["CurrentLocation", "Predict035", "Predict075", "Predict125", "Predict175"]


def load_json(path: Path):
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def write_json(path: Path, payload: dict):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def get_number(obj: dict, key: str, default=0.0) -> float:
    value = obj.get(key, default)
    try:
        return float(value)
    except (TypeError, ValueError):
        return float(default)


def sum_distribution(distribution: dict) -> int:
    return sum(int(distribution.get(name, 0)) for name in MODE_NAMES)


def distribution_max_share(distribution: dict) -> float:
    total = sum_distribution(distribution)
    if total <= 0:
        return 0.0
    return max(int(distribution.get(name, 0)) for name in MODE_NAMES) / total


def predict035_balanced_accuracy(distribution: dict) -> float:
    present_classes = [name for name in MODE_NAMES if int(distribution.get(name, 0)) > 0]
    if not present_classes:
        return 0.0
    return 1.0 / len(present_classes) if int(distribution.get("Predict035", 0)) > 0 else 0.0


def validate_policy(policy: dict):
    errors = []
    if policy.get("format_version") != 1:
        errors.append("candidate policy format_version must be 1")
    if policy.get("model_type") != "RandomForestClassifier":
        errors.append("candidate policy model_type must be RandomForestClassifier")
    if policy.get("runtime_feature_set") != "EnemyInterceptObservationV1":
        errors.append("candidate policy runtime_feature_set must be EnemyInterceptObservationV1")
    if policy.get("class_labels") != [0, 1, 2, 3, 4]:
        errors.append("candidate policy class_labels must be [0,1,2,3,4]")
    if policy.get("feature_names") != EXPECTED_FEATURES:
        errors.append("candidate policy feature_names do not match the runtime feature schema")
    if not policy.get("trees"):
        errors.append("candidate policy has no trees")
    return errors


def backup_stable_policy(stable_policy: Path):
    if not stable_policy.exists():
        return None
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    backup_dir = stable_policy.parent / "Backups"
    backup_dir.mkdir(parents=True, exist_ok=True)
    backup_path = backup_dir / f"{timestamp}_{stable_policy.name}"
    shutil.copy2(stable_policy, backup_path)
    return str(backup_path)


def main():
    parser = argparse.ArgumentParser(description="Promote or reject a candidate enemy intercept runtime policy using objective metrics.")
    parser.add_argument("--candidate-policy", required=True)
    parser.add_argument("--eval-report", required=True)
    parser.add_argument("--stable-policy", default="Saved/EnemyLearning/Models/enemy_intercept_policy_runtime.json")
    parser.add_argument("--output-decision", required=True)
    parser.add_argument("--max-path-failure-count", type=int, default=0)
    parser.add_argument("--max-invalid-target-rate", type=float, default=0.35)
    parser.add_argument("--max-fallback-rate", type=float, default=40.0)
    parser.add_argument("--balanced-accuracy-margin", type=float, default=0.15)
    parser.add_argument("--accuracy-tolerance-vs-predict035", type=float, default=0.03)
    parser.add_argument("--score-gap-multiplier-vs-predict035", type=float, default=1.10)
    parser.add_argument("--max-learned-mode-share", type=float, default=0.80)
    args = parser.parse_args()

    candidate_policy = Path(args.candidate_policy)
    eval_report = Path(args.eval_report)
    stable_policy = Path(args.stable_policy)
    decision_path = Path(args.output_decision)

    reasons = []
    passed = []

    if not candidate_policy.exists():
        reasons.append(f"candidate policy does not exist: {candidate_policy}")
        policy = {}
    else:
        try:
            policy = load_json(candidate_policy)
            schema_errors = validate_policy(policy)
            if schema_errors:
                reasons.extend(schema_errors)
            else:
                passed.append("candidate policy schema is runtime-compatible")
        except Exception as exc:
            policy = {}
            reasons.append(f"candidate policy is not readable JSON: {exc}")

    if not eval_report.exists():
        reasons.append(f"eval report does not exist: {eval_report}")
        report = {}
    else:
        try:
            report = load_json(eval_report)
            passed.append("eval report is readable JSON")
        except Exception as exc:
            report = {}
            reasons.append(f"eval report is not readable JSON: {exc}")

    accuracy = report.get("accuracy", {})
    scores = report.get("score_comparison", {})
    safety = report.get("safety_metrics", {})
    distributions = report.get("distributions", {})
    oracle_distribution = distributions.get("oracle_label_distribution", {})
    learned_distribution = distributions.get("learned_chosen_mode_distribution", {})
    total_scenarios = int(report.get("total_scenarios", 0) or report.get("episodes", 0) or 0)

    learned_accuracy = get_number(accuracy, "learned_initial_choice_accuracy")
    deterministic_accuracy = get_number(accuracy, "deterministic_initial_choice_accuracy")
    predict035_accuracy = get_number(accuracy, "predict035_baseline_accuracy")
    learned_balanced = get_number(accuracy, "learned_balanced_accuracy")
    deterministic_balanced = get_number(accuracy, "deterministic_balanced_accuracy")
    predict035_balanced = predict035_balanced_accuracy(oracle_distribution)

    learned_gap = get_number(scores, "average_learned_score_gap_from_oracle")
    deterministic_gap = get_number(scores, "average_deterministic_score_gap_from_oracle")
    predict035_gap = get_number(scores, "average_predict035_score_gap_from_oracle")

    learned_path_failures = int(get_number(safety, "learned_chosen_path_failure_count"))
    invalid_rate = get_number(safety, "average_learned_chosen_invalid_target_count")
    fallback_rate = get_number(safety, "average_learned_chosen_fallback_count")
    learned_max_share = distribution_max_share(learned_distribution)
    oracle_max_share = distribution_max_share(oracle_distribution)

    gates = [
        (learned_path_failures <= args.max_path_failure_count, f"path failures {learned_path_failures} <= {args.max_path_failure_count}"),
        (invalid_rate <= args.max_invalid_target_rate, f"invalid target rate {invalid_rate:.4f} <= {args.max_invalid_target_rate:.4f}"),
        (fallback_rate <= args.max_fallback_rate, f"fallback rate {fallback_rate:.4f} <= {args.max_fallback_rate:.4f}"),
        (learned_balanced >= predict035_balanced + args.balanced_accuracy_margin, f"learned balanced accuracy {learned_balanced:.4f} >= Predict035 balanced {predict035_balanced:.4f} + {args.balanced_accuracy_margin:.4f}"),
        (learned_accuracy >= predict035_accuracy - args.accuracy_tolerance_vs_predict035, f"learned accuracy {learned_accuracy:.4f} >= Predict035 accuracy {predict035_accuracy:.4f} - {args.accuracy_tolerance_vs_predict035:.4f}"),
        (learned_accuracy > deterministic_accuracy, f"learned accuracy {learned_accuracy:.4f} > deterministic accuracy {deterministic_accuracy:.4f}"),
        (learned_balanced > deterministic_balanced, f"learned balanced accuracy {learned_balanced:.4f} > deterministic balanced accuracy {deterministic_balanced:.4f}"),
        (learned_gap <= deterministic_gap, f"learned score gap {learned_gap:.4f} <= deterministic score gap {deterministic_gap:.4f}"),
        (learned_gap <= predict035_gap * args.score_gap_multiplier_vs_predict035, f"learned score gap {learned_gap:.4f} <= Predict035 gap {predict035_gap:.4f} * {args.score_gap_multiplier_vs_predict035:.4f}"),
        (learned_max_share <= max(args.max_learned_mode_share, oracle_max_share + 0.15), f"learned chosen-mode max share {learned_max_share:.4f} is not collapsed relative to oracle max share {oracle_max_share:.4f}"),
    ]

    for ok, message in gates:
        if ok:
            passed.append(message)
        else:
            reasons.append(message)

    decision = "promote" if not reasons else "reject"
    backup_path = None
    promoted_to = None
    if decision == "promote":
        backup_path = backup_stable_policy(stable_policy)
        stable_policy.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(candidate_policy, stable_policy)
        promoted_to = str(stable_policy)

    metrics = {
        "total_scenarios": total_scenarios,
        "learned_accuracy": learned_accuracy,
        "deterministic_accuracy": deterministic_accuracy,
        "predict035_accuracy": predict035_accuracy,
        "learned_balanced_accuracy": learned_balanced,
        "deterministic_balanced_accuracy": deterministic_balanced,
        "predict035_balanced_accuracy": predict035_balanced,
        "learned_avg_score_gap_from_oracle": learned_gap,
        "deterministic_avg_score_gap_from_oracle": deterministic_gap,
        "predict035_avg_score_gap_from_oracle": predict035_gap,
        "learned_path_failures": learned_path_failures,
        "learned_invalid_target_rate": invalid_rate,
        "learned_fallback_rate": fallback_rate,
        "learned_max_mode_share": learned_max_share,
        "oracle_max_label_share": oracle_max_share,
    }
    thresholds = {
        "max_path_failure_count": args.max_path_failure_count,
        "max_invalid_target_rate": args.max_invalid_target_rate,
        "max_fallback_rate": args.max_fallback_rate,
        "balanced_accuracy_margin": args.balanced_accuracy_margin,
        "accuracy_tolerance_vs_predict035": args.accuracy_tolerance_vs_predict035,
        "score_gap_multiplier_vs_predict035": args.score_gap_multiplier_vs_predict035,
        "max_learned_mode_share": args.max_learned_mode_share,
    }

    payload = {
        "decision": decision,
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "reasons": reasons,
        "passed_gates": passed,
        "metrics": metrics,
        "thresholds": thresholds,
        "candidate_policy": str(candidate_policy),
        "candidate_policy_sha256": sha256_file(candidate_policy) if candidate_policy.exists() else "",
        "stable_policy": str(stable_policy),
        "stable_backup": backup_path,
        "promoted_to": promoted_to,
        "eval_report": str(eval_report),
    }
    write_json(decision_path, payload)

    if decision == "promote":
        stable_eval_copy = stable_policy.parent / "enemy_intercept_policy_last_promotion_eval_report.json"
        stable_decision_copy = stable_policy.parent / "enemy_intercept_policy_last_promotion_decision.json"
        shutil.copy2(eval_report, stable_eval_copy)
        shutil.copy2(decision_path, stable_decision_copy)

    print(f"Decision: {decision.upper()}")
    print(f"Candidate: {candidate_policy}")
    print(f"Eval report: {eval_report}")
    print(f"Output decision: {decision_path}")
    print(
        "Metrics: "
        f"accuracy learned={learned_accuracy:.2%}, predict035={predict035_accuracy:.2%}, deterministic={deterministic_accuracy:.2%}; "
        f"balanced learned={learned_balanced:.2%}, predict035={predict035_balanced:.2%}; "
        f"score gap learned={learned_gap:.2f}, predict035={predict035_gap:.2f}, deterministic={deterministic_gap:.2f}"
    )
    if reasons:
        print("Reject reasons:")
        for reason in reasons:
            print(f"  - {reason}")
    else:
        print(f"Promoted to: {stable_policy}")


if __name__ == "__main__":
    main()
