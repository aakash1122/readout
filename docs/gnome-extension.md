# GNOME Shell extension

The GNOME frontend lives in `frontends/gnome/`.
It is a GNOME Shell 48+ extension (ESM) with the UUID `readout@aakash1122.github.io`.
It only draws data; all collection happens in `readoutd` (see `dbus-api.md`).
The feature list it implements is `frontend-spec.md`, and its look follows the Stats app as described in `visual-spec.md` (section 6 lists the Readout decisions).

## Layout

| Path | Purpose |
|---|---|
| `extension.js` | `enable()` / `disable()`: creates the D-Bus client, the alert monitor and one panel button per item; process icon lookup |
| `prefs.js` | libadwaita preferences: panel items, general settings, alerts |
| `lib/dbusClient.js` | The single D-Bus client shared by every panel item |
| `lib/panelItem.js` | One `PanelMenu.Button` per item: panel widgets, the popup frame (header with system monitor, title and settings buttons), the combined overview with back navigation, tooltip |
| `lib/panelWidgets.js` | Panel widget styles (mini, text, line, bars, pie, tachometer, speed, dualLine, state, stack, battery, memory) drawn at the Stats geometry, and the clock |
| `lib/popups.js` | The full page of each module (dashboard, sections, cards, top processes) |
| `lib/summaries.js` | Compact module summaries for the combined overview |
| `lib/charts.js` | Cairo drawings: line and mirrored charts, rings, gauges, LED core bars, grid, capacity bar, battery, analog clock |
| `lib/components.js` | Section separators, key/value rows with legend squares, cards, process lists |
| `lib/theme.js` | The Stats series palette (light and dark), accent, threshold and battery colors, scale |
| `lib/values.js`, `lib/format.js` | Value selection and Stats number formatting |
| `lib/alerts.js` | Threshold alerts through the message tray |
| `lib/catalog.js` | Modules, styles and item validation, shared with the preferences |
| `schemas/` | GSettings schema `org.gnome.shell.extensions.readout` |
| `stylesheet*.css` | Layout in em (one Stats point is 1em / 13) plus the light-mode card colors |
| `icons/` | Symbolic icons that Adwaita does not have (CPU, memory, GPU, temperature, clock) |
| `po/POTFILES` | Files with translatable strings |

## How it stays cheap

- One D-Bus client merges the demand of every panel item and the alerts into a single `Subscribe()` call.
  Calls are coalesced to one per main-loop iteration and skipped when nothing changed.
- Data arrives through the unicast `Update` signal; nothing is polled.
- Panel items use the `panel` tier.
  A module switches to `detail` only while its full page is visible.
  The combined overview uses panel-tier data, so only a module page opened with "›" costs detail sampling.
- Popup pages are built on first use and updated only while visible.
- Every drawing is an `St.DrawingArea` that repaints only when its data changed and it is mapped.
  Text-like panel styles (mini, speed, memory, state, stack, battery, pie) skip the repaint when the shown values did not change.
- Panel charts call `GetHistory()` once; a popup page calls it each time it opens, so its charts have no gap from the time spent in the panel tier.
  While open, per-drive charts are extended from the detail payload.
- Client-side state is small and bounded: the 90-cell connectivity buffer per item, and per-sensor and GPU chart history only while the page is open.
- The clock ticks only while it is visible, aligned to the next second or minute; the clock page and summary tick only while shown.
- Labels are only written when their text changes.

Measured on GNOME Shell 50.6 (headless nested session, 60 s windows, 2 s interval):

| Case | gnome-shell CPU (one core) |
|---|---|
| Extension disabled | 0.13% |
| Default items (CPU, memory, network) | 0.40% |
| Default items, with the CPU popup open | 0.88% |
| Six items with charts and a clock with seconds | 0.62% |

## Settings

All settings are in GSettings.
`items` is a JSON list; each entry is one panel item:

```json
{
  "modules": ["cpu", "memory"],
  "styles": {"cpu": "mini", "memory": "pie"},
  "showLabel": true,
  "showIcon": false,
  "colorMode": "accent",
  "warning": 70,
  "critical": 90,
  "processCount": 8,
  "sensors": [],
  "gpu": "",
  "bits": false,
  "timezones": ["Europe/Berlin"],
  "clock24": true,
  "clockSeconds": false
}
```

Unknown or invalid values fall back to defaults (`lib/catalog.js`, `normalizeItem`): color mode `accent`, 8 processes, thresholds 70 and 90.
`colorMode` is `accent` (charts in the desktop accent, panel text in the text color), `thresholds` (accent, then orange above the warning and red above the critical threshold) or `monochrome`.
The `memory` style (free and used in two rows) is available for the memory and disk modules.
An empty `sensors` list shows the CPU and GPU temperature.
An empty `gpu` picks the first GPU that is not secondary.
For the clock, the first entry of `timezones` is the zone shown in the panel; the popup lists local time and every zone.

The popup look keys are shared by all items (docs/frontend-spec.md, "Popup look"):
`popup-text-scale` (80 to 130 percent), `popup-spacing` (`compact`, `normal`, `comfortable`), `popup-margin` (0 to 16 points) and `popup-width` (`narrow`, `normal`, `wide`).
`lib/panelItem.js` applies them to `.readout-menu-content`: the text size and margins as an inline style, spacing and width as style classes defined in `stylesheet.css`.
Because every size in the stylesheet is in em, the text size scales the whole popup, charts included.
`popup-heights` (internal) maps `readout-N/module` to the last full height of that popup page, so the popup opens at that height while it waits for its first detail update (docs/visual-spec.md 6.4).

Other keys: `update-interval` (1 to 10 seconds), `panel-box` (`left`, `center`, `right`) and `alert-<name>-enabled` / `alert-<name>-threshold`.

## Building and installing

```sh
cmake -S . -B build -DREADOUT_BUILD_GNOME=ON
cmake --build build                          # compiles the schema
cmake --install build                        # installs to <prefix>/share/gnome-shell/extensions/<uuid>
cmake --build build --target gnome-extension-zip   # <uuid>.shell-extension.zip for extensions.gnome.org
cmake --build build --target gnome-extension-pot   # refreshes po/readout.pot
```

Translations go in `po/<language>.po`, with the language code added to `po/LINGUAS`.

## Linting

```sh
cd frontends/gnome
npm install
npx eslint .
```

`eslint.config.js` follows the GJS and GNOME Shell code style.
Nothing from `package.json` or `node_modules` is installed or packed.

## Testing in a nested session

A headless nested shell keeps the test away from the running desktop.
Use separate XDG directories so the test does not touch your real settings:

```sh
export XDG_CONFIG_HOME=/tmp/readout-test/config XDG_DATA_HOME=/tmp/readout-test/share
cmake --install build --prefix /tmp/readout-test   # lands in $XDG_DATA_HOME/gnome-shell/extensions
dbus-run-session -- bash -c '
    build/daemon/readoutd --no-idle-exit &
    gsettings set org.gnome.shell enabled-extensions "[\"readout@aakash1122.github.io\"]"
    gnome-shell --headless --virtual-monitor 1600x900 --wayland --no-x11'
```

`gnome-shell --devkit --wayland` opens the same session in a window instead.
`org.gnome.Shell.Eval` and `org.gnome.Shell.Screenshot` need unsafe mode, which a small helper extension can turn on (`global.context.unsafe_mode = true`) in the test session only.
