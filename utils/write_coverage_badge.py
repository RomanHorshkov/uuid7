#!/usr/bin/env python3
"""Read gcovr's coverage-summary.json and write a shields.io endpoint-badge JSON.

Usage: write_coverage_badge.py <coverage-summary.json> <out-badge.json>
"""
import json
import os
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} <coverage-summary.json> <out-badge.json>", file=sys.stderr)
        return 2

    summary_path, out_path = sys.argv[1], sys.argv[2]
    with open(summary_path) as fh:
        pct = json.load(fh)["line_percent"]

    if pct >= 90:
        color = "brightgreen"
    elif pct >= 75:
        color = "green"
    elif pct >= 50:
        color = "yellow"
    else:
        color = "red"

    badge = {"schemaVersion": 1, "label": "coverage", "message": f"{pct:.1f}%", "color": color}

    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    with open(out_path, "w") as fh:
        json.dump(badge, fh)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
