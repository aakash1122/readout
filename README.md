# Readout

A system monitor for the KDE Plasma and GNOME Shell panels, inspired by [Stats](https://github.com/exelban/stats) for macOS.

Readout puts CPU, memory, disk, network, GPU, sensors, battery, Bluetooth and world clocks in your panel.
Each item opens a detailed popup with history charts and top processes.

It is built to cost almost nothing while it runs:

- One small daemon, `readoutd`, reads the system for every widget, so ten panel items cost the same as one.
- The daemon only reads what is on screen. Popup details (per-core data, process lists, SMART data) are read only while a popup is open.
- It starts on demand and exits 30 seconds after the last widget goes away.
- A sleeping discrete GPU is never woken up to read it.

Measured on a Ryzen 9 9900X with three panel items at a 2 second interval, `readoutd` uses about 0.02% of one CPU core and under 1.5 MiB of proportional memory.
See [docs/perf.md](docs/perf.md) for the numbers.

## Features

| Module | Panel | Popup |
|---|---|---|
| CPU | Usage, per-core bars, gauge or chart | Usage history, temperature, frequency, per-core usage (performance and efficiency cores on hybrid CPUs), load, uptime, top processes |
| Memory | Usage | App, cache, compressed and free breakdown, swap, pressure, top processes |
| Disk | Usage or read/write speed | Read/write history, every volume's free space, every drive's model, speed, temperature, SMART health and power-on hours, top processes by I/O |
| Network | Upload and download speed, or a connection dot | Speed history, connection and Wi-Fi details (signal, band, security, link speed), addresses, public IP on request, other interfaces |
| GPU | Usage | Usage history, temperature, memory, clocks, power and fan per GPU (AMD, Intel, NVIDIA) |
| Sensors | Chosen temperatures, fans, voltages or power | Every hardware sensor, grouped |
| Battery | Battery glyph and level | Level, time left, power draw, health, cycles, capacity, and mouse, keyboard and headset batteries |
| Bluetooth | Device battery levels | Adapter state and devices |
| Clock | Time | Local time and any number of time zones |

Panel styles follow the Stats menu bar: text, mini (label over value), boxed line chart, bars, pie, half-disk gauge, two-row speed, mirrored network chart, connection dot, stacked values, free and used, and battery.
Popups follow the Stats layout: rings and gauges, charts in light wells, legend squares, cards per GPU, drive, device and clock.
Several modules can share one panel item; its popup stacks a compact summary of each, with a button to open each module's full page.

Optional alerts notify you when CPU or memory usage, CPU or GPU temperature, free disk space or battery level crosses a threshold, or when the network connection drops.
Alerts respect Do Not Disturb.

## Requirements

- Linux with systemd (the daemon uses `sd-bus` and runs as a systemd user service)
- KDE Plasma 6.3 or later, or GNOME Shell 48 or later
- Optional: NetworkManager (Wi-Fi details), UPower (battery), BlueZ (Bluetooth), UDisks2 (SMART health), the NVIDIA driver (NVIDIA GPU data)

## Building

Build dependencies on Fedora:

```sh
sudo dnf install cmake ninja-build gcc-c++ systemd-devel gtest-devel dbus-daemon hwdata \
    extra-cmake-modules qt6-qtbase-devel qt6-qtdeclarative-devel \
    kf6-ki18n-devel kf6-kcoreaddons-devel kf6-kconfig-devel kf6-knotifications-devel \
    kf6-kquickcharts-devel kf6-kirigami-devel libplasma-devel \
    glib2-devel gettext
```

Build, test and install:

```sh
cmake -S . -B build -G Ninja -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
ctest --test-dir build
sudo cmake --install build
```

Leave out a desktop with `-DREADOUT_BUILD_PLASMA=OFF` or `-DREADOUT_BUILD_GNOME=OFF`.

After installing:

- **Plasma:** right-click the panel, choose *Add Widgets*, and add *Readout*.
  Add it several times to show several modules, or pick several modules in one item.
- **GNOME:** log out and back in, then run `gnome-extensions enable readout@aakash1122.github.io`.

The daemon starts by itself when a widget needs it.
To check it from a terminal:

```sh
readoutctl info          # static facts: CPU model, GPUs, disks, sensors
readoutctl watch cpu     # live CPU values as JSON lines
readoutctl once network:detail
```

## How it works

```
 Plasma widget (QML + C++)          GNOME extension (GJS)
            \                              /
             session D-Bus, unicast Update signals
                           |
                       readoutd
        /proc, /sys, hwmon, NetworkManager, UPower,
          BlueZ, UDisks2, NVML (loaded on demand)
```

`readoutd` is a single-threaded C++23 service built on `sd-bus` and `sd-event`.
Widgets subscribe with the modules they show and a tier: `panel` for what the panel draws, `detail` while a popup is open.
The daemon keeps the files it reads open, parses them without allocating, and wakes up once per interval, aligned with other timers.
The D-Bus API is documented in [docs/dbus-api.md](docs/dbus-api.md), so other frontends can use it too.

## Privacy

Readout reads only local system information and sends nothing anywhere.
The one exception is the network popup's public IP, which asks `api.ipify.org` only when you click *Show*.

## Repository layout

| Path | Contents |
|---|---|
| `daemon/` | `readoutd` sources, unit and integration tests, fuzz target |
| `tools/readoutctl/` | Command-line client |
| `frontends/plasma/` | Plasma 6 widget |
| `frontends/gnome/` | GNOME Shell extension |
| `docs/` | D-Bus API, frontend specification, GNOME notes, performance report |
| `tools/` | Fixture collection, performance measurement, QML lint gate |

## Development

- `ctest --test-dir build` runs the daemon unit tests, an integration test against a private D-Bus session, and the QML lint gate.
- `-DREADOUT_SANITIZE=address,undefined` builds with sanitizers; `-DREADOUT_FUZZ=ON` (Clang) adds a libFuzzer target for the parsers.
- `tools/perf/measure.py --name readoutd --seconds 60` measures CPU, wakeups and memory of a running process.
- GNOME extension lint: `cd frontends/gnome && npm ci && npx eslint .`

## License

MIT. See [LICENSE](LICENSE).
