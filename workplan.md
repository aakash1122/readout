# Readout - Work Plan (v2)

A system monitor for the Linux desktop panel, modeled on [exelban/stats](https://github.com/exelban/stats) for macOS.
It targets KDE Plasma 6 and GNOME Shell, and it should look native on both.
This plan replaces the first plan and all code written for it.

---

## 1. Goal

Each module (CPU, GPU, RAM, Disk, Network, Battery, Sensors, Bluetooth, Clock) appears as a small live item in the panel.
Clicking it opens a detailed popup, the same way Stats works on macOS.
Users choose the modules, the widget style for each one, and the update interval.

The macOS source in `reference/stats-macos/` is a feature and UX reference only.
None of its code can be reused.

### Hard rules (from the owner)

1. Optimized and high performance.
2. Very low resource use.
3. Looks its best on KDE and on GNOME.

Rules 1 and 2 become the measurable budgets in section 3.
Every phase has to pass them before it is considered done.
Rule 3 means each frontend uses its own desktop's toolkit, theme, and design guidelines, with no shared look-alike UI.

---

## 2. Why the previous work is scrapped

The idea from the first plan still holds: one shared backend, thin native frontends, and Stats as the feature reference.
The execution broke the hard rules and has to be rebuilt:

| Problem in the old code | Why it matters |
|---|---|
| `ReaderBase` starts one `std::thread` per module, each sleeping in its own loop. | That means ten threads and ten separate wakeup sources, which wastes CPU and power. |
| The KDE plugin linked the core directly into `plasmashell`. | A parser bug could crash the whole desktop shell, and each panel instance sampled the system separately. |
| The KDE charts use QML `Canvas`. | Canvas draws in software on the CPU and repaints the whole surface on every update. It is the slowest way to draw a chart in Qt Quick. |
| `stats-daemon` is a stub that only prints a version. | The GNOME frontend had no real data source. |
| The GNOME extension has no D-Bus client. | The GNOME UI was never connected to any data. |
| Sampling was always on, whether or not anything was visible. | Process scans and detail data cost CPU even with every popup closed. |
| The plan had no measurement method for its budgets. | "< 1% CPU" cannot be enforced without a harness. |

Done: `core/`, `daemon/`, `frontends/`, the old CI, and the old CMake files were deleted.
Kept: `reference/stats-macos/`, `LICENSE`, `.gitmodules`, `.gitignore`, `.clang-format`, and `.clang-tidy`.
The kept config files and `README.md` still describe the old design and are reviewed in Phase 0.

---

## 3. Resource and performance budgets

These numbers are acceptance criteria.
The perf harness from Phase 0 checks them in CI where it can, and on real hardware before every release.

| Scenario | Budget |
|---|---|
| Daemon, panel only (CPU, RAM, Network at 1 s) | < 0.05% of one core averaged over 10 min, at most 1 wakeup per interval |
| Daemon, one popup open (per-core data, top processes at 2 s) | < 0.5% of one core |
| Daemon memory | PSS < 2 MiB in steady state. RSS is about 5.5 MiB, nearly all of it shared libraries (libstdc++, libc, libsystemd) that other processes map too; a static libstdc++ build lowers it. |
| Daemon with no clients | Exits within 30 s, so it uses nothing |
| Daemon steady-state heap | No heap growth per tick. Sampling and parsing do not allocate; sd-bus allocates the outgoing message buffers. |
| `plasmashell` cost (5 module widgets on the panel) | < 8 MiB extra RSS, < 0.3 ms of CPU per update on the GUI thread |
| `gnome-shell` cost (5 module indicators) | < 5 MiB extra JS heap, no GC spikes visible in the frame timing |
| Hybrid graphics laptops | A runtime-suspended discrete GPU is never woken by the monitor |
| Panel rendering | Charts redraw only when their value changes; nothing animates while idle |

How we measure:
- CPU: `utime + stime` from `/proc/<pid>/stat`, sampled before and after a 10-minute run.
- Memory: `/proc/<pid>/smaps_rollup` (RSS and PSS).
- Wakeups: `perf stat -e 'sched:sched_wakeup'` or `powertop` on the daemon PID.
- Allocations: `heaptrack` during a steady-state run, where the expected tick count is known.
- Plasma frame cost: `QSG_RENDER_TIMING=1` and the QML profiler, with and without the widgets.
- GNOME cost: compare `gnome-shell` heap and frame timing with the extension on and off, using Looking Glass and Sysprof.

---

## 4. Architecture

```
            Plasma panel (plasmashell)                 GNOME top bar (gnome-shell)
   +-----------------------------------------+   +----------------------------------+
   | Applet instances (QML, AOT-compiled)    |   | Extension (GJS, St/Clutter)      |
   | KQuickCharts (GPU-rendered charts)      |   | St widgets + small St.DrawingArea|
   | C++ QML module: ReadoutClient singleton   |   | One shared D-Bus client          |
   +--------------------+--------------------+   +----------------+-----------------+
                        |  session D-Bus (unicast signals, typed structs) |
                        +------------------------+------------------------+
                                                 v
                    +--------------------------------------------------+
                    | readoutd - one process, one thread, sd-event loop  |
                    | Demand-driven: samples only what clients ask for |
                    | Modules: cpu mem net disk gpu sensors battery bt |
                    | Sources: /proc, /sys (fds kept open, pread),     |
                    | netlink, UPower, NetworkManager, BlueZ, UDisks2  |
                    +--------------------------------------------------+
```

### 4.1 Key decisions

**One daemon serves both desktops.**
All collection logic lives in `readoutd`, a small C++ process with no Qt and no GLib.
Plasma and GNOME are both D-Bus clients of it.
- Crash isolation: a bad parse can never take down `plasmashell` or `gnome-shell`.
- Shared sampling: five widgets across two panels trigger one sample, not five.
- One implementation of every reader, tested once.
- IPC cost at 1 Hz is a few microseconds and a few hundred bytes, which is negligible.
- Rejected alternative: using `ksystemstats` on KDE. It only exists on KDE, its sensor coverage differs from ours, and it would mean maintaining two data paths.

**Single thread on an event loop.**
`readoutd` runs one `sd-event` loop.
One aligned timer fires per base interval, and every module due on that tick samples in the same wakeup.
Timer accuracy slack lets the kernel batch our wakeup with other system timers.
Nothing in the steady state blocks on I/O, because `/proc` and `/sys` reads finish in microseconds.

**Demand-driven tiers.**
Clients subscribe per module, with a tier:
- `panel`: the few values the panel widget draws (for example total CPU %).
- `detail`: everything the popup shows (per-core usage, frequencies, top processes, and so on).

Clients request `detail` only while a popup is open.
Nobody subscribed means the module does not sample at all.
This is the biggest single resource saving, because the costly work (process scans, per-core frequency) only happens while a person is looking at it.

**Event-driven where the system offers events.**
Polling is the fallback, not the default:
- Battery and AC state: UPower `PropertiesChanged` signals.
- Wi-Fi, connection type, and connectivity: NetworkManager signals. We never send our own pings.
- Interface and address changes: an rtnetlink socket.
- Mount changes: `poll()` on `/proc/self/mountinfo` (`POLLPRI`).
- Bluetooth devices and batteries: BlueZ `ObjectManager` signals.
- Suspend and resume: logind `PrepareForSleep`, so delta counters reset and never show spikes after resume.

**Fast reads.**
- Each hot file (`/proc/stat`, `/proc/meminfo`, `/proc/net/dev`, `/proc/diskstats`, chosen hwmon inputs) is opened once and re-read with `pread(fd, buf, 0)`. No `open`/`close` per tick.
- Parsers are pure functions over `std::string_view` using `std::from_chars`. They do not allocate.
- History lives in fixed-size `float` ring buffers inside the daemon.

**Clock is client-side.**
The clock module needs no system data, so each frontend formats time with its own toolkit (`QDateTime` or `GLib.DateTime`).

**No network traffic by default.**
The public IP lookup is opt-in and only runs when a person asks for it.

### 4.2 D-Bus API (`io.github.aakash1122.Readout1`)

Bus name and interface: `io.github.aakash1122.Readout1`. Object path: `/io/github/aakash1122/Readout1`.
The `1` suffix is the API version. Breaking changes require a new `Readout2` interface.

| Member | Kind | Purpose |
|---|---|---|
| `GetInfo() -> a{sv}` | method | Static facts read once: CPU model, core counts, GPUs, disks, sensor list, battery presence. |
| `Subscribe(a{ss} module_to_tier, u interval_ms, a{sv} options)` | method | Sets or replaces this caller's subscriptions. Called again on popup open and close. Options: `sensors` (as), `cores` (b). |
| `Unsubscribe()` | method | Removes this caller's subscriptions. |
| `GetHistory(s module) -> a{sa(td)}` | method | Returns the ring buffers per series, so a newly opened popup has a full chart right away. |
| `Update(t timestamp_us, a{sa{sv}} modules)` | signal, **unicast** | One signal per subscriber per interval with all its modules, so other D-Bus clients are never woken. |

The exact contract, including every payload key, is in [docs/dbus-api.md](docs/dbus-api.md).

Rules:
- Subscriptions belong to the caller's unique bus name. When a client disappears (`NameOwnerChanged`), its subscriptions are dropped automatically.
- The effective interval of a module is the smallest interval any subscriber asked for, clamped to a minimum of 500 ms.
- Payloads are `a{sv}` dictionaries with documented keys (not fixed structs), so new keys can be added without breaking clients. An absent key means "not available".
- Updates are sent every interval, even when nothing changed. Skipping unchanged updates was dropped: rates change almost every tick, and a fixed cadence keeps the client charts simple.
- The daemon is started on demand by D-Bus activation through a systemd user unit (`Type=dbus`), and it exits 30 s after its last subscriber leaves.

### 4.3 Data sources

| Module | Source | Tier | Notes |
|---|---|---|---|
| CPU usage | `/proc/stat` | panel (total), detail (per core, user/system/iowait) | Delta of jiffies between samples. |
| CPU frequency | `cpufreq/scaling_cur_freq` | detail only, at most every 2 s | On x86, reading it can send an IPI that wakes idle cores, so it must stay out of the panel tier. |
| CPU temperature | hwmon `k10temp` (Tctl/Tdie) or `coretemp` (Package id 0) | panel or detail | The sensor is chosen once at startup by driver name. |
| Load average | `/proc/loadavg` | detail | |
| Top processes (CPU, RAM) | `/proc/<pid>/stat` through an `openat` on a held `/proc` dirfd | detail only | One read per PID gives utime, stime, RSS, and name. Top N picked with `std::nth_element`. Buffers are reused. |
| Memory | `/proc/meminfo` | panel | Used = MemTotal - MemAvailable, the kernel's own definition. |
| Memory pressure | `/proc/pressure/memory` (PSI) | detail | |
| Disk capacity | `statvfs` on real mounts from `/proc/self/mountinfo` | panel | Re-read every 30 s and whenever mounts change. |
| Disk I/O | `/proc/diskstats` | panel | Whole disks only. Partitions, loop, ram, and zram devices are filtered out. |
| Disk health (SMART) | UDisks2 D-Bus (`Drive.Ata`, `Drive.NVMe`) | detail | No root needed. Read once per popup open. |
| Disk I/O per process | `/proc/<pid>/io` | detail, best effort | Only for the user's own processes. |
| Network rates | `/proc/net/dev` | panel | Ignores `lo` and virtual interfaces by default. Handles counter wrap and interface changes. |
| Interfaces and addresses | rtnetlink | detail, event-driven | |
| Wi-Fi, connection type, connectivity | NetworkManager D-Bus | detail, event-driven | SSID, signal strength, band, security. |
| Battery | UPower D-Bus + `/sys/class/power_supply/BAT*` | panel, event-driven | Health = `energy_full / energy_full_design`, plus cycle count and power draw. |
| Sensors | hwmon (`temp*`, `in*`, `power*`, `curr*`) and `thermal_zone*` | panel or detail | Labels come from hwmon `*_label` and the chip name. No libsensors. |
| Fans | hwmon `fan*_input` | panel or detail | **Read only.** Fan control is out of scope (section 11). |
| GPU, AMD | `gpu_busy_percent`, `mem_info_vram_*`, hwmon, `pp_dpm_sclk` | panel or detail | |
| GPU, Intel i915/xe | Busy % from RC6 / gt-idle residency deltas, frequency from sysfs | panel or detail | Works without `CAP_PERFMON`. |
| GPU, NVIDIA | NVML loaded with `dlopen` | panel or detail | Optional at runtime, not needed at build time. |
| GPU per process | DRM `fdinfo` (`drm-engine-*`) | detail, best effort | Works on amdgpu, i915, xe, and others. |
| GPU power state (all vendors) | `/sys/bus/pci/devices/<bdf>/power/runtime_status` | always checked first | If the GPU is `suspended`, report "sleeping" and touch nothing else on that GPU. |
| Bluetooth | BlueZ D-Bus (`Device1`, `Battery1`) | panel or detail, event-driven | |

### 4.4 Daemon internals

- Language: C++23 (`std::expected`, `std::span`, `std::from_chars`).
- Built with `-fno-exceptions -fno-rtti`. Errors are returned as `std::expected`.
- Only dependency: `libsystemd` (sd-bus and sd-event). It ships on every systemd distribution, and `basu` or elogind's copy can replace it elsewhere.
- NVML is loaded with `dlopen` only if an NVIDIA GPU is present.
- Each module implements one small interface: `describe()`, `sample(now, tier)`, `encode(message)`, plus its event hooks.
- Missing hardware or denied permissions mean the value is simply absent, never a crash and never log spam. Each failure is logged once.
- systemd user unit hardening: `NoNewPrivileges`, `ProtectSystem=strict`, `ProtectHome=read-only`, `PrivateTmp`, `RestrictAddressFamilies=AF_UNIX AF_NETLINK`, `MemoryDenyWriteExecute`, `SystemCallFilter=@system-service`, `LockPersonality`.
- Small static binary size: link-time optimization, `-ffunction-sections`, `--gc-sections`.

---

## 5. KDE Plasma frontend

Target: Plasma 6.4 or newer (6.4 added `plasma_add_applet`), Qt 6.8 or newer, KDE Frameworks 6 (development machine runs Plasma 6.8).

### 5.1 Structure

- **One applet ID, one module per instance.**
  Each panel item is one module with its own popup, just like the separate menu bar items in Stats.
  The person adds the widget once per module they want and picks the module in its settings.
- **Combined mode.**
  One instance can show several modules side by side with one shared popup, like the Stats "combined" view.
- **C++ QML module** (`qt_add_qml_module`), with QML compiled ahead of time by `qmlcachegen` and the QML type compiler:
  - `ReadoutClient` singleton: one QtDBus connection for every applet instance in `plasmashell`.
    It counts what each instance needs and sends one merged `Subscribe` call.
    It requests `detail` only while at least one popup is expanded.
  - One model per module: a `QObject` with `Q_PROPERTY`s, notifying only when a value actually changed.
  - A C++ `ChartDataSource` subclass backed by a ring buffer, so chart data is never copied through `QVariantList`.

### 5.2 Rendering

- All charts use **KQuickCharts** (`org.kde.quickcharts`), the same shader-based chart library that Plasma System Monitor uses.
  It renders on the GPU, it is already loaded in most Plasma sessions, and it matches the native look.
- No QML `Canvas` and no `Timer`-driven polling in QML. Updates arrive as pushed signals.
- The popup (`fullRepresentation`) is only created the first time it opens.
  Charts inside it stop updating while it is closed.
- Process lists use `ListView` with delegate reuse.

### 5.3 Native look (Plasma HIG)

- Colors come from `Kirigami.Theme`: `highlightColor` for normal values, and `positiveTextColor`, `neutralTextColor`, `negativeTextColor` for the threshold levels.
- Spacing and sizes come from `Kirigami.Units`. No hard-coded pixels.
- Popups use `PlasmaExtras.Representation` with a `PlasmaExtras.PlasmoidHeading`, like Plasma's own applets.
- Text uses PlasmaComponents 3 labels with tabular figures (`font.features: { "tnum": 1 }`) so numbers do not jitter.
- Icons come from the Breeze icon theme (`cpu`, `memory`, `drive-harddisk`, `network-wired`, `battery-*`, `temperature-*`), so they follow the user's icon theme.
- The compact view adapts to panel thickness and to vertical panels through `Plasmoid.formFactor`.
- Tooltips (`PlasmaCore.ToolTipArea`) show key values without opening the popup.
- The settings dialog uses `Kirigami.FormLayout` with QtQuick Controls, stored with KConfigXT (`main.xml`).

### 5.4 Panel widget styles

Ported from Stats in this order: Text/Label, Mini bar, Line chart, Bar chart (per core), Network chart (up and down), Speed text, Battery, Stack (two-row sensor values), Pie, Tachometer.

### 5.5 Alerts

Threshold alerts (CPU above X%, temperature above Y, battery below Z%, low disk space) are evaluated in the client with hysteresis.
They are shown through `KNotification`, so they respect Do Not Disturb and the user's notification settings.

---

## 6. GNOME Shell frontend

Target: GNOME 48 or newer, through the current release.
GNOME 48 is the floor because Debian 13 ships it. Ubuntu 24.04 (GNOME 46) is not supported.

### 6.1 Structure

- ES module extension (`export default class extends Extension`).
- One `PanelMenu.Button` per enabled module, plus an optional combined button, to match the Plasma design.
- One shared D-Bus client using `Gio.DBusConnection.signal_subscribe` directly, without `Gio.DBusProxy` and its property cache.
- The client asks for `detail` only while a menu is open (`open-state-changed`).
- If `readoutd` is not installed, each indicator shows a clear message with install instructions instead of failing silently.

### 6.2 Rendering

- Text values use `St.Label`.
- Bars and the mini meter are `St.Widget` children whose width changes, so the compositor draws them with no Cairo work.
- Line and network charts use a small `St.DrawingArea`.
  It repaints only when its data changed and only while visible.
- Popup charts are only drawn while the menu is open.

### 6.3 Native look (GNOME HIG)

- Reuse the shell's own style classes (`popup-menu-item`, `popup-sub-menu`, panel button classes) wherever they fit.
- Accent color from `-st-accent-color` so the extension follows the user's accent setting.
- Sizes in `em`, so text scaling and the large-text setting work.
- Check that light, dark, and high-contrast themes all stay readable.
- Tabular figures for numbers. Confirm that St supports `font-feature-settings` on the target versions; otherwise use a fixed-width label.
- Preferences built with libadwaita (`Adw.PreferencesPage`, `Adw.SwitchRow`, `Adw.SpinRow`, `Adw.ComboRow`), stored with GSettings and bound with `Gio.Settings.bind`.
- Alerts through `MessageTray`, which respects Do Not Disturb.

### 6.4 Lifecycle rules (from the extensions.gnome.org review guidelines)

- Nothing is created in the constructor. Everything is created in `enable()` and fully removed in `disable()`: actors, signal handlers, GLib sources, D-Bus subscriptions.
- No work while the screen is locked (the extension does not declare the `unlock-dialog` session mode).
- The extension contains no binaries. `readoutd` comes from distribution packages.
- Before each new GNOME release, read the gjs.guide porting guide for that version and update the supported `shell-version` list.

---

## 7. Repository layout

```
readout/
|-- daemon/                     readoutd
|   |-- src/
|   |   |-- main.cpp            Event loop setup, D-Bus registration, idle exit
|   |   |-- bus/                Subscription registry, signal encoding
|   |   |-- modules/            cpu, memory, network, disk, gpu, sensors, battery, bluetooth
|   |   |-- parsers/            Pure parsers (string_view in, struct out), no I/O
|   |   +-- io/                 Held fds, pread helpers, /proc walker, netlink
|   |-- data/                   D-Bus service file, systemd user unit, introspection XML
|   +-- tests/
|       |-- fixtures/           Real /proc and /sys samples from many machines and kernels
|       |-- parsers/            Unit tests
|       +-- fuzz/               libFuzzer targets, one per parser
|-- frontends/
|   |-- plasma/
|   |   |-- plugin/             C++ QML module (ReadoutClient, models, chart sources)
|   |   |-- package/            metadata.json, QML, config
|   |   +-- tests/              Qt Test against a fake readoutd on a private bus
|   +-- gnome/
|       |-- extension.js, prefs.js, metadata.json, stylesheet.css
|       |-- lib/                dbusClient.js, modules/, widgets/
|       +-- schemas/
|-- tools/
|   |-- readoutctl/               Small CLI: prints samples as JSON (debugging, Waybar use later)
|   +-- perf/                   Budget harness scripts from section 3
|-- packaging/                  fedora/, arch/, debian/
|-- docs/                       D-Bus API reference, architecture notes
+-- reference/stats-macos/      Read-only submodule
```

---

## 8. Tooling and quality gates

| Area | Choice |
|---|---|
| Build | CMake 3.25+ with Ninja. One top-level project with options `READOUT_BUILD_DAEMON`, `READOUT_BUILD_PLASMA`, `READOUT_BUILD_GNOME`. |
| Compilers | GCC 13+ and Clang 17+. Warnings as errors in CI only. |
| C++ tests | GoogleTest. Parsers are tested only against fixtures, never against the host's `/proc`. |
| Fuzzing | libFuzzer target for every parser, run for a short time in CI and longer locally. |
| Sanitizers | ASan and UBSan build in CI for the daemon and its tests. |
| Lint | `clang-format` and `clang-tidy` (C++), `qmllint` (QML), `eslint` with the GJS rule set (GNOME). |
| Integration tests | Start `readoutd` on a private bus with `dbus-run-session`, subscribe, and check payloads and subscription cleanup. |
| Cross-check tests | On a real machine, compare against `free -b`, `df`, `ip -s link`, and `/proc/stat` math. Tagged and skipped in CI. |
| CI | GitHub Actions with containers for Fedora (latest), Debian 13, and Arch. |
| Perf gate | The `tools/perf` harness runs in CI for the daemon budgets that a container can measure. |
| Commits | Conventional Commits. Feature branches merged into `main` through pull requests. |

---

## 9. Phases

Work proceeds in vertical slices.
The first slice goes all the way from the daemon to a working Plasma widget, so the architecture and the budgets are proven early before the module count grows.

### Phase 0 - Reset and foundation

- Review `.clang-format` and `.clang-tidy`. Fix `.gitignore`, whose `*.cmake` rule would hide future CMake modules.
- Rewrite `README.md` for the new architecture.
- Settle the open decisions in section 12.
- Rename the repository directory and GitHub repository from `stats-kde` to `readout`.
- Top-level CMake, CI workflow, formatting and lint configs.
- Perf harness in `tools/perf` that measures every budget from section 3.
- Fixture collection script that copies the relevant `/proc` and `/sys` files from a machine into `tests/fixtures/<machine>/`. Run it on every machine available.

Exit: CI is green on an empty project and the perf harness runs against a dummy process.

### Phase 1 - Daemon core with CPU, memory, network

- sd-event loop, aligned tick scheduler, idle exit, suspend and resume handling.
- D-Bus API from section 4.2, with subscriptions, tiers, unicast signals, and history.
- CPU, memory, and network modules, including top processes in the detail tier.
- `readoutctl` for manual inspection.
- Write `docs/dbus-api.md`.

Exit: all section 3 daemon budgets pass, heaptrack shows zero steady-state allocations, the fuzzers run clean, and subscriptions are dropped when a client is killed.

### Phase 2 - Plasma slice (CPU, memory, network)

- `ReadoutClient`, models, and the ring buffer chart source.
- Compact and popup views for the three modules, built with KQuickCharts and the Plasma components.
- Per-instance settings: module, widget style, interval, colors.

Exit: daily use on Plasma 6.8 shows no visible lag, the `plasmashell` budget passes, and the widgets look right in Breeze light and dark, in horizontal and vertical panels, and at 1x and 2x scale.

### Phase 3 - Remaining daemon modules

Order: disk, sensors and fans (read only), battery, GPU (AMD, Intel, NVIDIA, power-state guard), Bluetooth.

Exit: every module has fixture tests from at least two machines, missing hardware is handled cleanly, and a suspended discrete GPU stays suspended (checked on a hybrid graphics laptop).

### Phase 4 - Plasma complete

- Every module, every widget style from section 5.4, combined mode, and alerts.
- Accessibility: names on every value for screen readers, and full keyboard navigation in popups.
- Translations through `i18n()`, with the translation template generated by the build.

Exit: every module is checked against the matching Stats popup for feature parity where Linux provides the data. The plasmashell budget passes with all modules enabled.

### Phase 5 - GNOME extension

- D-Bus client, indicators, popups, preferences, alerts.
- Same modules and widget styles as Plasma, adjusted to GNOME patterns.

Exit: the extension runs in a nested `gnome-shell --devkit` session and on real hardware for every supported version. It passes the review guidelines checklist, enable/disable cycles leak nothing, and the `gnome-shell` budget passes.

### Phase 6 - Packaging

- Fedora COPR first (the development distribution), then the AUR, then a Debian/Ubuntu PPA.
- Packages: `readoutd` (daemon and CLI), the Plasma widget, the GNOME extension. Each frontend package depends on `readoutd`.
- GNOME extension submitted to extensions.gnome.org. Its description explains that the `readoutd` package is required.

### Phase 7 - Hardening and v1.0

- A week of real use on at least: an AMD desktop, an Intel laptop, a hybrid NVIDIA laptop, and a GNOME machine.
- Final perf report from the harness, checked into `docs/perf.md`.
- README with screenshots for both desktops and install steps per distribution.

---

## 10. Feature parity map with Stats

| Stats module | Linux v1 | Not possible or deferred |
|---|---|---|
| CPU: usage, per core, frequency, temperature, load, top processes | Yes | Efficiency/performance core split, only where the kernel exposes it (hybrid Intel) |
| GPU: usage, memory, temperature, clocks, fan, power | Yes, per vendor | Some values missing on Intel |
| RAM: used, app, wired, compressed, swap, pressure, top processes | Used, cached, buffers, swap, zram, PSI pressure, top processes | macOS-only categories (wired, compressed) |
| Disk: capacity, I/O, SMART, top processes | Yes (SMART through UDisks2) | Per-process I/O for other users' processes |
| Network: rates, interface, Wi-Fi, IPs, connectivity, public IP (opt-in) | Yes | Per-process network usage (needs root or eBPF), deferred |
| Battery: level, health, cycles, time left, power | Yes | Per-app energy impact |
| Sensors: temperature, voltage, power, current, fans | Yes, read only | Fan control |
| Bluetooth: devices, battery | Yes | Batteries of devices that do not report them to BlueZ |
| Clock: multiple time zones | Yes, client-side | |
| Combined view | Yes | |
| Notifications | Yes | |

---

## 11. Risks

| Risk | Mitigation |
|---|---|
| Reading sensors wakes hardware (discrete GPU, cpufreq IPIs) | Power-state guard before every GPU read. Frequency only in the detail tier. Measured on real hybrid hardware in Phase 3. |
| `/proc` and `/sys` formats differ across kernels and drivers | Fixtures from many machines, fuzzing, and "absent value" handling instead of failure. |
| GNOME changes extension APIs each release | Support N back to Debian stable's version. Read each porting guide. Keep shell-specific calls in a small number of files. |
| KQuickCharts lacks a widget style we need | Write a small `QQuickItem` using the scene graph (`QSGGeometryNode`). Never use `Canvas`. |
| Process scans cost too much on machines with many processes | Detail tier only, adjustable interval, one read per PID, and a measured budget. |
| Fan control could damage hardware | Out of scope for v1. Revisit later only as a separate privileged helper behind polkit. |
| Scope growth | The slice order in section 9 ships something usable after Phase 2. Every addition must state its resource cost. |

---

## 12. Decisions

Settled:

| Decision | Choice |
|---|---|
| Lowest GNOME version | 48 (Debian 13). Ubuntu 24.04 (GNOME 46) is not supported. |
| Lowest Plasma version | 6.4, for `plasma_add_applet`. Debian 13 (Plasma 6.3) gets only the daemon and the GNOME extension. |
| Project name | Readout |
| App ID, D-Bus name, interface | `io.github.aakash1122.Readout`, with the versioned interface `io.github.aakash1122.Readout1` |
| Daemon and CLI | `readoutd` and `readoutctl` |
| License | MIT, matching Stats |
| GPU per-process usage | Not provided. Stats does not have it either, and reading it costs a walk of every process's fdinfo. |
| Plasma charts | KQuickCharts from QML (`ArraySource`), since KQuickCharts has no public C++ API |

Open (settle in Phase 0):

| Decision | Proposed default |
|---|---|
| Non-systemd distributions | Not supported for v1. Later possible with `basu` and a plain D-Bus service file. |

---

*This is a living plan. Update it whenever a decision changes.*
