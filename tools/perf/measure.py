#!/usr/bin/env python3
"""Measure the resource use of a running process against Readout's budgets.

Usage:
    measure.py --pid PID [--seconds 60] [--label NAME] [--json]
    measure.py --name readoutd ...

Reports, over the measurement window:
    cpu_percent      CPU time / wall time, as percent of one core
    wakeups_per_sec  voluntary + involuntary context switches per second (wakeup proxy)
    rss_kib, pss_kib from /proc/PID/smaps_rollup at the end of the window
    heap_growth_kib  growth of the [heap] mapping over the window (should be 0 in steady state)
"""

import argparse
import json
import os
import sys
import time

CLOCK_TICKS = os.sysconf("SC_CLK_TCK")


def read_cpu_ticks(pid):
    with open(f"/proc/{pid}/stat") as handle:
        text = handle.read()
    fields = text[text.rfind(")") + 2 :].split()
    # fields[11] = utime, fields[12] = stime (relative to the state field)
    return int(fields[11]) + int(fields[12])


def read_context_switches(pid):
    total = 0
    with open(f"/proc/{pid}/status") as handle:
        for line in handle:
            if line.startswith(("voluntary_ctxt_switches", "nonvoluntary_ctxt_switches")):
                total += int(line.split()[1])
    return total


def read_memory(pid):
    values = {}
    with open(f"/proc/{pid}/smaps_rollup") as handle:
        for line in handle:
            parts = line.split()
            if len(parts) >= 2 and parts[0] in ("Rss:", "Pss:"):
                values[parts[0][:-1].lower()] = int(parts[1])
    return values


def read_heap_kib(pid):
    with open(f"/proc/{pid}/maps") as handle:
        for line in handle:
            if line.rstrip().endswith("[heap]"):
                start, end = line.split()[0].split("-")
                return (int(end, 16) - int(start, 16)) // 1024
    return 0


def find_pid(name):
    for entry in os.listdir("/proc"):
        if not entry.isdigit():
            continue
        try:
            with open(f"/proc/{entry}/comm") as handle:
                if handle.read().strip() == name:
                    return int(entry)
        except OSError:
            continue
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    target = parser.add_mutually_exclusive_group(required=True)
    target.add_argument("--pid", type=int)
    target.add_argument("--name")
    parser.add_argument("--seconds", type=float, default=60.0)
    parser.add_argument("--label", default="")
    parser.add_argument("--json", action="store_true")
    arguments = parser.parse_args()

    pid = arguments.pid if arguments.pid else find_pid(arguments.name)
    if pid is None:
        print(f"process '{arguments.name}' not found", file=sys.stderr)
        return 1

    start_wall = time.monotonic()
    start_ticks = read_cpu_ticks(pid)
    start_switches = read_context_switches(pid)
    start_heap = read_heap_kib(pid)
    time.sleep(arguments.seconds)
    wall = time.monotonic() - start_wall
    ticks = read_cpu_ticks(pid) - start_ticks
    switches = read_context_switches(pid) - start_switches
    memory = read_memory(pid)
    heap_growth = read_heap_kib(pid) - start_heap

    result = {
        "label": arguments.label,
        "pid": pid,
        "seconds": round(wall, 1),
        "cpu_percent": round(ticks / CLOCK_TICKS / wall * 100.0, 4),
        "wakeups_per_sec": round(switches / wall, 2),
        "rss_kib": memory.get("rss", 0),
        "pss_kib": memory.get("pss", 0),
        "heap_growth_kib": heap_growth,
    }
    if arguments.json:
        print(json.dumps(result))
    else:
        for key, value in result.items():
            print(f"{key:>16}: {value}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
