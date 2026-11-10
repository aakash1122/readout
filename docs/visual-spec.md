# Stats visual specification

This document describes how the macOS Stats app (MIT licensed, `reference/stats-macos`) draws its popups and menu-bar widgets.
Both Linux frontends (Plasma QML widget and GNOME Shell extension) use it as the target for visual parity.
All numbers come from the Swift source and are in macOS points (1 pt = 1 logical px).
File references are relative to `reference/stats-macos`.

Conventions used below:

- "x, y" positions follow AppKit, so `y` grows upward unless stated otherwise.
- Percent values inside Stats are fractions `0..1`; our daemon sends `0..100`, so divide by 100 before using the formulas here.
- `pct(v)` means `Int(round2(v) * 100) + "%"`, where `round2` rounds to 2 decimals and `Int` truncates.
  For example 0.678 becomes "68%".
- Color names are AppKit names; their sRGB values are in the color table in section 1.6.

## 1. Global look

### 1.1 Popup window

Source: `Kit/constants.swift`, `Kit/module/popup.swift`.

| Property | Value |
|---|---|
| Content width | 264 |
| Outer margin (all sides around the body) | 8 |
| Window width | 264 + 2 * 8 = 280 |
| Header height | 42 |
| Window height | body height + 42 + 16, capped at screen height - 22 (then the body scrolls) |
| Corner radius | 6 |
| Shadow | standard window shadow |
| Background, light | opaque white (`#FFFFFF`) layered over the vibrancy view |
| Background, dark | `NSVisualEffectView` material `.titlebar`, behind-window blur; looks like about `rgba(40,40,40,0.92)` with blur |
| Scrollbar | vertical only, auto-hiding overlay |
| Inter-section spacing | 0 (sections are stacked with no gap; separators provide the rhythm) |

The popup has no visible border and no header divider line.
The body is a vertical stack; each module computes its height as the sum of its sections, so the window always fits the content exactly.

### 1.2 Header

The header is a 280 x 42 horizontal row with three items:

| Item | Geometry | Look |
|---|---|---|
| Left button | icon area 24 wide, padded to the header height (hit area about 42 x 42) | "chart" glyph tinted `lightGray`; opens Activity Monitor; Clock module uses a "calendar" glyph |
| Title | fills the rest, text centered | module name ("CPU", "RAM", "GPU", "Disk", "Network", "Battery", "Sensors", "Bluetooth", "Clock"), system font 16, regular, `textColor` |
| Right button | same as left | "settings" gear glyph tinted `lightGray`; opens module settings |

When the user drags the popup away from the menu bar, the left glyph changes to a "close" (X) glyph and the popup stays open until closed.
Linux mapping: left button opens the system monitor (KSysGuard / Plasma System Monitor / GNOME System Monitor), right button opens the frontend settings.

### 1.3 Section separator

Source: `separatorView()` in `Kit/helpers.swift`.

| Property | Value |
|---|---|
| Height | 30 |
| Text | the section title in normal case, not uppercase (for example "Usage history", "Details", "Top processes") |
| Font | system 12, medium weight |
| Color | `secondaryLabelColor` |
| Alignment | centered horizontally; 15 px tall label centered vertically |
| Lines / rules | none |

Some separators carry a small action button at the right edge: an 18 x 18 icon at x = 264 - 18, y = 6, tinted `lightGray`, borderless (Net "Details" reset, "Interface" tune, "Address" refresh).

### 1.4 Row types

| Row | Height | Label | Value | Notes |
|---|---|---|---|---|
| Key/value (`popupRow`) | 22 | system 12 regular, `secondaryLabelColor`, left, width = text width + 4 | system 13 regular, `textColor`, right-aligned, fills the remaining width | Text boxes are 16 tall at y = 3. Labels always end with ":" ("System:"). |
| Multiline key/value | lines * 16 + 6 | label pinned to the top line | value wraps, one line per entry | Used for DNS servers. |
| Colored key/value (`popupWithColorRow`) | 22 | label at x = 18, same font as above | same as above | A 12 x 12 square at x = 2, y = 5 (vertically centered), corner radius 2, filled with the series color. |
| Compact value variant | 22 | same | system 11 regular | Used for Uptime, Disk detail rows, SMART rows. |
| Grid key/value (GPU details) | 16 | `LabelField` 12 regular secondary | `ValueField` 13 regular right | No gap between rows. |

The colored square is the only "dot" shape in popup rows; it is a rounded square, not a circle.
The "idle" and "free" squares use their color at 50% alpha, matching the empty arc of the pie.

### 1.5 Charts and wells

Every chart sits in a "well": background `lightGray` at 10% alpha (`rgba(170,170,170,0.10)`), corner radius 3, no border, no grid lines, no axes, no labels except where noted.

| Chart | Source | Drawing |
|---|---|---|
| Line chart | `LineChartView` | Polyline, stroke width 1 device pixel (`1 / scale`), color = series color. Area under the line filled with a vertical gradient from series color at 50% alpha (bottom, baseline) to 100% alpha (top of the filled shape). X spacing = width / (points - 1). Default scale: `none`, meaning values are fractions 0..1 mapped to full height. Missing points (nil) break the line. |
| Line chart hover | same | Vertical dashed line (dash 4/4) and horizontal dashed line (dash 6/6), 1 device px, `tertiaryLabelColor`; 4 px red circle outline on the nearest point; tooltip box. |
| Tooltip box | `drawToolTip` | 78 wide, 14 tall (24 with subtitle), radius 2, 1 px `gray` border, fill black (dark) or white (light) at 80% alpha. Value system 12 regular; subtitle (timestamp `dd/MM HH:mm:ss`) system 9 medium at 70% alpha. |
| Min/max label | `minMax` | Max value of the visible window, system 9 light, at the outer edge of the chart (top-left for the upper half, bottom-left for the lower half), 1 px from the left. |
| Network chart (mirrored) | `NetworkChartView` | Two line charts, each half the height. Default: upload (out) in the top half growing upward from the center line, download (in) in the bottom half growing downward from the center line. "Reverse order" swaps halves. Each half auto-scales to its own max. |
| Pie / ring | `PieChartView` | Ring stroke width 7 (or full radius when `filled`), butt caps, starts at 12 o'clock and runs clockwise through the segments in the given order; the remainder up to 100% is the "non active" color at 50% alpha (default `lightGray`). Radius = (min(w, h) - 7) / 2. Optional center text: `pct(value)`, system 15 regular, `textColor` (white in dark). |
| Gauge (270 degree) | `HalfCircleGraphView` | Stroke width 7, round caps. The arc covers 270 degrees with the gap at the bottom: it starts at about 7:30 (bottom-left) and fills clockwise over the top to about 4:30 (bottom-right). Filled part = color (default `systemBlue`), rest = `lightGray` at 50% alpha. Center text system 10 regular, `textColor`. Values > 1 are treated as percent (`v / 100`). |
| Tachometer (180 degree) | `TachometerGraphView` | Upper half circle, left to right, butt caps, filled (stroke width = radius) by default; segments in order, remainder `lightGray` at 50%. Shifted down 4 px. |
| Per-core bars | `BarChartView` | One column per core, gap 2. Column = rounded rect radius 3 filled `underPageBackgroundColor` at 50%. If the chart is 30 px or taller, each column is an LED meter of 16 blocks: block width = column width - 4, block height = ((h - 2 - 1) / 16) - 1, gap 1, radius 1; active blocks = `round(v * 16)` in the core color, inactive blocks `controlBackgroundColor` at 40%. Hover shows `pct(v)` in a tooltip. |
| Grid chart | `GridChartView` | Rounded cells (radius 1), gap 2; inactive `underPageBackgroundColor` at 40%, ok `systemGreen`, failure `systemRed`. New values enter at the last cell (bottom-right) and older ones shift toward the top-left. |
| Horizontal capacity bar | Disk `BarView` | Height 10, radius 3, 0.25 px border `secondaryLabelColor`, track white (light) or `lightGray` 10% (dark), fill `controlAccentColor` with width = used fraction. |

### 1.6 Colors

Popups default to these series colors; the user can change each one in settings.

| AppKit name | Light sRGB | Dark sRGB | Used for |
|---|---|---|---|
| `systemBlue` | `#007AFF` | `#0A84FF` | CPU user, RAM app, read, download, gauges, needle |
| `systemRed` | `#FF3B30` | `#FF453A` | CPU system, write, upload, today in calendar, failed connectivity |
| `systemOrange` | `#FF9500` | `#FF9F0A` | RAM wired, battery 20-40% |
| `systemPink` | `#FF2D55` | `#FF375F` | RAM compressed |
| `systemGreen` | `#28CD41` | `#32D74B` | GPU active dot, pressure normal, connectivity ok, battery charged |
| `systemYellow` | `#FFCC00` | `#FFD60A` | Pressure warning |
| `systemTeal` | `#59ADC4` (approx.) | `#6AC4DC` (approx.) | Efficiency cores |
| `systemIndigo` | `#5856D6` | `#5E5CE6` | Performance cores |
| `controlAccentColor` | user accent, default `systemBlue` | same | Line charts, capacity bars, fan bars |
| `lightGray` | `#AAAAAA` | `#AAAAAA` | CPU idle, RAM free, header icons, inactive dots |
| `gray` | `#808080` | `#808080` | Tooltip border, calendar weekday header |
| `orange` (plain) | `#FF8000` | `#FF8000` | Utilization color 60-80% |
| `red` (plain) | `#FF0000` | `#FF0000` | Utilization color > 80%, hover dot, clock seconds hand |
| `textColor` | `#000000` | `#FFFFFF` | Values |
| `labelColor` | `rgba(0,0,0,0.85)` | `rgba(255,255,255,0.85)` | Disk name, network unit |
| `secondaryLabelColor` | `rgba(0,0,0,0.50)` | `rgba(255,255,255,0.55)` | Row labels, separators |
| `tertiaryLabelColor` | `rgba(0,0,0,0.26)` | `rgba(255,255,255,0.25)` | Process list header, hover crosshair |
| `quaternaryLabelColor` | `rgba(0,0,0,0.10)` | `rgba(255,255,255,0.10)` | Fan bar border |
| `underPageBackgroundColor` | about `#969696` | about `#282828` | Bar and grid chart tracks (used at 40-50% alpha) |
| `controlBackgroundColor` | `#FFFFFF` | `#1E1E1E` | Inactive LED blocks (40% alpha), clock face |

Card background (GPU, Disk, Fans, Bluetooth, Clock, Calendar): light `rgb(245,245,245)` opaque, dark `rgba(17,17,17,0.25)`, corner radius 2.

Utilization color (`Double.usageColor`, zones default `(0.6, 0.8)`): `v <= 0.6` is `systemBlue`, `0.6 < v <= 0.8` is plain `orange`, above is plain `red`.
RAM bar widget uses zones `(0.8, 0.95)`.
Battery uses the reversed mapping (low is red).
Pressure color: normal `systemGreen`, warning `systemYellow`, critical `systemRed`.
Battery color (`batteryColor`): below 20% `systemRed`; 20-40% `systemOrange` when colorize is on, else `textColor`; 40-99% `systemGreen` when colorize is on, else `textColor`; exactly 100% `textColor`; low power mode `systemOrange`.
"Monochrome" means white in dark mode and black in light mode.

Linux mapping: use the desktop accent for `controlAccentColor` (Plasma `Kirigami.Theme.highlightColor`, GNOME `-st-accent-color`), and use the dark column when the panel or popup theme is dark.

### 1.7 Number formatting

| Kind | Format | Example |
|---|---|---|
| Percent | `pct(v)`, integer, truncated | `42%` |
| Process CPU | raw double + "%" | `12.3%` |
| Speed (`getReadableSpeed`) | SI (1000). `< 1 KB/s` is `0 KB/s`; KB integer; MB with 1 decimal below 100 MB/s, integer from 100 to 999 MB/s; GB 1 decimal | `0 KB/s`, `512 KB/s`, `1.2 MB/s`, `340 MB/s`, `1.1 GB/s` |
| Speed in bits | same thresholds on bytes * 8, units `Kb/s`, `Mb/s`, `Gb/s` | `9.6 Mb/s` |
| Memory size (`getReadableMemory(.memory)`) | binary (1024), adaptive, up to 2 decimals, comma replaced by dot | `7.83 GB`, `512 MB` |
| File size (`.file`) | decimal (1000), adaptive | `1.24 GB` |
| Disk size (`DiskSize`) | decimal; KB/MB integer, GB/TB 1 decimal | `245.1 GB of 494.4 GB free` |
| Temperature | no decimals in gauges and rows, 1 decimal in the Sensors popup, unit symbol attached | `54°C`, `54.3°C` |
| Frequency row | integer MHz with a space | `3200 MHz` |
| Frequency gauge text | GHz with up to 2 decimals, no unit | `3.2` |
| Durations | `DateComponentsFormatter`, full style, max 2 units of day/hour/minute | `2 days, 4 hours` |
| Battery time, short | `h:mm` | `3:07` |
| Battery time, long | `Xh YYmin`, `YYmin`, `Xh` | `3h 07min` |
| Latency / jitter | 2 decimals + " ms" | `18.42 ms` |

### 1.8 Top processes list

Source: `Kit/process.swift`.
Shared by CPU, RAM, Disk, Net and Battery.

| Part | Geometry and style |
|---|---|
| Section height | `22 * n + 30 + 22` (separator + header row + n rows); 0 when n = 0 |
| n | user setting, default 8, choices 0, 3, 5, 8, 10, 15 |
| Separator | "Top processes" |
| Header row (22) | 22 px empty icon column; "Process" in system 12 medium `tertiaryLabelColor`; one value column: title right-aligned in the same style ("Usage"); two value columns: each column is 60 wide and shows only a 12 x 12 colored square (radius 4) at its right edge (x = 48) |
| Row (22) | icon box 22 x 22 with a 12 x 12 app icon at x = 2, y = 5; name in system 12 regular `secondaryLabelColor`, truncated; each value column 68 wide, right-aligned, system 13 regular (system 10 when there are 2 columns), `textColor` |
| Hover | row background `rgba(3,3,3,0.05)`, radius 3 |
| Click (lock) | background `rgba(3,3,3,0.10)`; the icon is replaced by a gray "cancel" glyph that kills the process |
| Empty value | blank (Disk uses "-") |

## 2. Module popups

Heights are listed as "section height = content + 30 separator".
Sections are in exact top-to-bottom order.

### 2.1 CPU

Source: `Modules/CPU/popup.swift`.

| # | Section | Height | Contents |
|---|---|---|---|
| 1 | Dashboard (no title) | 90 | Three gauges, see below |
| 2 | "Usage history" | 150 | Separator 30, line chart well 70, per-core bar well 50 |
| 3 | "Details" | `22 * rows + 30` | Rows below |
| 4 | "Average load" | `22 * 3 + 30` = 96 | "1 minute:", "5 minutes:", "15 minutes:" |
| 5 | "Frequency" | `22 * rows + 30` | Inserted only after the first frequency sample arrives |
| 6 | "Top processes" | `22 * n + 52` | One column "Usage", value `"\(usage)%"` |

Dashboard (264 x 90):

| Element | Frame | Drawing |
|---|---|---|
| Usage ring (center) | 70 x 70 at x = 97, y = 10 | `PieChartView`, ring width 7. Segments clockwise from 12 o'clock: system (`systemRed`), user (`systemBlue`); remainder = idle color (`lightGray`) at 50%. Center text `pct(totalUsage)`, 15 regular. |
| Temperature gauge (left) | 50 x 50 at x = 23.5, y = 17 (3 px below center) | 270 degree gauge, `systemBlue`, fill = temperature / 100 (so 54 degrees fills 54%). Text e.g. `54°C`, 10 regular. Hidden until a value exists. |
| Frequency gauge (right) | 50 wide x 90 tall at x = 190.5 (gauge 50 x 50 centered vertically) | 270 degree gauge, `systemBlue`, fill = current MHz / highest MHz seen so far. Text = GHz, e.g. `3.2`. Hidden until a value exists. |

Usage history:

- Line chart well 264 x 70, chart inset x = 1, 180 points by default (settings: 60, 120, 180, 300, 600), color `controlAccentColor`, value = total usage.
- Bar well 264 x 50, chart inset 2 on each side (260 x 46), one LED column per logical core, 16 blocks each.
- Core colors: efficiency `systemTeal`, performance `systemIndigo` on hybrid CPUs; `systemBlue` for all cores otherwise.

Details rows in order:

| Row | Kind | Value | Condition |
|---|---|---|---|
| System: | colored, `systemRed` | `pct(system)` | always |
| User: | colored, `systemBlue` | `pct(user)` | always |
| Idle: | colored, `lightGray` at 50% | `pct(idle)` | always |
| Scheduler limit: | plain | `N%` | Intel Macs only |
| Speed limit: | plain | `N%` | Intel Macs only |
| Efficiency cores: | colored, `systemTeal` | `pct` | hybrid CPUs |
| Performance cores: | colored, `systemIndigo` | `pct` | hybrid CPUs |
| Uptime: | plain, value 11 pt | `2 days, 4 hours` | always |

Frequency rows: "All cores:" `3200 MHz`; on hybrid CPUs also "Efficiency cores:" (teal square) and "Performance cores:" (indigo square).

Total height example (non-hybrid, no limits rows, 8 processes): 90 + 150 + (22 * 4 + 30) + 96 + (22 + 30) + (22 * 8 + 52) = 734.

### 2.2 RAM

Source: `Modules/RAM/popup.swift`.

| # | Section | Height | Contents |
|---|---|---|---|
| 1 | Dashboard | 90 | Pressure gauge (left), usage ring (center); right side empty |
| 2 | "Usage history" | 120 | Separator 30, line chart well 264 x 90, 180 points, accent color, value = usage |
| 3 | "Details" | `22 * 6 + 30` = 162 | Rows below |
| 4 | "Top processes" | `22 * n + 52` | One column "Usage", value = resident memory, binary format |

Dashboard:

| Element | Frame | Drawing |
|---|---|---|
| Usage ring | 70 x 70 centered (x = 97, y = 10) | Segments clockwise from 12 o'clock: app (`systemBlue`), wired (`systemOrange`), compressed (`systemPink`), each = bytes / total; remainder = free color (`lightGray`) at 50%. Center text `pct(usage)`. |
| Pressure gauge | 60 x 50 at x = 14.5, y = 10 | 270 degree arc, width 7, round caps, split into three equal thirds: `systemGreen`, `systemYellow`, `systemRed` (bottom-left to bottom-right). A `systemBlue` filled triangular needle from the center (base 4 px) points to the green third (toward 0.15w, 0.40w) for normal, straight up (0.5w, 0.85w) for warning, right (0.85w, 0.40w) for critical. Center hub circle 4 px `systemBlue`. Pressure level number in system 9 regular just below the hub. |

Details rows in order:

| Row | Kind | Value |
|---|---|---|
| Used: | plain | binary size |
| App: | colored `systemBlue` | binary size |
| Wired: | colored `systemOrange` | binary size |
| Compressed: | colored `systemPink` | binary size |
| Free: | colored `lightGray` at 50% | binary size |
| Swap: | plain | binary size used |

### 2.3 GPU

Source: `Modules/GPU/popup.swift`.

The popup is a vertical list of GPU cards with 8 px between cards, in reversed enumeration order.
Each card uses the card background (section 1.6), full width 264.

| # | Part | Height | Contents |
|---|---|---|---|
| 1 | Title row | 24 | GPU model name, system 13 regular `secondaryLabelColor`, at x = 0; a 6 x 6 status dot (fully round) 8 px after the name, `systemGreen` if active else `systemRed`; at the right edge a borderless text button "DETAILS" in system 9 regular |
| 2 | Details (toggled by the button) | `16 * rows + 8` | Grid inset 8: Vendor, Model, Cores, Status ("Active" / "Non active"), Fan speed (`N%`), Core clock (`NMHz`, no space), Memory clock, Temperature, Utilization, Render utilization, Tiler utilization; only rows with values |
| 3 | Gauge row | 70 | Up to four 50 x 50 270 degree gauges, equal distribution, insets top 10, left 10, right 10, bottom-aligned: temperature (text `54°C`), utilization, render utilization, tiler utilization (text `pct`) |
| 4 | Chart row | 80 | One line chart per gauge, 60 tall, equal widths, spacing 8, insets 8; each chart has its own well (`lightGray` 10%, radius 3), 120 points, accent color, hover disabled. The temperature chart plots degrees / 100. |

### 2.4 Disk

Source: `Modules/Disk/popup.swift`.

The popup is a list of drive cards (8 px apart), then "Top processes".
Each card is 264 wide, card background, radius 2, inner content 248 wide centered, vertical padding 5 top and bottom, 5 px between parts.
Card height = sum(part heights + 5) - 5 + 10.

| # | Part | Height | Contents |
|---|---|---|---|
| 1 | Name row | 16 | Drive name as a borderless button in `labelColor` (opens the mount in the file manager); then two 8 x 8 round activity dots 2 px apart: read and write, `lightGray` 75% when idle, `systemBlue` / `systemRed` when the rate is non-zero; flexible space; 15 x 15 "refresh" glyph and 15 x 15 "tune" glyph tinted `lightGray`, spacing 4 |
| 2 | Activity chart | 36 | Mirrored network chart (section 1.5), write (red) top half, read (blue) bottom half, 120 points, hover disabled, no min/max labels; well white in light mode, `lightGray` 10% in dark, radius 3 |
| 3 | Capacity bar | 10 | Section 1.5 capacity bar, inset 1 px left and right |
| 4 | Legend | 16 | Left: `"X GB of Y GB free"` in system 11 light; right (40 wide): free percent in system 11 regular, right-aligned. Clicking toggles to `"X GB of Y GB used"` and the used percent. |
| 5 | Details (toggled by "tune") | 88 + 162 | Speed block (4 rows of 22, values 11 pt): "Read:" (blue square) speed, "Write:" (red square) speed, "Total read:", "Total written:" (decimal sizes). Then separator "SMART" and 6 rows (values 11 pt): Total read, Total written, Temperature, Health (`N%`), Power cycles, Power on hours. |

Top processes: two columns "Read" (blue square) and "Write" (red square), values are speeds in system 10.

### 2.5 Network

Source: `Modules/Net/popup.swift`.

| # | Section | Height | Contents |
|---|---|---|---|
| 1 | Dashboard | 90 | Download (left half) and upload (right half) big numbers |
| 2 | "Usage history" | 30 + 90 | Mirrored network chart, well 264 x 90, chart 264 x 88 at y = 1, 180 points, min/max labels on, upload `systemRed` top, download `systemBlue` bottom |
| 3 | "Connectivity history" | 30 + 30 | Grid chart 264 x 28 at y = 1, 30 columns x 3 rows = 90 cells; cell width = (264 - 29 * 2) / 30 = 6.87, cell height = (28 - 2 * 2) / 3 = 8 |
| 4 | "Details" + reset button | 30 + 22 * 6 | Rows below |
| 5 | "Interface" + tune button | 30 + 22 * rows | Rows below |
| 6 | "Address" + refresh button | 30 + 22 * rows | Hidden entirely when "Public IP" is off |
| 7 | "Top processes" | `22 * n + 52` | Columns "Downloading" (blue square) and "Uploading" (red square), values are speeds in system 10 |

Dashboard half (132 x 90), content centered horizontally and vertically:

- Value line (30 tall): number in system 26 light `textColor`, right-aligned, width = text width + 5; unit right after it in system 13 light `labelColor`, raised 4 px from the line bottom.
- Title line (15 tall, directly under the value): a 12 x 12 square with radius 4 at y = 1, then the title "Downloading" / "Uploading" in system 12 regular `secondaryLabelColor`.
- The square uses the series color while the rate is non-zero and `lightGray` at 75% when it is zero.
- The number uses `getReadableTuple`, which splits value and unit (`"1.2"` + `"MB/s"`); below 1 KB/s it shows `"0"` + `"KB/s"`.

Details rows:

| Row | Kind | Value |
|---|---|---|
| Total upload: | colored `systemRed` | decimal size; tooltip "Last reset" duration |
| Total download: | colored `systemBlue` | decimal size |
| Status: | plain | `UP` / `DOWN` |
| Internet connection: | plain | `UP` / `DOWN` / `Unknown` |
| Latency: | plain | mean of last 90 samples, `18.42 ms` |
| Jitter: | plain | mean of last 90 samples, `1.07 ms` |

The reset button zeroes both totals.

Interface rows:

| Row | Value | Condition |
|---|---|---|
| Interface: | `Wi-Fi (en0, US)` (display name, BSD name, country) | always |
| Status: | `UP` / `DOWN` | always |
| Physical address: | MAC, selectable | always |
| Network: | `SSID (-55)` (RSSI in parentheses) | Wi-Fi only |
| Standard: | e.g. `802.11ax` | Wi-Fi and details toggle on |
| Channel: | e.g. `36 (5 GHz, 80 MHz)` | Wi-Fi and details toggle on |
| Speed: | `1000baseT` (rounded transmit rate) | details toggle on |
| DNS Server: | one server per line (multiline row) | details toggle on |

Address rows: "Local IP:" (IPv4, else IPv6), "Public IP:" IPv4 with a country flag emoji (or `(CC)`), "Public IP:" IPv6 drawn in system 7 semibold to fit.
The public rows appear only when a value exists.
While refreshing, the values show `Updating...`.

### 2.6 Battery

Source: `Modules/Battery/popup.swift`.

| # | Section | Height | Contents |
|---|---|---|---|
| 1 | Dashboard | 90 | Large battery icon |
| 2 | "Details" | `22 * 4 + 30` = 118 | Level, Source, Time, Last charge |
| 3 | "Battery" | `22 * 7 + 30` = 184 | Health, Capacity, Cycles, Temperature, Power, Current, Voltage |
| 4 | "Power adapter" | `22 * 4 + 30` = 118 | Only while on AC power, inserted before processes |
| 5 | "Top processes" | `22 * n + 52` | One column "Usage", value `"\(usage)%"` (energy impact) |

Dashboard battery icon, drawn in a 248 x 74 box (inset 8):

- Body size `w = min(248, 120) = 120`, `h = min(74, 50) = 50`, centered.
- Outline: rounded rect at (x + 1, y + 1), size (w - 8) x (h - 2) = 112 x 48, radius 3, stroke 1 `textColor`.
- Terminal: 8 x 8 circle centered on the right edge of the outline, filled `textColor`, with a 4 px vertical line punched out (destination-out) along the outline edge so the nub looks detached.
- Fill: rounded rect at (x + 2, y + 2), width `(w - 10) * level`, height `h - 4`, radius 3, color from `batteryColor(colorize)`; colorize defaults to off, so the fill is `textColor` unless the level is under 20%.

Details rows: "Level:" `pct`; "Source:" `Battery Power` / `AC Power`; the third label switches between "Time to discharge:" and "Time to charge:" and its value is a short (`3:07`) or long (`3h 07min`) duration, `Calculating`, `Fully charged` or `Unknown`; "Last charge:" duration since unplugged.

Battery rows: "Health:" `92% (Normal)`; "Capacity:" `4012 / 4380 / 4790 mAh` (current / max / design); "Cycles:" integer; "Temperature:" `31°C`; "Power:" `7.42 W` (= voltage * |amperage| / 1000); "Current:" `612 mA`; "Voltage:" `12.13 V`.

Power adapter rows: "Is charging:" `Yes` / `No`; "Power:" `67 W`; "Current:" `mA`; "Voltage:" `mV`; each shows `Not connected` on battery.

### 2.7 Sensors

Source: `Modules/Sensors/popup.swift`.

Sections appear only for sensor types that have at least one visible sensor, in this type order: Fans (special), Temperature, Voltage, Current, Power, Energy.

Fans block (first, only if fans exist):

- Header 26 tall: three equal columns; the middle holds "Fans" in system 12 medium `secondaryLabelColor`, top-aligned; the right holds a 15 x 15 "tune" glyph tinted `lightGray`.
- One card per fan, 2 px apart, width 264 - 10, card background, radius 2, padding 5.
- Card row (16 tall, three equal columns): fan name (system 13), an 80 x 8 bar (track `textBackgroundColor` (white / black), 1 px `quaternaryLabelColor` border, radius 2, fill `controlAccentColor` with width = percent), value right-aligned in system 13 (`48%` or `1850 RPM`).
- Fan control rows (mode buttons, slider, min/max buttons) follow on macOS; they are out of scope for Linux.

Each other type:

- Separator with the type name ("Temperature", "Voltage", "Current", "Power", "Energy"), height 30.
- One 22 tall row per sensor, grouped by sensor group in discovery order: name in system 12 `secondaryLabelColor`, value system 13 right-aligned.
- Hover background `rgba(3,3,3,0.05)`, radius 3.
- Clicking a row expands a 60 tall line chart under it (well `lightGray` 10%, radius 3, 120 points, `linear` auto-scale, accent color); clicking again collapses it.
- Values: temperature 1 decimal (`54.3°C`); voltage 3 decimals below 100 (`1.215V`); power, current and energy 2 decimals below 100 (`12.40W`); integers at 100 and above; fans `1850 RPM`.

### 2.8 Bluetooth

Source: `Modules/Bluetooth/popup.swift`.

- Vertical list of device cards, 8 px apart, each 264 x 30, card background, radius 2, horizontal padding 5.
- Device name left in system 13 light; flexible space; one value per battery component (main, left, right, case) in system 12 regular, `N%`, right side.
- Empty state: a 30 tall view with the centered message "No Bluetooth devices are available".

### 2.9 Clock

Source: `Modules/Clock/popup.swift`.

Items are 8 px apart.

Calendar card (on by default), 264 x 232, card background, radius 2, padding 2 top/bottom and 8 left/right:

- Navigation row, height max(item height, 24): "November 2026" in system 16 medium, left; left and right arrow buttons (SF Symbols `arrow.left` / `arrow.right`, `labelColor`), right.
- Item size: width `(264 - 16) / 7 = 35.4` (or `/ 8` with week numbers), height `(264 - 4) / 8 - 4 = 28.5`.
- Weekday header: system 12, `gray`, centered.
- Day cells: system 13 default, `textColor`; days of the adjacent months in `lightGray`.
- Today: 25 px `systemRed` circle centered in the cell, number in white.
- Optional week number column: system 11 `secondaryLabelColor` with a 1 device px right border (`separatorColor` in light, white 40% in dark).

Clock cards, one per configured time zone, 264 x 44, card background, radius 2, padding 5, spacing 5:

- Analog clock 34 x 34: face `controlBackgroundColor` with a `lightGray` (light) / `darkGray` (dark) outline; hour hand 2 wide, length r - 4, `labelColor`, radius 2; minute hand 2 wide, length r - 2, `secondaryLabelColor`; seconds hand 1 wide, length r - 1, plain red; 3 px center pin.
- Text column (spacing 2): name plus `(zone)` in system 11 light; formatted time in system 13 regular.

## 3. Menu-bar widgets

Source: `Kit/Widgets/*.swift`, `Kit/module/widget.swift`.

Common geometry:

| Constant | Value |
|---|---|
| Menu bar height | 22 (system value when available) |
| Drawable height | 22 - 2 * 2 = 18 (widgets sit at y = 2) |
| Horizontal margin inside widgets | 0 |
| Spacing between widgets of one module item | 2, plus 2 before the first and after the last |
| Lines | 1 device px (`1 / scale`) |
| Box / frame radius | 2 |

Vertical label ("Label" option): the first 3 characters of the module title, uppercased, stacked top to bottom, one letter per cell of 6 wide x (18 / 3 = 6) tall, system 7 regular, `textColor`, centered.
When on, the widget gets 6 + 2 extra width on the left (Pie and Tachometer use 8 + 2).

Defaults per module (from each `config.plist`):

| Module | Default widget | Available widgets |
|---|---|---|
| CPU | Mini | Label, Mini, Line chart, Bar chart, Pie chart, Tachometer |
| RAM | Mini | Label, Mini, Line chart, Bar chart (box on, label off), Pie chart, Memory, Tachometer, Text |
| GPU | Mini | Label, Mini, Line chart, Bar chart, Tachometer |
| Disk | Mini (title "SSD") | Label, Mini, Bar chart, Pie chart, Memory, Speed (chars R/W), Network chart, Text |
| Network | Speed (symbols D/U) | Label, Speed, Network chart, State, Text |
| Battery | Battery | Label, Mini (title "BAT", no label), Bar chart, Battery, Battery details |
| Sensors | Mini + Stack | Label, Mini, Stack, Bar chart ("FAN") |
| Bluetooth | Stack | Label ("BLE"), Stack |
| Clock | Stack | Label ("CLK"), Stack |

### 3.1 Mini

| Property | Label on (default) | Label off |
|---|---|---|
| Width | 31 | 36 |
| Label | module title, system 7 light, at y = 12, height 7, alignment left (setting: left / center / right), white in dark, `textColor` in light | none |
| Value | `pct(v)` + suffix, system 12 regular, at y = 1 | system 14 regular, centered, y = (22 - 14) / 2 |
| Value color | default monochrome; options: system accent, utilization (zones), pressure (RAM), any named color | same |

### 3.2 Line chart

- Chart width by history length: 30 points = 24, 60 (default) = 32, 90 = 42, 120 = 52.
- Box (default on): rounded rect radius 2 filled black in light mode and white in dark mode, with a 1 device px outline of the same color.
- With the box on, the chart fill uses the series color and the monochrome color inverts (white in light, black in dark) so it stays visible.
- Frame (alternative to box): outline only, transparent inside.
- Chart: same drawing as the popup line chart (section 1.5), inset by 1 device px inside the box; color default accent (CPU, GPU, RAM config), or utilization / pressure / monochrome / named.
- Value option: `pct(v)` in system 8 regular, right-aligned at the top of the box (y = 18 - 7); the chart then shrinks to the bottom 10 px (9 on non-retina).
- Value color is white/black, or the chart color when "colorize value" is on.
- Scale options: none, linear, square, cube, logarithmic, fixed.

### 3.3 Bar chart

- Width by number of bars: 1 bar = 10 (+1 device px), 2 = 22, 3-4 = 30, 5-8 = 40, 9-12 = 50, 13-16 = 76, 17-32 = 84, more = 118.
- Box (default on): same black / white rounded box as the line chart; bars drawn inside, 0.5 gap between bars, square ends, bars grow from the bottom, height = 18 * value.
- Stacked values per bar are drawn bottom-up (RAM app / wired / compressed, CPU system / user).
- Colors: accent by default; utilization, pressure, monochrome (inverted inside the box), named, or "cluster" (CPU per core type: teal / indigo).
- CPU bar content: total usage (1 bar), per core (setting), system + user stacked, or E/P cluster averages.

### 3.4 Pie chart

- Size 18 + 0 = 18 square (the drawable height), filled pie (no hole), segments clockwise from 12 o'clock, remainder `lightGray` at 50%.
- CPU: system red + user blue; RAM: app blue + wired orange + compressed pink; Disk: used `systemBlue`.
- "Monochrome accent" converts segment colors to grayscale.

### 3.5 Tachometer

- Same 18 px footprint as the pie; draws a filled upper half disk (`TachometerGraphView`), segments left to right, remainder `lightGray` at 50%, shifted down 4 px.
- CPU: system + user; RAM: app + wired + compressed; GPU: utilization in `systemBlue`.

### 3.6 Network chart

- Chart width by history: 30 = 22, 60 (default) = 30, 90 = 40, 120 = 50; height 18.
- Two mirrored polylines around the horizontal center: upload on top, download below (reverse order swaps).
- Each half scales to its own max over the window (`linear` by default, zero value 256 for logarithmic).
- Line 1 device px; area between line and center filled with the line color at 50% alpha.
- Default colors: download `systemBlue`, upload `systemRed`; box and frame default off.

### 3.7 Speed

Default mode is two rows, icon "dots", values on, units on, value color "none" (`textColor`), icon color "default".

| Mode | Layout |
|---|---|
| Two rows (default) | Icon column 7 wide on the left (icon alignment left), then a value column 48 wide with units (30 without). Each row is 9 tall; values are system 9 light, right-aligned (setting). Default order "oi": upload (output) on the top row, download (input) on the bottom row. Total width = 7 + 48 = 55. |
| One row | Each item: icon, 2 px gap, value box 58 wide (32 without units) with system 11 regular text; items separated by 6. |

Icons:

- Dots: 6 px circles in two-row mode (centered on each row), 8 px in one-row mode.
- Arrows: 1 px stroked arrow, height 80% of 18, head length 3 + scale / 2 at 36 degrees; down arrow for download / read, up arrow for upload / write.
- Characters: the symbol letter (D / U for network, R / W for disk) in system 12 regular, 10 wide.

Icon color modes: "default" uses the series color only when the rate is at least 1024 B/s and `textColor` otherwise; "transparent" hides the icon when idle; "constant" always uses the series color; "none" is always `textColor`.
Value color modes behave the same way; default "none".
Monochrome option replaces the series colors with grays (`MonochromeColor`).

### 3.8 Battery

- Battery body 22 x 12 (XL: 26 x 14), outline 1 px in `textColor` at 50% alpha, radius 2 (XL 3), at the vertical center.
- Terminal nub 3 x 4, radius 2, at the right edge, separated from the body by a 1 px punched gap.
- Fill inset 1.5 px, radius 1 (XL 2), width = max(1, (body width - 3) * level), color `batteryColor(colorize)` (default `textColor`, red under 20%).
- Charger glyph drawn centered inside the body while on AC (default): a lightning bolt 9 x 18 when charging, a plug 9 x 14 when plugged in but not charging; drawn knocked out of the fill.
- Additional info (default none), drawn left of the icon with 2 px spacing: percentage or time in system 12 regular; "percentage and time" / "time and percentage" in two rows of system 9 regular, centered; "percentage inside the icon" draws the number in system 8 bold knocked out of the fill.
- Additional info is hidden at 100% by default.
- If the level is unknown, a `?` in system 11 is drawn inside the body.

Battery details widget: text only, `pct` or time in system 12 (one row) or both in two rows of system 9.

### 3.9 Stack (Sensors, Bluetooth, Clock)

- Mode auto (default): values are paired into two-row columns; a single leftover value is drawn as one row.
- One row: system 13 regular (monospaced digits optional), width = text width + 2 (or fixed 45 with "static width").
- Two rows: system 10 light, each row 9 tall, column width = max(20, widest text) + 2 (fixed 32 with static width).
- Columns are separated by 2, with 2 px padding at both ends; text color `textColor`; alignment left by default.
- Sensor values use the mini format: temperature without the unit letter (`54°`), other kinds 1 decimal under 9.95.

### 3.10 Memory

- Two rows, system 9 light, right-aligned, value column 50 wide.
- Top row free, bottom row used (reversible); optional symbols "F:" and "U:" in an 8 px column plus 4 px gap.
- Color default monochrome; utilization colors free by `1 - used` and used by `used`.

### 3.11 Label, State, Text

- Label: width 6, the vertical 3-letter label only.
- State: 8 px circle, vertically centered; active `systemGreen`, inactive `systemRed` (Network: connected or not).
- Text: user template string, system 12 regular `textColor`, width rounded up to the nearest 10.

### 3.12 Empty state

When a widget has no data it draws a 15 px stroked circle with a diagonal slash (1 device px, `textColor`).

## 4. Data mapping to readoutd

Status values: "available" (a daemon key exists), "derivable" (computable from daemon data or client state, how is given), "not available" (hide the row or element).
Daemon percents are 0..100; divide by 100 for the fractions used above.
Keys marked [detail] in `docs/dbus-api.md` need the detail tier, which frontends should request only while the popup is open.

### 4.1 CPU

| Stats element | Status | Source / rule |
|---|---|---|
| Usage ring system / user | available | `cpu.system`, `cpu.user` |
| Idle row and ring remainder | derivable | `100 - cpu.usage` (ioWait is part of usage) |
| Temperature gauge | available | `cpu.temperature`, else `sensors.cpuTemperature` |
| Frequency gauge | available | `cpu.frequency` [detail]; use `GetInfo().cpu.maxFrequency` as the 100% reference instead of max-seen |
| Usage history | available | `GetHistory("cpu").usage`, then `cpu.usage` per update |
| Per-core LED bars | available | `cpu.cores` [detail] |
| Core colors teal / indigo | derivable | `GetInfo().cpu.coreTypes` (hybrid only); else `systemBlue` |
| Scheduler limit, Speed limit | not available | macOS power management only; hide |
| Efficiency / Performance cores rows | available | `efficiencyUsage`, `performanceUsage` [detail], hybrid only |
| Uptime | available | `cpu.uptime` [detail] |
| Average load | available | `load1`, `load5`, `load15` [detail]; show 2 decimals |
| Frequency rows E / P | derivable | average of `cpu.frequencies` grouped by `coreTypes` |
| Top processes | available | `cpu.processes` (`cpu` percent of one core) |
| Process icons | derivable | match process name to a `.desktop` entry icon; fallback to a generic executable icon |

### 4.2 RAM

| Stats element | Status | Source / rule |
|---|---|---|
| Usage ring and percent | available | `memory.usage` |
| App segment | available | `memory.app` [detail] |
| Wired segment | derivable | no wired memory on Linux; show "Kernel" = max(0, `used - app - compressed`) with the same orange |
| Compressed segment | available | `memory.compressed` [detail] (zram + zswap) |
| Free remainder / row | available | `memory.free` [detail] |
| Used, Swap rows | available | `memory.used`, `memory.swapUsed` |
| Pressure gauge | available | `memory.pressureLevel` [detail] when `GetInfo().memory.hasPressure`; hide otherwise |
| Pressure level number | derivable | normal 1, warning 2, critical 4, or show `round(pressureSome)` |
| Usage history | available | `GetHistory("memory").usage` |
| Top processes | available | `memory.processes` (`memory` resident bytes) |

### 4.3 GPU

| Stats element | Status | Source / rule |
|---|---|---|
| Card per GPU, model name | available | `gpus[].name` |
| Active / inactive dot | derivable | green when `state == "active"`, red when `suspended`, gray (`lightGray`) when `idle` |
| Utilization gauge and chart | available | `gpus[].usage`; history from `GetHistory("gpu")["<id>/usage"]` |
| Temperature gauge and chart | available | `gpus[].temperature`; chart history kept client-side |
| Render / Tiler utilization | not available | Apple GPU counters; hide those gauges and charts |
| Vendor | available | `GetInfo().gpu.gpus[].vendor` |
| Cores | not available | hide |
| Fan speed | available | `fanPercent` (or `fanRpm`) |
| Core clock, Memory clock | available | `coreClock`, `memoryClock` |
| Extra rows worth adding | available | `memoryUsed / memoryTotal` (VRAM), `power` |

### 4.4 Disk

| Stats element | Status | Source / rule |
|---|---|---|
| Drive card name, size, free | available | `disk.mounts[]` [detail] joined to `disk.disks[]` by `disk`; panel tier has the root filesystem only |
| Read / write dots | available | `disks[].read`, `disks[].write` |
| Per-drive activity chart | derivable | `GetHistory("disk")` is the all-disks total; per-drive history must be accumulated client-side while the popup is open |
| Capacity bar and legend | available | `mounts[].used / total`, `free` |
| Speed rows, Total read / written | available | `disks[].read/write`, `totalRead`, `totalWritten` |
| SMART Temperature | available | `disks[].temperature` |
| SMART Health percent | derivable | show `OK` / `Failing` from `healthy` instead of a percent |
| SMART Power on hours | available | `disks[].powerOnHours` |
| SMART Power cycles, lifetime totals | not available | hide |
| Top processes read / write | available | `disk.processes` (own user only) |

### 4.5 Network

| Stats element | Status | Source / rule |
|---|---|---|
| Dashboard download / upload | available | `network.download`, `network.upload` |
| Usage history | available | `GetHistory("network")` `download`, `upload` |
| Connectivity history grid | derivable | client appends `connectivity == "full"` each update (green) or not (red) into a 90-cell ring buffer; no daemon history |
| Total upload / download | available | `totalUploaded`, `totalDownloaded` (since the interface came up); the reset button is derivable by storing an offset client-side |
| Status | available | `connected` |
| Internet connection | available | `connectivity` (`full` = UP, `none` / `limited` / `portal` = DOWN, else Unknown) |
| Latency, Jitter | not available | hide |
| Interface | available | `interface` plus `connectionName` [detail], e.g. `Wi-Fi (wlp2s0)` |
| Physical address | available | `interfaces[].mac` [detail] |
| Network (SSID + signal) | available | `ssid`, `signal`; signal is a percent, so show `SSID (72%)` instead of RSSI |
| Standard | not available | hide |
| Channel | derivable | from `wifi.frequency` MHz: 2.4 GHz `(f - 2407) / 5`, 5 GHz `(f - 5000) / 5`, 6 GHz `(f - 5950) / 5`; append `wifi.band` |
| Speed | available for Wi-Fi | `wifi.bitrate` Mbit/s; Ethernet link speed not available |
| DNS servers | not available | hide |
| Local IP | available | `interfaces[].ipv4[0]`, else `ipv6[0]` |
| Public IP, country flag | not available | hide the two rows (keep "Local IP") |
| Top processes | not available | no per-process network data; hide the section |

### 4.6 Battery

| Stats element | Status | Source / rule |
|---|---|---|
| Dashboard icon, Level | available | `battery.percentage` |
| Source | available | `externalPower` (`AC Power` / `Battery Power`) |
| Time to discharge / charge | available | `timeToEmpty`, `timeToFull` seconds; 0 means `Calculating`; `state == "full"` means `Fully charged` |
| Last charge | derivable | remember when `externalPower` last went false; unknown until that happens in the session |
| Health | available | `batteries[].health` [detail] |
| Capacity | available | `energy / energyFull / energyFullDesign` in Wh instead of mAh |
| Cycles | available | `batteries[].cycles`, hide when -1 |
| Temperature, Voltage, Power | available | `batteries[].temperature`, `voltage`, `power` |
| Current | derivable | `power / voltage * 1000` mA |
| Is charging | available | `state == "charging"` |
| Adapter power, current, voltage | not available | show only "Is charging" in the Power adapter section |
| Top processes (energy) | not available | hide |
| Peripheral batteries | available | `battery.peripherals` [detail]; can feed the Bluetooth popup |

### 4.7 Sensors

| Stats element | Status | Source / rule |
|---|---|---|
| Type sections and rows | available | `sensors.sensors` grouped by `kind` then `group`; section titles from `kind` |
| Row values and units | available | `value`, `unit`; skip entries with `valid == false` |
| Expandable row chart | derivable | client-side history per sensor while the popup is open |
| Fan rows (value) | available | `kind == "fan"`, RPM |
| Fan bar percent | derivable | only with a known max RPM; otherwise draw the value without a bar |
| Fan control | not available | hide |
| Energy section | not available | hide |

### 4.8 Bluetooth and Clock

| Stats element | Status | Source / rule |
|---|---|---|
| Device cards | available | `bluetooth.devices[]` (`name`, `connected`) |
| Battery levels | available | `devices[].battery` (single value; multi-part left / right / case not available) |
| Clock popup and widget | derivable | computed in the frontend from local time and time zones; no daemon module |

### 4.9 Widgets

| Widget value | Status | Source / rule |
|---|---|---|
| Mini / line / bar / tachometer values | available | panel tier `usage` per module |
| Mini "pressure" color | derivable | `pressureLevel` is detail-only, so fall back to utilization color in the panel tier |
| CPU per-core bars | available | panel tier with the `cores` option |
| RAM stacked bars, pie, tachometer | derivable | app / compressed are detail-only; in the panel tier draw a single `usage` segment |
| Speed, network chart | available | `network.download/upload`, `disk.read/write` |
| Battery widget | available | `percentage`, `externalPower`, `state`, `timeToEmpty`, `timeToFull` |
| Stack (sensors) | available | panel tier `sensors` for the ids passed in `options.sensors` |
| State (network) | available | `network.connected` |

## 5. The defining traits

1. A narrow fixed-width popup (264 content + 8 margins = 280) with a 42 px header: centered 16 pt title flanked by two light-gray icon buttons.
2. Section titles are small centered gray text (12 pt medium, secondary color, 30 px tall) with no rules or uppercase.
3. Dense 22 px key/value rows: 12 pt gray label with a trailing colon on the left, 13 pt primary value right-aligned.
4. Legend squares: 12 x 12 rounded squares (radius 2) at the start of rows tie each row to a chart series.
5. A 90 px dashboard at the top of CPU, RAM, Network and Battery: thin 7 px rings and 270 degree gauges with centered text, big light 26 pt numbers for network.
6. Charts sit in faint wells (light gray at 10%, radius 3) with no axes or grid; line charts have a 1 device px stroke and a solid gradient fill.
7. Mirrored up/down charts for network and disk: upload red above the center line, download / read blue below.
8. A fixed palette: system blue / red / orange / pink / green / teal / indigo, with the user accent for single-series charts.
9. Cards for multi-instance content (GPUs, drives, fans, Bluetooth, clocks): flat `#F5F5F5` in light, near-transparent dark in dark mode, radius 2, 8 px apart.
10. Menu-bar widgets are tiny (18 px drawable height): 7 pt labels stacked vertically, 9-14 pt values, and black/white filled boxes behind charts.

## 6. Readout adaptation

Sections 1 to 5 describe Stats.
This section is binding for both Readout frontends and wins where it differs from sections 1 to 5.

### 6.1 Scale

Stats sizes are macOS points at a 13 pt body font.
Readout scales every Stats number by `s = body font pixel size / 13`, rounded to whole pixels at the end.

| Stats value | Plasma | GNOME |
|---|---|---|
| Body text 13 (values) | default font | 1em |
| Label text 12 | 0.92 of the default font | 0.92em |
| Small text 11 / 10 / 9 | 0.85 / 0.77 / 0.7 of the default font | same in em |
| Title 16 | 1.23 of the default font | 1.23em |
| Popup content width 264 | `264 * s` (about 17 grid units) | `264 * s` px |
| Row 22, separator 30, dashboard 90 | `* s` | `* s` |
| Ring 70, gauge 50, stroke 7 | `* s` | `* s` |

Panel widgets: Stats draws them in an 18 px drawable height.
Readout uses `h = min(panel thickness - 4, 26 * scale)` as the drawable height and scales every widget number in section 3 by `h / 18`.
In a vertical panel, `h` is the panel width divided by 2 for text styles, and the widget is laid out to fit the width.

### 6.2 Colors

- Series colors use the Stats palette from section 1.6, picking the light or dark column by the brightness of the popup background.
- The accent (`controlAccentColor`) is the desktop accent.
- Text, label (`secondaryLabelColor`), background and separators come from the desktop theme, never hard-coded.
  Label color = text color at 55% opacity; tertiary = text color at 30%.
  Plasma uses 70% and 45%, because its popups are translucent and blurred and the Stats values read too faint over busy windows.
- An accent that barely differs in brightness from the background (some Plasma themes report a near-black highlight) is replaced by `systemBlue`.
- Wells: `lightGray` at 10% (as Stats).
- Cards: light themes use the text color at 4% over the background; dark themes use black at 25%.
- "Inactive" colors (`lightGray` at 50%) are blended into an opaque color where a chart library cannot draw alpha.

### 6.3 Defaults

- Color mode default is `accent`, as in Stats: charts and bars use the accent, panel text is monochrome.
- `thresholds` uses the Stats utilization colors: accent (or `systemBlue`) at or below the warning threshold, plain orange up to the critical threshold, plain red above it.
  The warning and critical settings stay at 70% and 90%.
- `monochrome` uses the text color for everything.
- Battery levels use `batteryColor` (section 1.6) in every mode.
- New panel style `memory` (section 3.10) for the memory and disk modules: two rows, "F:" free and "U:" used.
- `showLabel` on chart styles (line, bars, pie, tachometer, dualLine) draws the vertical three-letter label (section 3, "Vertical label").
  Mini keeps its label above the value; speed and state never show a label.

### 6.4 Popup frame

- Header (42 * s): left button opens the system monitor (Plasma: `plasma-systemmonitor`, else `ksysguard`; GNOME: `gnome-system-monitor`), centered title, right button opens this item's settings.
- The popup is exactly as tall as its content, up to 70% of the available screen height; only above that does the body scroll.
- A page has only panel-tier data until its first detail update, about 500 ms after it opens.
  Meanwhile the popup keeps the height that page had the last time it showed detail data (saved per item and page), so it opens at its final size instead of growing.
- Section titles differ from Stats (1.3): each has a gap of 6 * s above it (scaled by the spacing setting), and a 1 px rule in the text color at 15% runs on each side of the title, 8 * s from it.
  The rules start at the left edge, so they line up across sections; when the title has an action button, the right rule stops 8 * s before it, and the title stays centered.
- "Top processes" always has the configured number of rows, blank when fewer processes are reported (as Stats does), so the height does not change as processes come and go.
- Content margin: 8 * s by default (user setting, docs/frontend-spec.md "Popup look"), measured from the visible edge of the popup frame.
  On Plasma, a theme whose `dialogs/background` declares no inset pads the content with its frame margins; that padding is subtracted from the margin so the gap is the same on every theme.
- A combined item (several modules) shows one compact summary per module, stacked like the Stats "Combined modules" view.
  Each summary has a header row with the module name on the left and a "›" button on the right that replaces the body with that module's full page; the popup header then shows a back button in place of the left button.
- Compact summary layout: a 70 * s ring (or the battery icon) on the left, up to four key/value rows on the right.
  Network and disk add a 40 * s mirrored chart under the rows.

| Module | Ring | Rows |
|---|---|---|
| cpu | system + user | Usage, Idle, and on hybrid CPUs E-cores, P-cores |
| gpu | usage | Usage, Temperature, Memory used |
| memory | app + kernel + compressed (panel tier: usage) | Used, Free, Swap, Pressure |
| disk | used percent of the root filesystem | Name, Used, Free; then Write and Read rows under the chart |
| network | none | Upload, Download, then the chart, then Local IP |
| sensors | none | the selected sensors (or CPU and GPU temperature) |
| battery | battery icon | Level, Status, Health |
| bluetooth | none | connected devices with battery |
| clock | none | local time and each time zone |

### 6.5 Linux content decisions

- Hidden because Linux has no source: CPU scheduler and speed limits; GPU render and tiler utilization and core count; network latency, jitter, Wi-Fi standard and DNS servers; per-process network traffic (no network "Top processes"); SMART power cycles; battery adapter watts, current and voltage; battery energy per process; fan control.
- RAM "Wired" becomes "Kernel" = max(0, used - app - compressed), in `systemOrange`.
- Public IP stays as one "Public IP:" row whose value is a "Show" link until clicked (privacy: no request before the click).
- Disk: one card per physical drive, titled with the model (or device name).
  Each mounted filesystem on the drive gets its own capacity bar and legend row inside the card.
  The activity chart uses the daemon's per-drive history (`GetHistory("disk")` series `<disk>/read` and `<disk>/write`).
- Clients fetch a module's history again whenever its popup opens, so charts have no gap from the time spent in the panel tier.
- Connectivity history: each frontend item keeps a 90-cell ring buffer of `connectivity == "full"` (or `connected` when NetworkManager is absent), appended on every network update.
- GPU temperature chart and sensor row charts use client-side history.
- GPU page: Readout follows the newer Stats layout (usage ring, "Usage history", "Details") instead of the cards of section 2.3, since Linux has no render or tiler utilization to fill the card gauges.
  Per GPU: a dashboard (section 2.1 positions) with the 70 * s usage ring in the middle, a 50 * s temperature gauge on the left and a 50 * s gauge on the right, each side gauge with a 9 pt caption under it.
  The right gauge shows video memory used when known (amdgpu, NVIDIA), else power against the power limit (xe, i915), else the core clock against its maximum.
  Then "Usage history" (70 * s, daemon history), "Temperature history" (50 * s, `systemOrange`, client-side) and "Details": Model, Vendor, Driver, Status, Utilization, Temperature, Memory with a capacity bar, Core clock, Memory clock, Max core clock, Power, Power limit, Fan speed, PCI slot.
  With several GPUs each section starts with a heading row: the name and the status dot.
  A sleeping GPU shows only its message and the static rows.
- Sensors: rows show the sensor label, then the chip name in tertiary color; type sections in the order Fans, Temperature, Voltage, Current, Power.
  Fans show RPM without a bar (no maximum is known).
- Battery capacity is shown in Wh: `energy / energyFull / energyFullDesign Wh`; Current = power / voltage.
- "Last charge" is the time since external power was last removed during this session; hidden until known.
- Clock popup: calendar card (Stats 2.9), then a "Local time" card, then one card per configured zone, each with the analog clock.
- Process icons: look up the process name as an icon or desktop entry; fall back to a generic executable icon.
- No Battery: the popup shows "This computer has no battery." centered, then peripheral batteries (if any) as cards.
