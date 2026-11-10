#!/usr/bin/env python3
"""Fails on any qmllint warning in the Plasma widget, except unqualified i18n calls.

KDE's i18n functions are injected into the QML context at run time, so qmllint cannot see them.
Every other warning is a real problem.

Usage: tools/lint-qml.py BUILD_DIR
"""
import json
import pathlib
import subprocess
import sys

TARGET = "io.github.aakash1122.readout"
I18N_PREFIXES = ("i18n", "xi18n")


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    build = pathlib.Path(sys.argv[1])
    subprocess.run(["cmake", "--build", str(build), "--target", f"{TARGET}_qmllint_json"],
                   check=False, capture_output=True)
    report = next(build.rglob(f"{TARGET}_qmllint.json"), None)
    if report is None:
        print("qmllint report not found; is READOUT_BUILD_PLASMA on?", file=sys.stderr)
        return 2
    failures = 0
    for entry in json.loads(report.read_text())["files"]:
        path = pathlib.Path(entry["filename"])
        lines = path.read_text().splitlines()
        for warning in entry.get("warnings", []):
            if warning.get("type") == "info":
                continue
            line, column = warning.get("line", 0), warning.get("column", 0)
            source = lines[line - 1][column - 1:] if 0 < line <= len(lines) else ""
            if warning.get("id") == "unqualified" and source.startswith(I18N_PREFIXES):
                continue
            failures += 1
            print(f"{path.name}:{line}:{column}: {warning.get('id')}: {warning['message']}")
    print(f"qmllint: {failures} warning(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
