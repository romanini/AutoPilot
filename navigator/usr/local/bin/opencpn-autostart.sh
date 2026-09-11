#!/bin/bash
# Launch OpenCPN and force it to open maximized, regardless of its last saved window state.
flatpak run org.opencpn.OpenCPN &

for i in $(seq 1 30); do
    win=$(xdotool search --onlyvisible --name "^OpenCPN" | head -1)
    if [ -n "$win" ]; then
        xdotool windowactivate "$win"
        wmctrl -i -r "$win" -b add,maximized_vert,maximized_horz
        break
    fi
    sleep 1
done
