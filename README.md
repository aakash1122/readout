# Readout

<p align="center"><img src="daemon/data/icons/io.github.aakash1122.readout.svg" width="120" alt="Readout icon"></p>

![Readout panel widgets](docs/images/panel.png)
![Readout popups](docs/images/popups.png)

System monitor for the KDE Plasma and GNOME Shell panels, inspired by [Stats](https://github.com/exelban/stats) for macOS.

## Installation

### Download

Prebuilt packages are attached to every [release](https://github.com/aakash1122/readout/releases).
Each release has three packages per distribution: the `readout` service, which every desktop needs, and one frontend per desktop.

| Distribution | Service | KDE Plasma | GNOME |
|---|---|---|---|
| Fedora 44 | `readout-*.fc44.x86_64.rpm` | `plasma-applet-readout-*.fc44.x86_64.rpm` | `gnome-shell-extension-readout-*.fc44.noarch.rpm` |
| Ubuntu 26.04 | `readout_*_amd64.deb` | `plasma-widget-readout_*_amd64.deb` | `gnome-shell-extension-readout_*_all.deb` |

Download the service and the frontend for your desktop, then install them from the folder you saved them in.

**Fedora:**

```bash
# KDE Plasma
sudo dnf install ./readout-*.fc44.x86_64.rpm ./plasma-applet-readout-*.fc44.x86_64.rpm

# GNOME
sudo dnf install ./readout-*.fc44.x86_64.rpm ./gnome-shell-extension-readout-*.fc44.noarch.rpm
```

**Ubuntu:**

```bash
# KDE Plasma
sudo apt install ./readout_*_amd64.deb ./plasma-widget-readout_*_amd64.deb

# GNOME
sudo apt install ./readout_*_amd64.deb ./gnome-shell-extension-readout_*_all.deb
```

The package manager installs everything else Readout needs.
To check the downloads first, save `SHA256SUMS` from the release next to them and run `sha256sum --check --ignore-missing SHA256SUMS`.

### Adding it to the panel

- **KDE Plasma:** right-click the panel, choose *Add Widgets…*, and add *Readout*.
  Add it again for every module you want in the panel, or pick several modules in one widget's settings.
- **GNOME:** log out and back in, then run `gnome-extensions enable readout@aakash1122.github.io`.
  Choose the modules in the extension preferences: `gnome-extensions prefs readout@aakash1122.github.io`.

The `readout` service starts by itself when a widget needs it; there is nothing to enable.

### Updating and removing

To update, download the packages of the new release and install them the same way.
Then restart the desktop shell, or log out and back in, so it loads the new widget.

To remove Readout, remove the service; the frontends depend on it and go with it:

```bash
sudo dnf remove readout   # Fedora
sudo apt remove readout   # Ubuntu
```

### Building it yourself

Other distributions, or other versions of Fedora and Ubuntu, can build Readout from source.
Any distribution with systemd works.

#### Fedora RPMs

```bash
sudo dnf install rpm-build git
sudo dnf builddep packaging/fedora/readout.spec
mkdir -p ~/rpmbuild/SOURCES
git archive --prefix=readout-0.1.0/ -o ~/rpmbuild/SOURCES/readout-0.1.0.tar.gz HEAD
rpmbuild -ba packaging/fedora/readout.spec
```

The packages land in `~/rpmbuild/RPMS/`; install them as in [Download](#download).

#### Ubuntu and Debian packages

```bash
sudo apt install devscripts equivs
cp -r packaging/debian debian
sudo mk-build-deps --install --remove debian/control
dch --create --package readout --newversion 0.1.0-1 --distribution unstable "Local build."
dpkg-buildpackage -us -uc -b
```

The packages land in the parent folder; install them as in [Download](#download).
Debian 13 has Plasma 6.3, which is older than the widget needs; build only the service and the GNOME extension there, with the steps below and `-DREADOUT_BUILD_PLASMA=OFF`.

#### Any distribution

Install the build dependencies first:

```bash
# Fedora
sudo dnf install cmake ninja-build gcc-c++ systemd-devel gtest-devel dbus-daemon hwdata \
    extra-cmake-modules qt6-qtbase-devel qt6-qtdeclarative-devel \
    kf6-ki18n-devel kf6-kcoreaddons-devel kf6-kconfig-devel kf6-knotifications-devel \
    kf6-kquickcharts-devel kf6-kirigami-devel kf6-kcmutils libplasma-devel glib2-devel gettext

# Debian and Ubuntu
sudo apt install devscripts equivs
sudo mk-build-deps --install --remove packaging/debian/control

# Arch Linux
sudo pacman -S --needed cmake ninja gcc pkgconf python systemd gtest dbus hwdata \
    extra-cmake-modules qt6-base qt6-declarative ki18n kcoreaddons kconfig knotifications \
    kcmutils kquickcharts kirigami libplasma glib2 gettext
```

Then build, test and install:

```bash
cmake -S . -B build -G Ninja -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
ctest --test-dir build
sudo cmake --install build
```

Leave out a desktop with `-DREADOUT_BUILD_PLASMA=OFF` or `-DREADOUT_BUILD_GNOME=OFF`.

## Requirements

- Linux with systemd
- KDE Plasma 6.4 or later, or GNOME Shell 48 or later
- Optional: NetworkManager (Wi-Fi details), UPower (battery), BlueZ (Bluetooth), UDisks2 (disk health), the NVIDIA driver (NVIDIA GPUs)

## Features

Readout is a set of panel widgets that let you monitor your Linux system.

- CPU utilization, temperature and frequency, with performance and efficiency cores on hybrid CPUs
- GPU utilization, temperature, memory, clocks, power and fan speed (AMD, Intel and NVIDIA)
- Memory usage, pressure and swap
- Disk usage, read and write speed, and drive health
- Network usage, connection and Wi-Fi details
- Battery level, health and power draw, including mice, keyboards and headsets
- Sensors information (temperature, voltage, current, power and fans)
- Bluetooth devices
- Multiple time zone clock
- Top processes for CPU, memory and disk
- Alerts for high usage, high temperature, low disk space, low battery and lost connection

Every module has several panel styles, from plain text to charts, bars, pies and gauges.
Several modules can share one panel item; its popup then shows a summary of each.

## FAQs

### How do I show several modules?

On Plasma, add the Readout widget once for every module you want, or pick several modules in one widget's settings.
On GNOME, add panel items in the extension preferences; each item can show one or more modules.

### How do I change the order of the panel items?

On Plasma, enter Edit Mode (right-click the panel, then *Enter Edit Mode*) and drag the widgets.
On GNOME, open the extension preferences: *Move Up* and *Move Down* set the order of the items, and *Position* puts them on the left, center or right of the top bar.

### How much does Readout cost to run?

Very little.
One small service, `readoutd`, reads the system for every widget, so ten panel items cost the same as one.
It reads only what is on screen: per-core data, process lists and disk health are read only while a popup is open.
It starts when a widget needs it and exits 30 seconds after the last one goes away.
A sleeping discrete GPU is never woken up to read it.

With three panel items at a 2 second interval, `readoutd` uses about 0.02% of one CPU core and under 1.5 MiB of memory.
The full measurements are in [docs/perf.md](docs/perf.md).

### The widget says "Cannot reach readoutd". What should I do?

Check that the service starts and that it can read your system:

```bash
systemctl --user status readoutd
readoutctl info
```

If `readoutctl info` works but the widget still cannot connect, log out and back in so the session picks up the new D-Bus service.
If it still fails, please open an issue with the output of both commands.

### Why does my GPU show "Sleeping"?

Laptops power down the discrete GPU when nothing uses it.
Reading its sensors would wake it and drain the battery, so Readout shows it as sleeping instead.

### Why are there no NVIDIA values?

NVIDIA GPUs are read through NVML, which comes with the proprietary NVIDIA driver.
With the open Nouveau driver, only the basic values the kernel exposes are available.

### Why do sensors have names like "Tctl" or "temp1"?

Sensor names come from the Linux kernel drivers.
Readout groups and orders them, but it does not rename them, because the same name can mean different things on different hardware.

### Does Readout send any data?

No.
Readout reads only local system information.
The one exception is the public IP in the network popup, which asks [api.ipify.org](https://www.ipify.org) only when you click *Show*.

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
Widgets subscribe with the modules they show; detailed data is only read while a popup is open.
The D-Bus API is documented in [docs/dbus-api.md](docs/dbus-api.md), so other frontends can use it too.

## Development

- `ctest --test-dir build` runs the daemon unit tests, an integration test on a private D-Bus session, and the QML lint gate.
- `-DREADOUT_SANITIZE=address,undefined` builds with sanitizers; `-DREADOUT_FUZZ=ON` (Clang) adds a libFuzzer target for the parsers.
- `cd frontends/gnome && npm ci && npx eslint .` lints the GNOME extension.
- `readoutctl watch cpu` prints live values as JSON lines, and `readoutctl once network:detail` prints one detailed sample.

The specifications are in [docs/](docs/): the D-Bus API, the frontend features, the visual design and the GNOME extension.

## Supported languages

- English

The GNOME extension is ready for translation: add `frontends/gnome/po/<language>.po` from `readout.pot` and list the language in `po/LINGUAS`.
Translations are welcome.

## License

[MIT License](LICENSE)
