#!/usr/bin/env python3
"""End-to-end test of readoutd on a private session bus.

Run under dbus-run-session (ctest does this):
    dbus-run-session -- python3 integration_test.py --daemon build/daemon/readoutd --ctl build/tools/readoutctl/readoutctl
"""

import argparse
import json
import os
import signal
import subprocess
import sys
import time

BUS_NAME = "io.github.aakash1122.Readout1"
OBJECT_PATH = "/io/github/aakash1122/Readout1"

failures = []


def check(condition, message):
    if condition:
        print(f"  ok   {message}")
    else:
        print(f"  FAIL {message}")
        failures.append(message)


def run(command, timeout=10):
    return subprocess.run(command, capture_output=True, text=True, timeout=timeout)


def ctl(arguments, timeout=10):
    result = run([options.ctl] + arguments, timeout)
    return result.returncode, result.stdout, result.stderr


def subscriber_count():
    result = run(["busctl", "--user", "get-property", BUS_NAME, OBJECT_PATH, BUS_NAME, "SubscriberCount"])
    return int(result.stdout.split()[1]) if result.returncode == 0 else -1


def name_has_owner():
    result = run(["busctl", "--user", "status", BUS_NAME])
    return result.returncode == 0


def wait_until(predicate, seconds):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if predicate():
            return True
        time.sleep(0.1)
    return predicate()


def test_direct_start():
    print("direct start")
    daemon = subprocess.Popen([options.daemon, "--idle-timeout=2"], stderr=subprocess.PIPE, text=True)
    check(wait_until(name_has_owner, 5), "daemon owns its bus name")

    code, out, _ = ctl(["info"])
    info = json.loads(out) if code == 0 else {}
    check(code == 0, "GetInfo succeeds")
    check(info.get("modules") == ["cpu", "memory", "network", "disk", "gpu", "sensors", "battery", "bluetooth"],
          "GetInfo lists all modules")
    check(info.get("cpu", {}).get("logicalCores", 0) >= 1, "cpu info has logical cores")
    check(info.get("memory", {}).get("total", 0) > 0, "memory info has total")

    code, out, _ = ctl(["once", "cpu", "memory", "network", "disk", "gpu", "sensors", "battery", "bluetooth"])
    update = json.loads(out) if code == 0 else {}
    modules = update.get("modules", {})
    check(code == 0 and update.get("timestamp", 0) > 0, "panel update arrives with a timestamp")
    check(set(modules) == {"cpu", "memory", "network", "disk", "gpu", "sensors", "battery", "bluetooth"},
          "panel update contains every subscribed module")
    check(0 <= modules.get("cpu", {}).get("usage", -1) <= 100, "cpu usage is a percentage")
    check("cores" not in modules.get("cpu", {}), "panel tier has no per-core data")
    check(modules.get("memory", {}).get("used", 0) > 0, "memory used is reported")

    code, out, _ = ctl(["once", "cpu:detail", "memory:detail"])
    detail = json.loads(out).get("modules", {}) if code == 0 else {}
    check(len(detail.get("cpu", {}).get("cores", [])) == info.get("cpu", {}).get("logicalCores"),
          "detail tier has one value per logical CPU")
    check(len(detail.get("cpu", {}).get("processes", [])) > 0, "detail tier lists top processes")
    check(len(detail.get("memory", {}).get("processes", [])) > 0, "memory detail lists top processes")

    code, out, _ = ctl(["history", "cpu"])
    history = json.loads(out) if code == 0 else {}
    check(code == 0 and isinstance(history.get("usage"), list), "GetHistory returns the usage series")
    check(len(history.get("usage", [])) >= 1, "history has points after sampling")

    code, _, err = ctl(["once", "nosuchmodule"])
    check(code != 0 and "Unknown module" in err, "unknown module is rejected")
    code, _, err = ctl(["once", "cpu:fast"])
    check(code != 0 and "Unknown tier" in err, "unknown tier is rejected")

    # A client that dies without unsubscribing is dropped.
    watcher = subprocess.Popen([options.ctl, "watch", "cpu", "--interval=500"], stdout=subprocess.DEVNULL)
    check(wait_until(lambda: subscriber_count() == 1, 5), "watching client is counted as a subscriber")
    watcher.send_signal(signal.SIGKILL)
    watcher.wait()
    check(wait_until(lambda: subscriber_count() == 0, 5), "killed client is removed")

    # Interval is respected (500 ms clamp) and updates keep coming.
    watcher = subprocess.Popen([options.ctl, "watch", "cpu", "--interval=100"], stdout=subprocess.PIPE, text=True)
    time.sleep(2.6)
    watcher.terminate()
    lines = watcher.stdout.read().strip().splitlines()
    timestamps = [json.loads(line)["timestamp"] for line in lines]
    gaps = [(b - a) / 1000 for a, b in zip(timestamps, timestamps[1:])]
    check(3 <= len(lines) <= 6, f"interval below 500 ms is clamped ({len(lines)} updates in 2.6 s)")
    check(all(gap >= 240 for gap in gaps[:1]) and all(gap >= 400 for gap in gaps[1:]),
          f"updates are ~500 ms apart (gaps {[round(gap) for gap in gaps]} ms)")

    # Idle exit once nobody is subscribed.
    started = time.monotonic()
    exited = wait_until(lambda: daemon.poll() is not None, 6)
    check(exited, "daemon exits when idle")
    if exited:
        check(time.monotonic() - started < 5, "idle exit happens within the timeout")
        check(daemon.returncode == 0, "idle exit status is 0")
    else:
        daemon.kill()
    stderr = daemon.stderr.read() if daemon.stderr else ""
    check("<4>" not in stderr or "unavailable" in stderr, "no unexpected warnings in the daemon log")


def test_activation():
    """ctest points XDG_DATA_DIRS at a generated service file whose Exec is the build's readoutd."""
    print("D-Bus activation")
    if options.no_activation:
        print("  skip (no test service directory)")
        return
    check(not name_has_owner(), "service is not running before the call")
    code, out, _ = ctl(["info"])
    check(code == 0 and "modules" in out, "calling the service activates readoutd")
    check(wait_until(lambda: not name_has_owner(), 6), "activated daemon exits when idle")


def main():
    global options
    parser = argparse.ArgumentParser()
    parser.add_argument("--daemon", required=True)
    parser.add_argument("--ctl", required=True)
    parser.add_argument("--no-activation", action="store_true")
    options = parser.parse_args()
    if "DBUS_SESSION_BUS_ADDRESS" not in os.environ:
        print("run under dbus-run-session", file=sys.stderr)
        return 2
    test_direct_start()
    test_activation()
    if failures:
        print(f"\n{len(failures)} check(s) failed")
        return 1
    print("\nall checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
