#!/usr/bin/env python3
"""Release-gate checks for kiku firmware.

This script intentionally performs no programming or hardware access. It is
safe to run while a product is disconnected.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
REPO = ROOT.parent

# The application task is provisioned with 32 KiB. Keep a meaningful margin
# for interrupt entry, library call depth and future compiler changes. This is
# a per-function static-frame gate, not a replacement for on-target high-water
# mark testing.
MAX_FIRST_PARTY_STATIC_FRAME = 24 * 1024


def run(*args: str) -> None:
    print("+", " ".join(args), flush=True)
    subprocess.run(args, cwd=REPO, check=True)


def first_party_stack_gate() -> None:
    obj = ROOT / "build" / "obj"
    worst: list[tuple[int, str, str]] = []
    for su in obj.rglob("*.su"):
        if "vendor__" in su.name or "ThirdParty__" in su.name:
            continue
        for line in su.read_text(encoding="utf-8", errors="replace").splitlines():
            match = re.search(r"\t(\d+)\t(\S+)$", line)
            if match is None:
                continue
            size = int(match.group(1))
            qualifier = match.group(2)
            symbol = line.split("\t", 1)[0]
            worst.append((size, symbol, qualifier))
            if qualifier != "static":
                raise SystemExit(f"Unbounded/dynamic stack usage: {line}")
            if size > MAX_FIRST_PARTY_STATIC_FRAME:
                raise SystemExit(
                    f"Static stack frame {size} exceeds release gate "
                    f"{MAX_FIRST_PARTY_STATIC_FRAME}: {symbol}"
                )
    if not worst:
        raise SystemExit("No first-party .su stack-usage files found after build")
    size, symbol, _ = max(worst)
    print(f"Stack gate: PASS (largest first-party static frame {size} bytes: {symbol})")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--require-clean",
        action="store_true",
        help="fail if tracked or untracked working-tree changes are present",
    )
    args = parser.parse_args()

    run(sys.executable, str(ROOT / "tools" / "test_host.py"))
    run(sys.executable, str(ROOT / "tools" / "build.py"), "--clean")
    first_party_stack_gate()
    run(sys.executable, str(ROOT / "tools" / "build.py"), "--clean", "--debug")
    run(sys.executable, str(ROOT / "tools" / "build.py"), "--clean")
    first_party_stack_gate()
    run("git", "diff", "--check")

    if args.require_clean:
        status = subprocess.check_output(
            ["git", "status", "--porcelain"], cwd=REPO, text=True
        ).strip()
        if status:
            print(status)
            raise SystemExit("Release requires a clean Git working tree")

    print("kiku release checks: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
