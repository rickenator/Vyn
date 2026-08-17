#!/usr/bin/env python3
"""
Run the Vyb milestone gate.

The gate intentionally aggregates multiple stable suites through run_tests.py
and fails if the total number of passing tests drops below the milestone floor.
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path


MILESTONE_MINIMUM = 156


@dataclass(frozen=True)
class Suite:
    name: str
    path: str
    execute_jit: bool = True
    pattern: str = "*.vyb"


MILESTONE_SUITES = [
    Suite("new_features", "test/new_features"),
    Suite("agents", "test/agents"),
    Suite("modules", "test/modules"),
    Suite("ffi", "test/ffi"),
    Suite("ownership", "test/ownership"),
    Suite("error_trap_phase2", "test/error_trap/phase2"),
    Suite("trap_propagation", "test/trap", pattern="propagation_no_trap.vyb"),
    Suite("trap_untrapped_main", "test/trap", pattern="propagation_to_main.vyb"),
    Suite("trap_defer_fail", "test/trap", pattern="defer_runs_on_fail.vyb"),
    Suite("trap_non_failable_reject", "test/trap", pattern="non_failable_caller_rejected.vyb"),
    Suite("basic", "test/basic"),
    Suite("string", "test/string"),
    Suite("math", "test/math"),
    Suite("introspection", "test/introspection"),
    Suite("types", "test/types"),
    Suite("range_for", "test/range_for"),
    Suite("vec_for", "test/vec_for"),
    Suite("stdlib", "test/stdlib"),
]


def repo_root() -> Path:
    current = Path(__file__).resolve().parent
    while current != current.parent:
        if (current / "CMakeLists.txt").exists() and (current / "src" / "tests.cpp").exists():
            return current
        current = current.parent
    raise RuntimeError("Could not find Vyb repository root")


def run_suite(root: Path, suite: Suite, vyb: str, verbose: bool) -> tuple[int, int]:
    with tempfile.TemporaryDirectory(prefix=f"vyb-{suite.name}-") as temp_dir:
        json_path = Path(temp_dir) / "results.json"
        cmd = [
            sys.executable,
            str(root / "test" / "run_tests.py"),
            "--vyb",
            vyb,
            "--test-dir",
            str(root / suite.path),
            "--json",
            str(json_path),
            "--pattern",
            suite.pattern,
        ]
        if suite.execute_jit:
            cmd.append("--execute-jit")

        result = subprocess.run(cmd, cwd=root, capture_output=True, text=True)
        if verbose or result.returncode != 0:
            print(result.stdout, end="")
            print(result.stderr, end="", file=sys.stderr)

        results = []
        if json_path.exists():
            with json_path.open("r", encoding="utf-8") as handle:
                results = json.load(handle)

        ran = len(results)
        passed = sum(1 for item in results if item.get("result", {}).get("success"))
        failed = ran - passed
        print(f"{suite.name}: {passed}/{ran} passed")

        if result.returncode != 0 or failed:
            raise RuntimeError(f"{suite.name} failed {failed} test(s)")

        return ran, passed


def main() -> int:
    root = repo_root()
    parser = argparse.ArgumentParser(description="Run the Vyb milestone test gate")
    parser.add_argument("--vyb", default=str(root / "build" / "vyb"), help="Path to the Vyb executable")
    parser.add_argument("--minimum", type=int, default=MILESTONE_MINIMUM, help="Minimum passing tests required")
    parser.add_argument("--verbose", "-v", action="store_true", help="Show full per-test output")
    args = parser.parse_args()

    if not os.path.isfile(args.vyb):
        print(f"Error: Cannot find Vyb executable at {args.vyb}", file=sys.stderr)
        return 1

    total_ran = 0
    total_passed = 0
    try:
        for suite in MILESTONE_SUITES:
            ran, passed = run_suite(root, suite, args.vyb, args.verbose)
            total_ran += ran
            total_passed += passed
    except RuntimeError as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1

    print(f"\nMilestone total: {total_passed}/{total_ran} passed")
    print(f"Milestone minimum: {args.minimum} passing tests")

    if total_ran < args.minimum or total_passed < args.minimum:
        print("Error: milestone test count is below the required minimum", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
