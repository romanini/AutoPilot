#!/bin/bash
# Blanks the screen after keyboard idle using xrandr (DPMS doesn't work on RPi 4 vc4 driver).
# Tracks keyboard events only — mouse motion from boat vibration is ignored.
# Requires the user to be in the 'input' group.

TIMEOUT=300  # 5 minutes in seconds
OUTPUT="HDMI-1"
KBD="/dev/input/by-id/usb-Telink_Wireless_Receiver-if01-event-kbd"

export DISPLAY=:0.0
export XAUTHORITY=/home/navigator/.Xauthority

blanked=false

while true; do
    if [ "$blanked" = false ]; then
        # Block until keyboard idle for TIMEOUT seconds
        if ! timeout "$TIMEOUT" dd if="$KBD" of=/dev/null bs=24 count=1 2>/dev/null; then
            xrandr --output "$OUTPUT" --off
            blanked=true
        fi
        # If dd succeeded, a key was pressed — restart the timer
    else
        # Screen is blanked — wait for any keypress to wake
        dd if="$KBD" of=/dev/null bs=24 count=1 2>/dev/null
        xrandr --output "$OUTPUT" --auto
        blanked=false
    fi
done
