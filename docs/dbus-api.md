# readoutd D-Bus API (version 1)

`readoutd` runs on the session bus and is started on demand by D-Bus activation.
It exits 30 seconds after its last subscriber leaves.

| Item | Value |
|---|---|
| Bus name | `io.github.aakash1122.Readout1` |
| Object path | `/io/github/aakash1122/Readout1` |
| Interface | `io.github.aakash1122.Readout1` |

Breaking changes get a new interface (`Readout2`). New keys may be added to any dictionary at any time, so clients must ignore keys they do not know.

## Methods

### `GetInfo() -> a{sv}`

Static facts. Call it once at startup and again when the service restarts.

| Key | Type | Meaning |
|---|---|---|
| `version` | s | Daemon version |
| `modules` | as | Module names |
| `cpu`, `memory`, ... | a{sv} | Per-module info, listed below |

### `Subscribe(a{ss} modules, u interval_ms, a{sv} options)`

Sets (replaces) the caller's subscription.

- `modules`: module name to tier. Tiers are `"none"`, `"panel"` and `"detail"`.
  Use `panel` for what a panel widget draws.
  Use `detail` only while a popup is open, because it costs more (per-core data, process lists, SMART, sensors on slow buses).
- `interval_ms`: update interval, clamped to 500..60000.
- `options`:
  - `sensors` (as): sensor ids the panel shows. The sensors module reads only these plus the CPU and GPU summary sensors in the panel tier.
  - `cores` (b): also send `cpu.cores` in the panel tier, for per-core panel bars. Default false.

Subscribing with every module set to `none` is the same as `Unsubscribe()`.
A subscription is dropped automatically when the caller disconnects from the bus.
Errors: `org.freedesktop.DBus.Error.InvalidArgs` for an unknown module or tier, `org.freedesktop.DBus.Error.LimitsExceeded` past 64 subscribers.

After a subscribe that raises a module's tier, the first `Update` arrives after about 500 ms so that rates already have a baseline.

### `Unsubscribe()`

Drops the caller's subscription.

### `GetHistory(s module) -> a{sa(td)}`

Recent values per series, oldest first, up to 180 points.
Each point is `(timestamp, value)` where `timestamp` is `CLOCK_MONOTONIC` in microseconds (the same clock as `GLib.get_monotonic_time()` and `QElapsedTimer`).
History only accumulates while the module is subscribed.

| Module | Series |
|---|---|
| cpu | `usage` |
| memory | `usage` |
| network | `download`, `upload` (bytes/s, primary interface) |
| disk | `read`, `write` (bytes/s, all physical disks), and `<disk name>/read`, `<disk name>/write` per physical disk |
| gpu | `<gpu id>/usage` |
| battery | `level` |

## Signal

### `Update(t timestamp, a{sa{sv}} modules)`

Sent to each subscriber on its interval, unicast (only the subscriber receives it).
`timestamp` is `CLOCK_MONOTONIC` in microseconds.
`modules` maps each subscribed module name to its payload.

## Properties

| Name | Type | Meaning |
|---|---|---|
| `Version` | s | Daemon version |
| `SubscriberCount` | u | Number of subscribers (no change signal) |

## Units

| Quantity | Unit |
|---|---|
| Percent | 0..100 (double) |
| Rates | bytes per second |
| Sizes | bytes (uint64) |
| Temperature | degrees Celsius |
| Frequency | MHz |
| Power | watts |
| Time spans | seconds |

A key that is absent means "not available on this machine". Clients show a dash or hide the row.

## Payloads

`[detail]` marks keys sent only in the detail tier.

### cpu

| Key | Type | Meaning |
|---|---|---|
| `usage` | d | Total usage, percent |
| `user`, `system`, `ioWait` | d | Breakdown, percent |
| `temperature` | d | CPU package/die temperature (absent if unknown) |
| `cores` | ad | [detail] Usage per logical CPU (also in the panel tier with the `cores` option) |
| `performanceUsage`, `efficiencyUsage` | d | [detail] Average per core type, hybrid CPUs only |
| `load1`, `load5`, `load15` | d | [detail] Load averages |
| `processCount` | t | [detail] Number of processes |
| `threadCount` | t | [detail] Number of threads |
| `uptime` | d | [detail] Seconds since boot |
| `frequency` | d | [detail] Average current frequency, MHz (refreshed every 2 s at most) |
| `frequencies` | ad | [detail] Per logical CPU, MHz |
| `processes` | aa{sv} | [detail] Top 10 by CPU: `pid` (x), `name` (s), `cpu` (d, percent of one core) |

Info: `model` (s), `logicalCores` (t), `physicalCores` (t), `hybrid` (b), `coreTypes` (as, hybrid only: `performance`/`efficiency`), `maxFrequency` (d), `temperatureSource` (s).

### memory

| Key | Type | Meaning |
|---|---|---|
| `total`, `used`, `available` | t | `used = total - available` (the kernel's definition) |
| `usage` | d | Used, percent |
| `swapTotal`, `swapUsed` | t | |
| `free` | t | [detail] Completely unused |
| `app` | t | [detail] Anonymous memory of applications |
| `cache` | t | [detail] Page cache + reclaimable slab - shared |
| `buffers`, `shared`, `dirty` | t | [detail] |
| `compressed` | t | [detail] Memory used by zram and zswap |
| `compressedOriginal` | t | [detail] Data stored compressed, before compression |
| `pressureSome`, `pressureFull` | d | [detail] PSI 10 s averages, percent |
| `pressureLevel` | s | [detail] `normal`, `warning`, `critical` |
| `processes` | aa{sv} | [detail] Top 10 by memory: `pid` (x), `name` (s), `memory` (t, resident bytes) |

Info: `total` (t), `swapTotal` (t), `hasPressure` (b).

### network

| Key | Type | Meaning |
|---|---|---|
| `interface` | s | Primary interface (NetworkManager's primary connection, else the default route) |
| `download`, `upload` | d | Primary interface rates |
| `totalDownloaded`, `totalUploaded` | t | Primary interface counters since it came up |
| `connected` | b | Primary interface is up |
| `connectivity` | s | `full`, `limited`, `portal`, `none`, `unknown` (NetworkManager) |
| `connectionType` | s | `ethernet`, `wifi`, `vpn`, `other`, or the interface kind |
| `ssid`, `signal` | s, d | Wi-Fi only; signal in percent |
| `connectionName` | s | [detail] NetworkManager connection name |
| `wifi` | a{sv} | [detail] `ssid`, `signal`, `frequency` (MHz), `bitrate` (Mbit/s), `security`, `band` |
| `interfaces` | aa{sv} | [detail] `name`, `kind` (`ethernet`/`wifi`/`virtual`), `up`, `download`, `upload`, `totalDownloaded`, `totalUploaded`, `mac`, `ipv4` (as), `ipv6` (as) |

Virtual interfaces are listed only while up with an address (VPNs, Tailscale), so container bridges stay hidden.

Info: `interfaces` (as).

### disk

| Key | Type | Meaning |
|---|---|---|
| `read`, `write` | d | All physical disks |
| `mountPoint`, `total`, `used`, `free`, `usage` | s, t, t, t, d | The root filesystem (or the first real mount) |
| `disks` | aa{sv} | [detail] `name`, `model`, `size`, `removable`, `rotational`, `read`, `write`, `busy` (percent), `totalRead`, `totalWritten`, `temperature`, `healthy` (b, SMART), `powerOnHours` |
| `mounts` | aa{sv} | [detail] `mountPoint`, `fsType`, `disk`, `total`, `used`, `free` |
| `processes` | aa{sv} | [detail] Top 10 active by I/O: `pid`, `name`, `read`, `write`. Only the user's own processes are visible. |

Capacity refreshes every 30 s and whenever the mount table changes.

Info: `disks` (aa{sv}: `name`, `model`, `size`, `removable`, `rotational`).

### gpu

| Key | Type | Meaning |
|---|---|---|
| `gpus` | aa{sv} | One entry per GPU |

GPU entry: `id` (PCI slot), `name`, `state` (`active`, `idle` = not sampled at this tier, `suspended`), `secondary` (b), `usage`, `temperature`, `memoryUsed`, `memoryTotal`, `coreClock`, `maxCoreClock` (MHz, the highest boost clock), `memoryClock`, `power`, `powerLimit` (W, the sustained power limit), `fanRpm`, `fanPercent`.

A suspended GPU is never woken. A secondary GPU (not the boot display device) is only sampled in the detail tier, so in the panel tier it shows `state: "idle"` or `"suspended"`.

Info: `gpus` (aa{sv}: `id`, `name`, `driver`, `vendor`, `secondary`).

### sensors

| Key | Type | Meaning |
|---|---|---|
| `cpuTemperature`, `gpuTemperature` | d | Summary sensors |
| `maxTemperature` | d | Highest temperature read this tick |
| `sensors` | aa{sv} | Panel: the subscriber's selected sensors. Detail: all sensors. |

Sensor entry: `id`, `label`, `chip`, `group` (`CPU`, `GPU`, `Storage`, `Memory`, `Wireless`, `Network`, `Power`, `System`), `kind` (`temperature`, `voltage`, `power`, `current`, `fan`), `unit`, `valid` (b), `sleeping` (b), `value` (d).

Sensor ids look like `k10temp@0000:00:18.3/temp1`. They stay stable across reboots.

Info: `sensors` (aa{sv}: `id`, `label`, `chip`, `group`, `kind`, `unit`).

### battery

| Key | Type | Meaning |
|---|---|---|
| `available` | b | UPower is running |
| `present` | b | A system battery exists |
| `externalPower` | b | On AC power |
| `percentage`, `state`, `timeToEmpty`, `timeToFull`, `power` | d, s, x, x, d | Combined battery; state is `charging`, `discharging`, `full`, `empty`, `pending-charge`, `pending-discharge`, `unknown` |
| `batteries` | aa{sv} | [detail] `vendor`, `model`, `percentage`, `state`, `energy`, `energyFull`, `energyFullDesign` (Wh), `health` (percent), `cycles` (x, -1 unknown), `voltage`, `power`, `temperature`, `technology`, `timeToEmpty`, `timeToFull` |
| `peripherals` | aa{sv} | [detail] `model`, `kind` (`mouse`, `keyboard`, `headset`, ...), `percentage`, `state` |

Info: `hasBattery` (b).

### bluetooth

| Key | Type | Meaning |
|---|---|---|
| `available` | b | BlueZ is running with an adapter |
| `powered` | b | An adapter is powered |
| `connectedCount` | t | |
| `devices` | aa{sv} | Paired or connected devices, connected first: `name`, `address`, `icon`, `connected`, `paired`, `battery` (d, if reported) |

## Client checklist

1. Watch the bus name (with auto-start). When it gains an owner, call `GetInfo()` and `Subscribe()` again.
2. Subscribe at the `panel` tier. Switch a module to `detail` only while its popup is open, then back.
3. Call `GetHistory()` when a chart first appears.
4. Match the `Update` signal by sender, path and interface. It is unicast, so it never reaches other clients.
