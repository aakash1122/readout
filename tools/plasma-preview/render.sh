#!/bin/bash
# Renders a preview scene to PNG inside a private, invisible session:
# its own D-Bus session, its own readoutd, and a virtual KWin with no screen.
# Nothing appears on the desktop and the user's settings are not touched.
#
#   tools/plasma-preview/render.sh BUILD_DIR SCENE.qml OUTPUT.png [light|dark] [seconds]
set -euo pipefail

build=$(realpath "$1")
scene=$(realpath "$2")
output=$(realpath -m "$3")
theme=${4:-light}
seconds=${5:-12}

if [[ -z ${READOUT_PREVIEW_INNER:-} ]]; then
    # Run the private session in its own process session, so everything it starts
    # (including services D-Bus activates, like portals) can be stopped together afterwards.
    export READOUT_PREVIEW_INNER=1
    log=$(mktemp)
    setsid bash -c 'echo $$ > "$1.sid"; exec dbus-run-session -- "${@:2}"' _ "$log" "$0" "$build" "$scene" "$output" "$theme" "$seconds" \
        </dev/null >"$log" 2>&1 &
    wait $! && status=0 || status=$?
    pkill -KILL -s "$(cat "$log.sid")" 2>/dev/null || true
    [[ $status -eq 0 ]] || cat "$log" >&2
    rm -f "$log" "$log.sid"
    exit "$status"
fi

work=$(mktemp -d)
trap 'kill $(jobs -p) 2>/dev/null || true; rm -rf "$work"' EXIT

# Theme and a 1 s update interval, so charts fill quickly.
export XDG_CONFIG_HOME="$work/config"
mkdir -p "$XDG_CONFIG_HOME"
scheme=$([[ $theme == dark ]] && echo BreezeDark || echo BreezeLight)
{ cat "/usr/share/color-schemes/$scheme.colors"; printf '\n[General]\nColorScheme=%s\n\n[Icons]\nTheme=breeze\n' "$scheme"; } > "$XDG_CONFIG_HOME/kdeglobals"
# READOUT_PREVIEW_SETTINGS adds lines to the [General] group, e.g. "PopupTextScale=120".
printf '[General]\nUpdateInterval=1\n%s\n' "${READOUT_PREVIEW_SETTINGS:-}" > "$XDG_CONFIG_HOME/readoutrc"

"$build/daemon/readoutd" --no-idle-exit 2>/dev/null &
socket="readout-preview-$$"
kwin_wayland --virtual --socket "$socket" --width 3400 --height 1600 --no-lockscreen 2>"$work/kwin.log" &
for _ in $(seq 50); do
    [[ -S "$XDG_RUNTIME_DIR/$socket" ]] && break
    sleep 0.1
done

# The KDE platform theme reads the color scheme above, as it does in a Plasma session.
QT_QPA_PLATFORM=wayland QT_QPA_PLATFORMTHEME=kde QT_QUICK_CONTROLS_STYLE=org.kde.desktop WAYLAND_DISPLAY="$socket" \
    "$build/frontends/plasma/plasma-preview/plasma-preview" --scene "$scene" --output "$output" --delay $((seconds * 1000))
