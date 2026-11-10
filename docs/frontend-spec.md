# Frontend specification

This is the feature list both frontends implement: the Plasma widget and the GNOME Shell extension.
Each frontend follows its own desktop's look (section 6), but the features, settings and behavior below are the same.
The visual design (layout, sizes, charts and colors) follows the Stats app, as specified in [visual-spec.md](visual-spec.md); its section 6 lists the Readout-specific decisions.
Phase 4 and Phase 5 exit checks are run against this file.

## 1. Panel items

Each panel item shows one module, like a separate menu bar item in Stats.
A combined item shows several modules side by side with one shared popup.

Modules: `cpu`, `memory`, `disk`, `network`, `gpu`, `sensors`, `battery`, `bluetooth`, `clock`.

### Widget styles

| Style | Looks like | Modules |
|---|---|---|
| `text` | Value as text, optional short label above or icon before it | all |
| `mini` | Small module name over the value (Stats mini) | cpu, memory, disk, gpu, battery |
| `line` | Scrolling line chart of recent values, optional value overlay | cpu, memory, gpu |
| `bars` | Vertical bars: one per core for cpu, one bar otherwise | cpu, memory, disk, gpu |
| `pie` | Small donut filled to the value | cpu, memory, disk, gpu |
| `tachometer` | Arc gauge | cpu, memory, gpu |
| `speed` | Two rows: up and down (network) or write and read (disk) | network, disk |
| `dualLine` | Two-line chart, download and upload | network, disk |
| `state` | Dot: connected or not | network |
| `stack` | One or two rows of small values (selected sensors, device batteries) | sensors, bluetooth |
| `battery` | Battery glyph with level and charging mark | battery |
| `memory` | Two rows: free and used | memory, disk |

Default style per module: cpu `mini`, memory `mini`, disk `mini`, network `speed`, gpu `mini`, sensors `stack`, battery `battery`, bluetooth `stack`, clock `text`.

### Color

- Accent (default, as in Stats): charts and bars use the desktop accent color, panel text uses the text color.
- Threshold mode: at or below the warning threshold (default 70%) the accent, then orange up to the critical threshold (default 90%), then red.
- Monochrome option: text color only.

### Tooltip

Hovering a panel item shows the key values without opening the popup (for example, "CPU 12% · 48 °C").

## 2. Popups

Opening a popup switches its modules to the `detail` tier. Closing it switches them back to `panel`.
Popups of modules with daemon history (CPU, memory, network, disk, GPU) chart it from `GetHistory()`, fetched again each time the popup opens.
Battery, sensors, Bluetooth and clock popups follow their Stats layouts instead (docs/visual-spec.md section 2).

| Module | Popup content |
|---|---|
| cpu | Usage chart. Usage, temperature, frequency. Per-core bars (performance and efficiency groups on hybrid CPUs). User, system, I/O wait. Load 1/5/15. Uptime. Process and thread counts. Top processes. |
| memory | Usage chart. Breakdown bar or donut: app, cache, compressed, free. Total, used, available, app, cache, compressed, swap used of total. Pressure level. Top processes. |
| disk | Read/write chart. Each mount: used-of-total bar with free space. Each disk: model, read and write rate, totals, busy %, temperature, health, power-on hours. Top processes by I/O. |
| network | Download/upload chart. Interface, connection name and type, connectivity. Wi-Fi: SSID, signal, band, security, bitrate. Local IPv4 and IPv6, MAC. Totals. Public IP, fetched only when the user clicks it. Other active interfaces. |
| gpu | Per GPU: usage chart, temperature, memory used-of-total bar, core and memory clock, power, fan. A sleeping GPU shows "Sleeping" and is not woken. |
| sensors | Grouped list (CPU, GPU, Storage, Memory, Wireless, Network, Power, System). Each row: label, chip, value with unit. Fans listed in their group. |
| battery | Level, state, time to empty or full, power draw, health, cycles, capacity (Wh) of design, voltage, temperature, technology. Peripheral batteries (mouse, keyboard, headset). |
| bluetooth | Adapter state. Devices: icon, name, connected state, battery level. |
| clock | Local time and date, then each configured time zone with its offset. |

The combined popup stacks a compact summary per module; each summary opens that module's full page (visual-spec.md section 6.4).

Process lists show 8 entries by default (0 to 10, configurable), as in Stats. Clicking a process does nothing (no kill action), to keep the popup read-only.

## 3. Settings

Global (shared by all items of the frontend):

| Setting | Default |
|---|---|
| Update interval | 2 s (1 to 10 s) |
| Popup text size | 100% (80% to 130%, steps of 5) |
| Popup spacing (compact, normal, comfortable) | normal |
| Popup margins | 8 pt (0 to 16 pt) |
| Popup width (narrow 236 pt, normal 264 pt, wide 320 pt) | normal |

### Popup look

The four popup settings are shared by every popup of the frontend and apply live, also to an open popup.
Text size scales the text and everything drawn with it (charts, rings, gauges, row heights), relative to the desktop font.
Spacing multiplies row heights, section heights and the gaps between cards by 0.8 (compact), 1 or 1.25 (comfortable).
Compact also shrinks the header by 0.8; comfortable keeps the normal header.
Margins are the space between the popup frame and the content, in points of the desktop font; they do not follow the text size.
The desktop's own popup frame padding is not added on top: when the theme's frame pads its content, that padding is subtracted from the margin, so 8 pt looks the same on every theme.
Width sets the content width; the panel items are not affected by any of these settings.

Per panel item:

| Setting | Default | Applies to |
|---|---|---|
| Module (or modules, for combined) | cpu | all |
| Widget style | per module default | all |
| Show label | on | all |
| Show icon | off | all |
| Color mode (accent, thresholds, monochrome) | accent | level styles |
| Warning and critical thresholds | 70%, 90% | level styles |
| Processes shown in popup | 8 | cpu, memory, disk |
| Selected sensors | CPU and GPU temperature | sensors |
| GPU | first non-secondary GPU | gpu |
| Speed units (bytes or bits) | bytes | network, disk |
| Time zones and 24-hour format | local zone | clock |

## 4. Alerts

Off by default. Each one can be enabled with its own threshold.
An alert fires once when its condition has held for 10 seconds, and may fire again only after the value has dropped back below the threshold (hysteresis).

| Alert | Default threshold |
|---|---|
| CPU usage above | 90% |
| Memory usage above | 90% |
| CPU temperature above | 90 °C |
| GPU temperature above | 90 °C |
| Disk free space below | 10% |
| Battery level below (discharging) | 20% |
| Battery level reached (charging) | 80% |
| Network connectivity lost | on/off |

Alerts use the desktop's notification system, which respects Do Not Disturb.

## 5. Behavior

- If `readoutd` is not installed, every item shows a short message with install instructions.
- If `readoutd` restarts, clients subscribe again automatically.
- Values that are absent are shown as a dash or the row is hidden.
- Numbers use tabular figures so they do not shift as they change.
- All user-visible text is translatable.
- Units: bytes as KB/MB/GB (1000-based for speeds, 1024-based for sizes), temperatures in °C.

## 6. Native look

Plasma: Kirigami theme colors and units, Plasma components, `PlasmaExtras.Representation` popups, Breeze icons, KQuickCharts charts, works in horizontal and vertical panels.

GNOME: shell style classes, accent color, `em` sizes, libadwaita preferences, works with light, dark and high-contrast styles.
