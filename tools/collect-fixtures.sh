#!/usr/bin/env bash
# Copies the /proc and /sys files readoutd parses into daemon/tests/fixtures/<name>/.
# Run it on every machine you can, then add the directory to the test suite.
#
#   tools/collect-fixtures.sh <machine-name>
#
# Only kernel statistics are copied. Nothing personal (no cmdlines, no environment).
set -euo pipefail

if [[ $# -ne 1 || ! "$1" =~ ^[a-z0-9-]+$ ]]; then
    echo "usage: $0 <machine-name>   (lowercase letters, digits, dashes)" >&2
    exit 1
fi

root="$(cd "$(dirname "$0")/.." && pwd)"
target="$root/daemon/tests/fixtures/$1"
mkdir -p "$target/proc/pressure" "$target/sys"

copy_file() {
    local source="$1" destination="$2"
    mkdir -p "$(dirname "$destination")"
    cat "$source" > "$destination" 2>/dev/null || rm -f "$destination"
}

for file in stat meminfo loadavg uptime cpuinfo diskstats net/dev net/route self/mountinfo; do
    copy_file "/proc/$file" "$target/proc/$file"
done
for file in /proc/pressure/*; do
    copy_file "$file" "$target/proc/pressure/$(basename "$file")"
done

# CPU topology and frequency (values only).
for cpu in /sys/devices/system/cpu/cpu[0-9]*; do
    name="$(basename "$cpu")"
    for file in topology/core_id topology/physical_package_id cpufreq/scaling_cur_freq \
                cpufreq/cpuinfo_avg_freq cpufreq/cpuinfo_max_freq cpu_capacity; do
        [[ -r "$cpu/$file" ]] && copy_file "$cpu/$file" "$target/sys/devices/system/cpu/$name/$file"
    done
done
for list in /sys/devices/cpu_core/cpus /sys/devices/cpu_atom/cpus; do
    [[ -r "$list" ]] && copy_file "$list" "$target${list}"
done

# hwmon chips: names, labels and inputs. The device link is recorded as a plain file.
for chip in /sys/class/hwmon/hwmon*; do
    name="$(basename "$chip")"
    destination="$target/sys/class/hwmon/$name"
    mkdir -p "$destination"
    for file in "$chip"/name "$chip"/*_label "$chip"/*_input "$chip"/power*_average; do
        [[ -r "$file" ]] && copy_file "$file" "$destination/$(basename "$file")"
    done
    if [[ -e "$chip/device" ]]; then
        readlink -f "$chip/device" > "$destination/device.link"
    fi
done

# Block devices.
for disk in /sys/block/*; do
    name="$(basename "$disk")"
    destination="$target/sys/block/$name"
    mkdir -p "$destination"
    for file in dev size removable queue/rotational device/model mm_stat; do
        [[ -r "$disk/$file" ]] && copy_file "$disk/$file" "$destination/$file"
    done
done

# DRM cards: PCI identity and the statistics files readoutd reads.
for card in /sys/class/drm/card[0-9]; do
    [[ -e "$card/device" ]] || continue
    name="$(basename "$card")"
    device="$(readlink -f "$card/device")"
    destination="$target/sys/class/drm/$name/device"
    mkdir -p "$destination"
    for file in vendor device class boot_vga power/runtime_status gpu_busy_percent \
                mem_info_vram_used mem_info_vram_total; do
        [[ -r "$device/$file" ]] && copy_file "$device/$file" "$destination/$file"
    done
    basename "$(readlink -f "$device/driver")" > "$destination/driver.name"
    for gt in "$device"/tile*/gt*; do
        [[ -d "$gt/gtidle" ]] || continue
        relative="${gt#"$device"/}"
        for file in gtidle/name gtidle/idle_residency_ms freq0/act_freq; do
            [[ -r "$gt/$file" ]] && copy_file "$gt/$file" "$destination/$relative/$file"
        done
    done
done

# Power supplies.
for supply in /sys/class/power_supply/*; do
    [[ -e "$supply" ]] || continue
    name="$(basename "$supply")"
    for file in type scope capacity status energy_full energy_full_design cycle_count; do
        [[ -r "$supply/$file" ]] && copy_file "$supply/$file" "$target/sys/class/power_supply/$name/$file"
    done
done

echo "Fixtures written to $target"
