#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import subprocess
import sys
import time
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
SCRIPTS_DIR = PROJECT_ROOT / "Scripts"
MCP_REFRESH = SCRIPTS_DIR / "mcp-refresh.sh"
FULL_REFRESH = SCRIPTS_DIR / "full-ubt-refresh.sh"

STRUCTURAL_SUFFIXES = {
    ".h",
    ".hpp",
    ".inl",
    ".cs",
    ".uplugin",
    ".uproject",
}

CPP_SUFFIXES = {
    ".cpp",
    ".cc",
    ".cxx",
    ".c",
}

SKIP_DIR_NAMES = {
    ".git",
    ".codex_tmp",
    "Binaries",
    "DerivedDataCache",
    "Intermediate",
    "Saved",
}


def iter_watch_roots() -> list[Path]:
    roots = [
        PROJECT_ROOT / "Source",
        PROJECT_ROOT / "Config",
        PROJECT_ROOT / "Soul_and_dungeon.uproject",
    ]

    plugins = PROJECT_ROOT / "Plugins"
    if plugins.exists():
        for plugin in plugins.iterdir():
            if not plugin.is_dir():
                continue
            plugin_source = plugin / "Source"
            if plugin_source.exists():
                roots.append(plugin_source)
            roots.extend(plugin.glob("*.uplugin"))

    return [root for root in roots if root.exists()]


def should_skip(path: Path) -> bool:
    parts = set(path.relative_to(PROJECT_ROOT).parts)
    if parts & SKIP_DIR_NAMES:
        return True
    return path.name.startswith(".DS_Store")


def iter_files(root: Path):
    if root.is_file():
        if not should_skip(root):
            yield root
        return

    for dirpath, dirnames, filenames in os.walk(root):
        current = Path(dirpath)
        dirnames[:] = [name for name in dirnames if name not in SKIP_DIR_NAMES]
        for filename in filenames:
            path = current / filename
            if should_skip(path):
                continue
            if path.suffix in STRUCTURAL_SUFFIXES or path.suffix in CPP_SUFFIXES:
                yield path


def snapshot() -> dict[Path, tuple[int, int]]:
    state: dict[Path, tuple[int, int]] = {}
    for root in iter_watch_roots():
        for path in iter_files(root):
            try:
                stat = path.stat()
            except FileNotFoundError:
                continue
            state[path] = (stat.st_mtime_ns, stat.st_size)
    return state


def changed_files(old: dict[Path, tuple[int, int]], new: dict[Path, tuple[int, int]]) -> set[Path]:
    changed: set[Path] = set()
    for path, value in new.items():
        if old.get(path) != value:
            changed.add(path)
    for path in old:
        if path not in new:
            changed.add(path)
    return changed


def classify(paths: set[Path]) -> str | None:
    if not paths:
        return None
    if any(path.suffix in STRUCTURAL_SUFFIXES for path in paths):
        return "full"
    if any(path.suffix in CPP_SUFFIXES for path in paths):
        return "live"
    return None


def format_paths(paths: set[Path]) -> str:
    relative = sorted(str(path.relative_to(PROJECT_ROOT)) for path in paths)
    if len(relative) <= 8:
        return ", ".join(relative)
    return ", ".join(relative[:8]) + f", ... ({len(relative)} files)"


def run_command(command: list[str], dry_run: bool) -> int:
    print(f"$ {' '.join(command)}", flush=True)
    if dry_run:
        return 0
    return subprocess.call(command, cwd=PROJECT_ROOT)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Watch Souls-In-Dungeon code changes and refresh Unreal through Monolith MCP."
    )
    parser.add_argument("--interval", type=float, default=1.0, help="Polling interval in seconds.")
    parser.add_argument("--debounce", type=float, default=2.0, help="Delay after the last change before refreshing.")
    parser.add_argument("--dry-run", action="store_true", help="Print refresh commands without running them.")
    parser.add_argument(
        "--force-close",
        action="store_true",
        help="Pass --force-close to full-ubt-refresh.sh for structural changes.",
    )
    parser.add_argument(
        "--no-reopen",
        action="store_true",
        help="Pass --no-reopen to full-ubt-refresh.sh for structural changes.",
    )
    args = parser.parse_args()

    if not MCP_REFRESH.exists() or not FULL_REFRESH.exists():
        print("Missing refresh scripts. Expected Scripts/mcp-refresh.sh and Scripts/full-ubt-refresh.sh.", file=sys.stderr)
        return 1

    print("Watching Unreal code changes. Press Ctrl-C to stop.", flush=True)
    print("Live refresh: .cpp changes -> Monolith Live Coding.", flush=True)
    print("Full refresh: headers, Build.cs, Target.cs, uplugin, uproject -> close/build/reopen.", flush=True)

    previous = snapshot()
    pending: set[Path] = set()
    last_change = 0.0

    try:
        while True:
            time.sleep(args.interval)
            current = snapshot()
            delta = changed_files(previous, current)
            previous = current

            if delta:
                pending.update(delta)
                last_change = time.monotonic()
                print(f"Detected change: {format_paths(delta)}", flush=True)
                continue

            if pending and (time.monotonic() - last_change) >= args.debounce:
                mode = classify(pending)
                changed = pending
                pending = set()

                if mode == "live":
                    print(f"Refreshing via MCP Live Coding for: {format_paths(changed)}", flush=True)
                    run_command([str(MCP_REFRESH), "compile"], args.dry_run)
                elif mode == "full":
                    command = [str(FULL_REFRESH)]
                    if args.force_close:
                        command.append("--force-close")
                    if args.no_reopen:
                        command.append("--no-reopen")
                    print(f"Running full editor refresh for: {format_paths(changed)}", flush=True)
                    run_command(command, args.dry_run)
                    previous = snapshot()
                else:
                    print(f"No refresh action for: {format_paths(changed)}", flush=True)
    except KeyboardInterrupt:
        print("\nStopped watcher.", flush=True)
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
