# Performance report

Measured with `tools/perf/measure.py`, which reads `utime + stime` and context switches from `/proc/<pid>` and memory from `smaps_rollup`.
CPU is the percentage of one core.

## Test machine

- AMD Ryzen 9 9900X (24 threads), Intel Arc B580 (xe driver), two NVMe drives, Wi-Fi, no battery
- Fedora 44, kernel 7.2, systemd 259, Plasma 6.8.2, GNOME Shell 50.6
- `readoutd` 0.1.0, RelWithDebInfo build with LTO

## readoutd

| Scenario | CPU | Wakeups/s | PSS | Heap growth |
|---|---|---|---|---|
| No subscribers | 0 (exits after 30 s) | 0 | 0 | - |
| Panel tier, all 8 modules, 2 s | 0.02% | 1.3 | 1.15 MiB | 0 |
| Detail tier, 6 modules open at once, 2 s | 0.23% | - | - | 0 |
| GNOME, default three items, 2 s | 0.017% | - | - | - |
| GNOME, six items with charts | 0.033% | - | - | - |
| GNOME, CPU popup open | 0.18% | - | - | - |

RSS is about 5.5 to 5.9 MiB, of which about 4.2 MiB is shared libraries (libstdc++, libc and libsystemd) mapped by many other processes too.
PSS is the fairer measure of what the daemon adds.
Building with a static libstdc++ (`libstdc++-static` installed) moves the C++ runtime out of the shared mappings.

In the detail tier, most context switches come from kernel drivers that sleep while a sensor is read (I2C memory temperature, the Wi-Fi card, NVMe), not from the daemon itself.
The detail tier holds one open descriptor per process for the process list, about 900 on this machine.

## Plasma widget

| Scenario | CPU | Wakeups/s | Heap growth |
|---|---|---|---|
| One CPU item, offscreen `plasmoidviewer`, 2 s, 60 s run | < 0.01% | 0.9 | 0 |

The offscreen run measures the data path and QML bindings, but not drawing, because an offscreen window never renders frames.

Drawing was measured with `tools/plasma-preview` in a headless `kwin_wayland --virtual` session, which renders in software, so these numbers are upper bounds.
Both scenes update every second, twice as often as the default.

| Scenario | CPU | Wakeups/s | Heap growth over 30 s |
|---|---|---|---|
| Panel scene: every style of every module, 53 widgets | 0.57% | 8 | 0 |
| Ten popups open at once (every module plus a combined item) | 4.4% | 42 | 0.3 MiB, client-side sensor and GPU history filling up |

One panel widget costs about 0.01% of a core, and one open popup about 0.4% at a 1 second interval.

## GNOME Shell extension

| Scenario | gnome-shell CPU |
|---|---|
| Extension disabled | 0.18% |
| Default three items | 0.42% |
| Six items with charts, clock showing seconds | 0.63% |
| Same, CPU popup open | 0.82% |

Measured in a nested GNOME Shell 50.6 session, 60 s per case, 2 s interval.
gnome-shell memory grew 0.2 MB with the default three items, and 80 enable and disable cycles left no Readout actors, no subscribers and no upward memory trend.

## Robustness

- Unit and integration tests pass in normal and AddressSanitizer plus UndefinedBehaviorSanitizer builds.
- The parser fuzz target ran 5 minutes with 4 workers (about 13 million inputs) without a crash.

## Not measured yet

- Frame cost inside a real `plasmashell` with GPU rendering (QML profiler, `QSG_RENDER_TIMING`).
- A laptop with a battery and a hybrid GPU, to confirm the discrete GPU stays suspended in daily use.
