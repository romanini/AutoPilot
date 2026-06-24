#!/bin/bash
# Blanks the screen after idle timeout using xrandr (DPMS doesn't work on RPi 4 vc4 driver).
# Monitors xprintidle and toggles HDMI-1 off/on.

TIMEOUT_MS=300000  # 5 minutes
POLL_INTERVAL=10   # seconds
OUTPUT="HDMI-1"

blanked=false

while true; do
    idle=$(xprintidle 2>/dev/null || echo 0)

    if [ "$idle" -ge "$TIMEOUT_MS" ] && [ "$blanked" = false ]; then
        xrandr --output "$OUTPUT" --off
        blanked=true
    elif [ "$idle" -lt "$TIMEOUT_MS" ] && [ "$blanked" = true ]; then
        xrandr --output "$OUTPUT" --auto
        blanked=false
    fi

    sleep "$POLL_INTERVAL"
done
