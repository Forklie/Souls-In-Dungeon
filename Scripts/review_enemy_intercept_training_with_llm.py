#!/usr/bin/env python3
import argparse
import json
import subprocess
from datetime import datetime, timezone
from pathlib import Path


def load_json(path: Path):
    if not path or not path.exists():
        return None
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def write_json(path: Path, payload: dict):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def extract_first_json_object(text: str):
    start = text.find("{")
    if start < 0:
        return None
    depth = 0
    in_string = False
    escaped = False
    for index in range(start, len(text)):
        char = text[index]
        if in_string:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                in_string = False
            continue
        if char == '"':
            in_string = True
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return text[start : index + 1]
    return None


def compact_payload(eval_report, training_report, promotion_decision):
    return {
        "eval_report": eval_report,
        "training_report_summary": {
            "row_count": (training_report or {}).get("row_count"),
            "class_distribution": (training_report or {}).get("class_distribution"),
            "best_model": ((training_report or {}).get("training") or {}).get("best_model"),
            "baseline_metrics": ((training_report or {}).get("training") or {}).get("baselines"),
            "analysis_warnings": ((training_report or {}).get("analysis") or {}).get("warnings"),
        },
        "promotion_decision": promotion_decision,
    }


def call_lmstudio(base_url: str, model: str, prompt: str, timeout_seconds: float):
    endpoint = base_url.rstrip("/") + "/chat/completions"
    body = {
        "model": model,
        "messages": [
            {
                "role": "system",
                "content": (
                    "You are a cautious reviewer for an Unreal enemy intercept training pipeline. "
                    "You only fact-check and summarize metrics. You never label training data, never decide promotion, "
                    "and never suggest code changes as an automatic action."
                ),
            },
            {"role": "user", "content": prompt},
        ],
        "temperature": 0.2,
        "max_tokens": 900,
    }
    proc = subprocess.run(
        [
            "/usr/bin/curl",
            "-sS",
            "--max-time",
            str(timeout_seconds),
            "-H",
            "Content-Type: application/json",
            "-X",
            "POST",
            endpoint,
            "--data-binary",
            "@-",
        ],
        input=json.dumps(body),
        text=True,
        capture_output=True,
    )
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or f"curl exited with {proc.returncode}")
    response = json.loads(proc.stdout)
    return response["choices"][0]["message"]["content"]


def main():
    parser = argparse.ArgumentParser(description="Use LM Studio to fact-check enemy intercept training reports without deciding promotion.")
    parser.add_argument("--eval-report", required=True)
    parser.add_argument("--training-report", required=True)
    parser.add_argument("--promotion-decision", required=True)
    parser.add_argument("--lmstudio-url", default="http://localhost:1234/v1")
    parser.add_argument("--model", default="google/gemma-3-270m")
    parser.add_argument("--timeout-seconds", type=float, default=8.0)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    output = Path(args.output)
    eval_report = load_json(Path(args.eval_report))
    training_report = load_json(Path(args.training_report))
    promotion_decision = load_json(Path(args.promotion_decision))

    if eval_report is None:
        payload = {
            "skipped": True,
            "reason": f"eval report missing or unreadable: {args.eval_report}",
            "timestamp": datetime.now(timezone.utc).isoformat(),
        }
        write_json(output, payload)
        print(payload["reason"])
        return

    prompt = (
        "Review the following enemy intercept policy training and evaluation metrics.\n"
        "Rules:\n"
        "- Do not invent missing metrics. If a metric is missing, say it is missing.\n"
        "- Do not recommend promotion unless the objective promotion gate passed.\n"
        "- The LLM review is advisory only; objective gates decide promotion.\n"
        "- A* remains the path planner. The model only chooses EEnemyInterceptMode 0..4.\n"
        "- The model must not output coordinates or raw steering.\n\n"
        "Check:\n"
        "1. Are metrics internally consistent?\n"
        "2. Is learned policy actually better than baselines?\n"
        "3. Is there a mismatch between accuracy and score gap?\n"
        "4. Is class imbalance a problem?\n"
        "5. Are safety metrics risky?\n"
        "6. Should this run be trusted for gameplay testing?\n"
        "7. What data improvements should be tried next?\n\n"
        "Return only valid JSON with keys: summary, risks, metric_consistency_notes, "
        "recommended_next_data_changes, promotion_comment.\n\n"
        f"Metrics JSON:\n{json.dumps(compact_payload(eval_report, training_report, promotion_decision), indent=2, sort_keys=True)}"
    )

    try:
        content = call_lmstudio(args.lmstudio_url, args.model, prompt, args.timeout_seconds)
        extracted = extract_first_json_object(content)
        if not extracted:
            raise ValueError("LM Studio response did not contain a JSON object")
        review = json.loads(extracted)
        review["skipped"] = False
        review["timestamp"] = datetime.now(timezone.utc).isoformat()
        review["promotion_comment"] = review.get("promotion_comment") or "LLM review only; objective gate decides promotion"
        write_json(output, review)
        print(f"Wrote LLM review: {output}")
    except Exception as exc:
        payload = {
            "skipped": True,
            "reason": f"LM Studio review skipped: {exc}",
            "timestamp": datetime.now(timezone.utc).isoformat(),
            "promotion_comment": "LLM review only; objective gate decides promotion",
        }
        write_json(output, payload)
        print(payload["reason"])


if __name__ == "__main__":
    main()
