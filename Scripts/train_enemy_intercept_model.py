#!/usr/bin/env python3
import argparse
import csv
import json
from collections import Counter
from pathlib import Path


REQUIRED_COLUMNS = [
    "PlayerSpeed",
    "EnemySpeed",
    "DistanceToPlayer",
    "ZDelta",
    "LineOfSight",
    "DotPlayerMoveWithEnemyDirection",
    "RecentPlayerTurnAmount",
    "TimeSinceLastPlayerDirectionChange",
    "BestModeLabel",
]


BASE_FEATURE_COLUMNS = [
    "PlayerSpeed",
    "EnemySpeed",
    "DistanceToPlayer",
    "ZDelta",
    "LineOfSight",
    "DotPlayerMoveWithEnemyDirection",
    "RecentPlayerTurnAmount",
    "TimeSinceLastPlayerDirectionChange",
]

OPTIONAL_NUMERIC_FEATURE_COLUMNS = [
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
    # Start distance is available before policy selection and is safe for a future runtime feature.
    "StartDistance_CurrentLocation",
]

RUNTIME_NUMERIC_FEATURE_COLUMNS = [
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

ANALYSIS_ONLY_PREFIXES = (
    "Score_",
    "FinalDistance_",
    "DistanceReduction_",
    "InvalidTargetCount_",
    "RepathCount_",
    "FallbackCount_",
    "PathFailureCount_",
    "TimeToAttackRange_",
)

MODE_COLUMNS = [
    ("CurrentLocation", 0),
    ("Predict035", 1),
    ("Predict075", 2),
    ("Predict125", 3),
    ("Predict175", 4),
]

MODE_LABELS = [mode_label for _, mode_label in MODE_COLUMNS]
MODE_NAMES_BY_LABEL = {mode_label: mode_name for mode_name, mode_label in MODE_COLUMNS}

RECOMMENDED_DATASET_COMMAND = (
    "Scripts/run-enemy-intercept-dataset.sh --episodes 500 "
    "--map /Game/Maps/EnemyLearningArena "
    "--scenario-type Mixed "
    "--min-start-distance 600 "
    "--max-start-distance 1600 "
    "--force-player-moving true "
    "--use-progress-score true"
)


def load_rows(path: Path):
    with path.open("r", newline="") as f:
        reader = csv.DictReader(f)
        missing = [column for column in REQUIRED_COLUMNS if column not in (reader.fieldnames or [])]
        if missing:
            raise SystemExit(f"Dataset is missing required columns: {', '.join(missing)}")
        return list(reader)


def to_float(value: str) -> float:
    try:
        return float(value)
    except (TypeError, ValueError):
        return 0.0


def write_json(path: Path, payload: dict):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")


def truthy_string(value: str) -> bool:
    return str(value).strip().lower() in {"1", "true", "yes", "on"}


def falsy_string(value: str) -> bool:
    return str(value).strip().lower() in {"0", "false", "no", "off", ""}


def average_column(rows, column: str):
    if not rows or column not in rows[0]:
        return None
    return sum(to_float(row.get(column)) for row in rows) / len(rows)


def derive_scenario_type(row):
    if row.get("ScenarioType"):
        return row["ScenarioType"].strip() or "Unknown"

    summary = row.get("PlayerActionSummary", "")
    if ";" in summary:
        return summary.split(";", 1)[0].strip() or "Unknown"

    return "Unknown"


def available_numeric_feature_columns(rows):
    if not rows:
        return []

    fieldnames = set(rows[0].keys())
    columns = [column for column in BASE_FEATURE_COLUMNS if column in fieldnames]
    columns.extend(column for column in OPTIONAL_NUMERIC_FEATURE_COLUMNS if column in fieldnames)
    return columns


def available_runtime_numeric_feature_columns(rows):
    if not rows:
        return []

    fieldnames = set(rows[0].keys())
    return [column for column in RUNTIME_NUMERIC_FEATURE_COLUMNS if column in fieldnames]


def build_categorical_vocabs(rows):
    if not rows:
        return {}

    fieldnames = set(rows[0].keys())
    vocabs = {}

    if "PlayerMovementSource" in fieldnames:
        values = sorted({(row.get("PlayerMovementSource") or "Unknown").strip() or "Unknown" for row in rows})
        vocabs["PlayerMovementSource"] = values

    if "ScenarioType" in fieldnames or any(";" in row.get("PlayerActionSummary", "") for row in rows):
        values = sorted({derive_scenario_type(row) for row in rows})
        vocabs["ScenarioType"] = values

    return vocabs


def build_feature_matrix(rows, numeric_columns, categorical_vocabs):
    matrix = []
    for row in rows:
        features = [to_float(row.get(column)) for column in numeric_columns]
        for category_name, values in categorical_vocabs.items():
            if category_name == "ScenarioType":
                row_value = derive_scenario_type(row)
            else:
                row_value = (row.get(category_name) or "Unknown").strip() or "Unknown"
            features.extend(1.0 if row_value == value else 0.0 for value in values)
        matrix.append(features)
    return matrix


def build_feature_names(numeric_columns, categorical_vocabs):
    names = list(numeric_columns)
    for category_name, values in categorical_vocabs.items():
        names.extend(f"{category_name}={value}" for value in values)
    return names


def excluded_runtime_feature_columns(rows):
    if not rows:
        return []
    return sorted(
        column
        for column in rows[0].keys()
        if column.startswith(ANALYSIS_ONLY_PREFIXES) or column == "BestModeScore"
    )


def analyze_dataset(rows):
    label_counts = Counter(int(to_float(row["BestModeLabel"])) for row in rows)
    row_count = len(rows)
    warnings = []

    if row_count:
        most_common_label, most_common_count = label_counts.most_common(1)[0]
        most_common_fraction = most_common_count / row_count
        if most_common_fraction > 0.80:
            warnings.append(
                f"Label {most_common_label} is {most_common_fraction:.1%} of the dataset; add scenario shaping or progress scoring for better class balance."
            )

    per_mode = {}
    for mode_name, mode_label in MODE_COLUMNS:
        per_mode[mode_name] = {
            "label": mode_label,
            "win_count": label_counts.get(mode_label, 0),
            "average_score": average_column(rows, f"Score_{mode_name}"),
            "average_final_distance": average_column(rows, f"FinalDistance_{mode_name}"),
            "average_time_to_attack": average_column(rows, f"TimeToAttackRange_{mode_name}"),
            "average_start_distance": average_column(rows, f"StartDistance_{mode_name}"),
            "average_distance_reduction": average_column(rows, f"DistanceReduction_{mode_name}"),
            "average_invalid_target_count": average_column(rows, f"InvalidTargetCount_{mode_name}"),
            "average_repath_count": average_column(rows, f"RepathCount_{mode_name}"),
            "average_fallback_count": average_column(rows, f"FallbackCount_{mode_name}"),
            "average_path_failure_count": average_column(rows, f"PathFailureCount_{mode_name}"),
        }

    final_distance_columns = [f"FinalDistance_{mode_name}" for mode_name, _ in MODE_COLUMNS]
    if rows and all(column in rows[0] for column in final_distance_columns):
        identical_rows = 0
        for row in rows:
            distances = [to_float(row[column]) for column in final_distance_columns]
            if max(distances) - min(distances) <= 1.0:
                identical_rows += 1
        if identical_rows == row_count:
            warnings.append(
                "All final distances are nearly identical across modes; scenario replay, world ticking, or enemy movement may be broken."
            )
        elif row_count and identical_rows / row_count > 0.95:
            warnings.append(
                f"{identical_rows}/{row_count} rows have nearly identical final distances across modes; inspect scenario replay and ticking."
            )

    return {
        "label_distribution": dict(sorted(label_counts.items())),
        "mode_win_counts": {
            mode_name: label_counts.get(mode_label, 0)
            for mode_name, mode_label in MODE_COLUMNS
        },
        "per_mode": per_mode,
        "warnings": warnings,
        "recommended_dataset_command": RECOMMENDED_DATASET_COMMAND,
    }


def format_optional_float(value):
    return "n/a" if value is None else f"{value:.3f}"


def print_analysis(analysis):
    print(f"Label distribution: {analysis['label_distribution']}")
    print(f"Mode win counts: {analysis['mode_win_counts']}")
    print("Per-mode averages:")
    for mode_name, _ in MODE_COLUMNS:
        stats = analysis["per_mode"][mode_name]
        print(
            f"  {mode_name}: "
            f"score={format_optional_float(stats['average_score'])}, "
            f"final_distance={format_optional_float(stats['average_final_distance'])}, "
            f"time_to_attack={format_optional_float(stats['average_time_to_attack'])}, "
            f"distance_reduction={format_optional_float(stats['average_distance_reduction'])}, "
            f"invalid_targets={format_optional_float(stats['average_invalid_target_count'])}, "
            f"repaths={format_optional_float(stats['average_repath_count'])}, "
            f"path_failures={format_optional_float(stats['average_path_failure_count'])}"
        )

    for warning in analysis["warnings"]:
        print(f"WARNING: {warning}")

    print("Recommended dataset command:")
    print(f"  {analysis['recommended_dataset_command']}")


def train_if_available(rows):
    try:
        from sklearn.metrics import accuracy_score
        from sklearn.metrics import balanced_accuracy_score
        from sklearn.metrics import classification_report
        from sklearn.metrics import confusion_matrix
        from sklearn.model_selection import train_test_split
    except Exception as exc:
        return None, {"sklearn_available": False, "message": str(exc)}

    numeric_columns = available_numeric_feature_columns(rows)
    categorical_vocabs = build_categorical_vocabs(rows)
    feature_columns = build_feature_names(numeric_columns, categorical_vocabs)
    x = build_feature_matrix(rows, numeric_columns, categorical_vocabs)
    y = [int(to_float(row["BestModeLabel"])) for row in rows]
    label_counts = Counter(y)

    if len(set(y)) < 2 or len(rows) < 10:
        return None, {
            "sklearn_available": True,
            "trained": False,
            "message": "Need at least 10 rows and 2 label classes for a useful V1 classifier split.",
        }

    candidates = build_model_candidates()
    if not candidates:
        return None, {
            "sklearn_available": True,
            "trained": False,
            "feature_columns": feature_columns,
            "message": "No supported sklearn classifiers were available.",
        }

    if min(label_counts.values()) < 2:
        candidate = candidates[0]
        model = candidate["model"]
        fit_candidate_model(candidate, model, x, y)
        return model, {
            "sklearn_available": True,
            "trained": True,
            "classifier": candidate["name"],
            "train_rows": len(x),
            "test_rows": 0,
            "evaluation_skipped": True,
            "feature_columns": feature_columns,
            "numeric_feature_columns": numeric_columns,
            "categorical_feature_vocabs": categorical_vocabs,
            "excluded_runtime_feature_columns": excluded_runtime_feature_columns(rows),
            "message": "Trained on all rows because at least one class has fewer than 2 samples, so stratified holdout evaluation is not valid yet.",
        }

    x_train, x_test, y_train, y_test = train_test_split(
        x,
        y,
        test_size=0.2,
        random_state=42,
        stratify=y,
    )

    baseline_metrics = build_baseline_metrics(y_train, y_test)
    model_results = []
    best_entry = None
    best_model = None

    for candidate in candidates:
        model = candidate["model"]
        fit_candidate_model(candidate, model, x_train, y_train)
        predictions = model.predict(x_test)
        metrics = evaluate_predictions(y_test, predictions)
        entry = {
            "classifier": candidate["name"],
            **metrics,
        }
        model_results.append(entry)
        if best_entry is None or entry["balanced_accuracy"] > best_entry["balanced_accuracy"]:
            best_entry = entry
            best_model = model

    best_accuracy = best_entry["accuracy"] if best_entry else 0.0
    best_balanced_accuracy = best_entry["balanced_accuracy"] if best_entry else 0.0
    majority_accuracy = baseline_metrics["majority_class"]["accuracy"]
    predict035_accuracy = baseline_metrics["predict035"]["accuracy"]
    best_baseline_accuracy = max(majority_accuracy, predict035_accuracy)

    return best_model, {
        "sklearn_available": True,
        "trained": True,
        "classifier": best_entry["classifier"] if best_entry else None,
        "evaluation_skipped": False,
        "test_accuracy": best_accuracy,
        "balanced_accuracy": best_balanced_accuracy,
        "baseline_metrics": baseline_metrics,
        "model_results": model_results,
        "best_model": best_entry,
        "model_improvement_over_majority_accuracy": best_accuracy - majority_accuracy,
        "model_improvement_over_predict035_accuracy": best_accuracy - predict035_accuracy,
        "model_improvement_over_best_baseline_accuracy": best_accuracy - best_baseline_accuracy,
        "model_balanced_accuracy_improvement_over_predict035": (
            best_balanced_accuracy - baseline_metrics["predict035"]["balanced_accuracy"]
        ),
        "is_better_than_predict035": best_accuracy > predict035_accuracy,
        "is_balanced_better_than_predict035": (
            best_balanced_accuracy > baseline_metrics["predict035"]["balanced_accuracy"]
        ),
        "train_rows": len(x_train),
        "test_rows": len(x_test),
        "feature_columns": feature_columns,
        "numeric_feature_columns": numeric_columns,
        "categorical_feature_vocabs": categorical_vocabs,
        "excluded_runtime_feature_columns": excluded_runtime_feature_columns(rows),
    }


def build_model_candidates():
    candidates = []

    try:
        from sklearn.ensemble import RandomForestClassifier

        candidates.append(
            {
                "name": "RandomForestClassifier",
                "model": RandomForestClassifier(
                    n_estimators=300,
                    random_state=42,
                    class_weight="balanced",
                    min_samples_leaf=2,
                ),
                "uses_sample_weight": False,
            }
        )
    except Exception:
        pass

    try:
        from sklearn.ensemble import GradientBoostingClassifier

        candidates.append(
            {
                "name": "GradientBoostingClassifier",
                "model": GradientBoostingClassifier(random_state=42),
                "uses_sample_weight": True,
            }
        )
    except Exception:
        pass

    try:
        from sklearn.linear_model import LogisticRegression
        from sklearn.pipeline import make_pipeline
        from sklearn.preprocessing import StandardScaler

        candidates.append(
            {
                "name": "LogisticRegressionStandardized",
                "model": make_pipeline(
                    StandardScaler(),
                    LogisticRegression(
                        max_iter=3000,
                        random_state=42,
                        class_weight="balanced",
                    ),
                ),
                "uses_sample_weight": False,
            }
        )
    except Exception:
        pass

    return candidates


def fit_candidate_model(candidate, model, x, y):
    if candidate.get("uses_sample_weight"):
        try:
            from sklearn.utils.class_weight import compute_sample_weight

            sample_weight = compute_sample_weight(class_weight="balanced", y=y)
            model.fit(x, y, sample_weight=sample_weight)
            return
        except Exception:
            pass

    model.fit(x, y)


def evaluate_predictions(y_true, y_pred):
    from sklearn.metrics import accuracy_score
    from sklearn.metrics import balanced_accuracy_score
    from sklearn.metrics import classification_report
    from sklearn.metrics import confusion_matrix

    target_names = [MODE_NAMES_BY_LABEL[label] for label in MODE_LABELS]
    return {
        "accuracy": accuracy_score(y_true, y_pred),
        "balanced_accuracy": balanced_accuracy_score(y_true, y_pred),
        "confusion_matrix": confusion_matrix(y_true, y_pred, labels=MODE_LABELS).tolist(),
        "classification_report": classification_report(
            y_true,
            y_pred,
            labels=MODE_LABELS,
            target_names=target_names,
            output_dict=True,
            zero_division=0,
        ),
        "classification_report_text": classification_report(
            y_true,
            y_pred,
            labels=MODE_LABELS,
            target_names=target_names,
            zero_division=0,
        ),
    }


def build_baseline_metrics(y_train, y_test):
    majority_label = Counter(y_train).most_common(1)[0][0]
    baselines = {
        "majority_class": {
            "label": majority_label,
            "mode": MODE_NAMES_BY_LABEL.get(majority_label, str(majority_label)),
        },
        "predict035": {
            "label": 1,
            "mode": "Predict035",
        },
    }

    for baseline in baselines.values():
        predictions = [baseline["label"]] * len(y_test)
        baseline.update(evaluate_predictions(y_test, predictions))

    return baselines


def save_model_bundle(model, path: Path, training_result):
    if not model:
        return {"saved": False, "message": "No model was trained."}

    path.parent.mkdir(parents=True, exist_ok=True)
    bundle = {
        "model": model,
        "classifier": training_result.get("classifier"),
        "feature_columns": training_result.get("feature_columns", []),
        "numeric_feature_columns": training_result.get("numeric_feature_columns", []),
        "categorical_feature_vocabs": training_result.get("categorical_feature_vocabs", {}),
        "label_column": "BestModeLabel",
        "mode_labels": dict(MODE_COLUMNS),
    }

    try:
        import joblib

        joblib.dump(bundle, path)
        return {"saved": True, "path": str(path), "format": "joblib"}
    except Exception as joblib_exc:
        try:
            import pickle

            with path.open("wb") as f:
                pickle.dump(bundle, f)
            return {
                "saved": True,
                "path": str(path),
                "format": "pickle",
                "joblib_error": str(joblib_exc),
            }
        except Exception as pickle_exc:
            return {
                "saved": False,
                "path": str(path),
                "message": f"joblib failed: {joblib_exc}; pickle failed: {pickle_exc}",
            }


def serialize_decision_tree(estimator, forest_classes, export_class_labels):
    tree = estimator.tree_
    class_to_export_index = {
        int(class_label): export_index
        for export_index, class_label in enumerate(export_class_labels)
    }
    nodes = []

    for node_index in range(tree.node_count):
        left = int(tree.children_left[node_index])
        right = int(tree.children_right[node_index])
        is_leaf = left < 0 or right < 0 or left == right

        if is_leaf:
            raw_values = [float(value) for value in tree.value[node_index][0]]
            value_sum = sum(raw_values)
            probabilities = [0.0 for _ in export_class_labels]

            if value_sum > 0.0:
                for class_index, class_label in enumerate(forest_classes):
                    export_index = class_to_export_index.get(int(class_label))
                    if export_index is not None:
                        probabilities[export_index] = raw_values[class_index] / value_sum

            nodes.append({"value": probabilities})
        else:
            nodes.append(
                {
                    "feature": int(tree.feature[node_index]),
                    "threshold": float(tree.threshold[node_index]),
                    "left": left,
                    "right": right,
                }
            )

    return {"nodes": nodes}


def export_random_forest_runtime_json(model, feature_columns, path: Path, metrics, source_csv: Path, rows):
    export_class_labels = MODE_LABELS
    forest_classes = [int(class_label) for class_label in model.classes_]
    unsupported_classes = sorted(set(forest_classes) - set(export_class_labels))
    if unsupported_classes:
        return {
            "saved": False,
            "path": str(path),
            "message": f"Model contains unsupported class labels: {unsupported_classes}",
        }

    trees = [
        serialize_decision_tree(estimator, forest_classes, export_class_labels)
        for estimator in model.estimators_
    ]

    payload = {
        "format_version": 1,
        "model_type": "RandomForestClassifier",
        "runtime_feature_set": "EnemyInterceptObservationV1",
        "created_by": "Scripts/train_enemy_intercept_model.py",
        "source_csv": str(source_csv),
        "row_count": len(rows),
        "class_labels": export_class_labels,
        "feature_names": feature_columns,
        "normalization": {"type": "none"},
        "tree_count": len(trees),
        "trees": trees,
        "metrics": metrics,
        "runtime_safe": True,
        "excluded_from_runtime": {
            "categorical_features": ["ScenarioType", "PlayerMovementSource"],
            "analysis_only_prefixes": list(ANALYSIS_ONLY_PREFIXES),
            "note": "Runtime policy uses only FEnemyInterceptObservation-derived numeric features.",
        },
    }

    write_json(path, payload)
    return {
        "saved": True,
        "path": str(path),
        "format": "json_random_forest_v1",
        "tree_count": len(trees),
        "feature_columns": feature_columns,
        "feature_count": len(feature_columns),
        "metrics": metrics,
    }


def train_and_export_runtime_policy(rows, output_path: Path, source_csv: Path):
    try:
        from sklearn.ensemble import RandomForestClassifier
        from sklearn.model_selection import train_test_split
    except Exception as exc:
        return {"saved": False, "sklearn_available": False, "message": str(exc), "path": str(output_path)}

    feature_columns = available_runtime_numeric_feature_columns(rows)
    if not feature_columns:
        return {
            "saved": False,
            "sklearn_available": True,
            "message": "No runtime-safe feature columns are available.",
            "path": str(output_path),
        }

    x = build_feature_matrix(rows, feature_columns, {})
    y = [int(to_float(row["BestModeLabel"])) for row in rows]
    label_counts = Counter(y)
    if len(set(y)) < 2 or len(rows) < 10:
        return {
            "saved": False,
            "sklearn_available": True,
            "feature_columns": feature_columns,
            "message": "Need at least 10 rows and 2 label classes to export a runtime policy.",
            "path": str(output_path),
        }

    runtime_model_kwargs = {
        "n_estimators": 300,
        "random_state": 42,
        "class_weight": "balanced",
        "min_samples_leaf": 2,
    }

    metrics = {
        "evaluation_skipped": True,
        "message": "Holdout evaluation skipped because at least one class has fewer than 2 samples.",
        "train_rows": len(rows),
        "test_rows": 0,
    }

    if min(label_counts.values()) >= 2:
        x_train, x_test, y_train, y_test = train_test_split(
            x,
            y,
            test_size=0.2,
            random_state=42,
            stratify=y,
        )
        eval_model = RandomForestClassifier(**runtime_model_kwargs)
        eval_model.fit(x_train, y_train)
        predictions = eval_model.predict(x_test)
        baseline_metrics = build_baseline_metrics(y_train, y_test)
        prediction_metrics = evaluate_predictions(y_test, predictions)
        metrics = {
            "evaluation_skipped": False,
            "train_rows": len(x_train),
            "test_rows": len(x_test),
            "baseline_metrics": baseline_metrics,
            **prediction_metrics,
            "accuracy_improvement_over_predict035": (
                prediction_metrics["accuracy"] - baseline_metrics["predict035"]["accuracy"]
            ),
            "balanced_accuracy_improvement_over_predict035": (
                prediction_metrics["balanced_accuracy"] - baseline_metrics["predict035"]["balanced_accuracy"]
            ),
            "is_better_than_predict035": (
                prediction_metrics["accuracy"] > baseline_metrics["predict035"]["accuracy"]
            ),
            "is_balanced_better_than_predict035": (
                prediction_metrics["balanced_accuracy"] > baseline_metrics["predict035"]["balanced_accuracy"]
            ),
        }

    final_model = RandomForestClassifier(**runtime_model_kwargs)
    final_model.fit(x, y)
    export_result = export_random_forest_runtime_json(
        final_model,
        feature_columns,
        output_path,
        metrics,
        source_csv,
        rows,
    )
    export_result["sklearn_available"] = True
    return export_result


def print_runtime_export_result(runtime_export_result):
    if not runtime_export_result:
        return

    if not runtime_export_result.get("saved"):
        print(f"Runtime policy export skipped: {runtime_export_result.get('message', 'unknown reason')}")
        return

    print(
        "Runtime policy JSON exported: "
        f"{runtime_export_result['path']} "
        f"({runtime_export_result.get('tree_count', 0)} trees, "
        f"{runtime_export_result.get('feature_count', 0)} features)"
    )

    metrics = runtime_export_result.get("metrics", {})
    if metrics.get("evaluation_skipped"):
        print(f"Runtime policy evaluation skipped: {metrics.get('message', 'no holdout metrics')}")
        return

    print(
        "Runtime policy vs Predict035: "
        f"accuracy_delta={metrics.get('accuracy_improvement_over_predict035', 0.0):.4f}, "
        f"balanced_accuracy_delta={metrics.get('balanced_accuracy_improvement_over_predict035', 0.0):.4f}, "
        f"better_by_accuracy={'yes' if metrics.get('is_better_than_predict035') else 'no'}, "
        f"better_by_balanced_accuracy={'yes' if metrics.get('is_balanced_better_than_predict035') else 'no'}"
    )


def print_training_result(training_result):
    if not training_result.get("trained"):
        print(f"Training skipped: {training_result.get('message', 'unknown reason')}")
        return

    print(f"Feature count: {len(training_result.get('feature_columns', []))}")
    print(f"Best classifier: {training_result.get('classifier')}")

    if training_result.get("evaluation_skipped"):
        print(training_result.get("message", "Evaluation skipped."))
        return

    baseline_metrics = training_result["baseline_metrics"]
    print("Baselines:")
    print(
        "  majority class "
        f"({baseline_metrics['majority_class']['mode']}): "
        f"accuracy={baseline_metrics['majority_class']['accuracy']:.4f}, "
        f"balanced_accuracy={baseline_metrics['majority_class']['balanced_accuracy']:.4f}"
    )
    print(
        "  Predict035: "
        f"accuracy={baseline_metrics['predict035']['accuracy']:.4f}, "
        f"balanced_accuracy={baseline_metrics['predict035']['balanced_accuracy']:.4f}"
    )

    print("Model comparison:")
    for result in training_result["model_results"]:
        print(
            f"  {result['classifier']}: "
            f"accuracy={result['accuracy']:.4f}, "
            f"balanced_accuracy={result['balanced_accuracy']:.4f}"
        )

    print(
        "Best model improvement over Predict035: "
        f"accuracy={training_result['model_improvement_over_predict035_accuracy']:.4f}, "
        f"balanced_accuracy={training_result['model_balanced_accuracy_improvement_over_predict035']:.4f}"
    )
    print(
        "Actually better than always choosing Predict035: "
        f"{'yes' if training_result['is_better_than_predict035'] else 'no'}"
    )
    print("Best model confusion matrix labels [0,1,2,3,4]:")
    for row in training_result["best_model"]["confusion_matrix"]:
        print(f"  {row}")
    print("Best model classification report:")
    print(training_result["best_model"]["classification_report_text"])


def main():
    parser = argparse.ArgumentParser(description="Train a placeholder enemy intercept classifier from V1 CSV data.")
    parser.add_argument(
        "--csv",
        default="Saved/EnemyLearning/InterceptDataset/intercept_samples.csv",
        help="Path to intercept_samples.csv",
    )
    parser.add_argument(
        "--metadata",
        default="Saved/EnemyLearning/Models/enemy_intercept_model_metadata.json",
        help="Metadata JSON output path",
    )
    parser.add_argument(
        "--report",
        default="Saved/EnemyLearning/Reports/enemy_intercept_training_report.json",
        help="Training report JSON output path",
    )
    parser.add_argument(
        "--model-output",
        default="Saved/EnemyLearning/Models/enemy_intercept_model.joblib",
        help="Best sklearn model bundle output path",
    )
    parser.add_argument(
        "--output-dir",
        default="",
        help="Optional directory for model, metadata, and runtime policy outputs.",
    )
    parser.add_argument(
        "--report-dir",
        default="",
        help="Optional directory for the training report output.",
    )
    parser.add_argument(
        "--export-runtime-json",
        default="Saved/EnemyLearning/Models/enemy_intercept_policy_runtime.json",
        help="Runtime-safe RandomForest JSON policy output path. Pass true to use output-dir/default, or false/empty to skip.",
    )
    args = parser.parse_args()

    if args.output_dir:
        output_dir = Path(args.output_dir)
        args.model_output = str(output_dir / "enemy_intercept_model.joblib")
        args.metadata = str(output_dir / "enemy_intercept_model_metadata.json")

    if args.report_dir:
        report_dir = Path(args.report_dir)
        args.report = str(report_dir / "enemy_intercept_training_report.json")

    export_runtime_json = args.export_runtime_json
    if truthy_string(export_runtime_json):
        export_runtime_json = str((Path(args.output_dir) if args.output_dir else Path("Saved/EnemyLearning/Models")) / "enemy_intercept_policy_runtime.json")
    elif falsy_string(export_runtime_json):
        export_runtime_json = ""

    csv_path = Path(args.csv)
    if not csv_path.exists():
        raise SystemExit(f"Dataset does not exist: {csv_path}")

    rows = load_rows(csv_path)
    analysis = analyze_dataset(rows)
    label_counts = Counter(int(to_float(row["BestModeLabel"])) for row in rows)
    print(f"Rows: {len(rows)}")
    print(f"Class distribution: {dict(sorted(label_counts.items()))}")
    print_analysis(analysis)

    model, training_result = train_if_available(rows)
    model_save_result = save_model_bundle(model, Path(args.model_output), training_result)
    training_result["model_save"] = model_save_result
    print_training_result(training_result)

    runtime_export_result = None
    if export_runtime_json:
        runtime_export_result = train_and_export_runtime_policy(rows, Path(export_runtime_json), csv_path)
        print_runtime_export_result(runtime_export_result)

    metadata = {
        "dataset": str(csv_path),
        "row_count": len(rows),
        "feature_columns": training_result.get("feature_columns", []),
        "label_column": "BestModeLabel",
        "class_distribution": dict(sorted(label_counts.items())),
        "analysis": analysis,
        "best_model_path": model_save_result.get("path"),
        "best_model_format": model_save_result.get("format"),
        "runtime_policy_export": runtime_export_result,
        "onnx_exported": False,
        "onnx_message": "ONNX export is deferred for V1E; Unreal runtime has no neural dependency yet.",
    }
    metadata.update(training_result)

    report = {
        "dataset": str(csv_path),
        "row_count": len(rows),
        "class_distribution": dict(sorted(label_counts.items())),
        "analysis": analysis,
        "training": training_result,
        "runtime_policy_export": runtime_export_result,
    }

    write_json(Path(args.metadata), metadata)
    write_json(Path(args.report), report)
    print(f"Wrote metadata: {args.metadata}")
    print(f"Wrote report: {args.report}")


if __name__ == "__main__":
    main()
