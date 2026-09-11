#!/bin/bash
# Gracefully close OpenCPN before the system powers off/reboots. OpenCPN only
# clears its crash-detection marker on a clean exit (SIGTERM) -- closing its
# window is not enough, it leaves the process running headless with no window.
# Without this, the next boot shows the "did not shut down properly" dialog.
pid=$(pgrep -x opencpn)
if [ -z "$pid" ]; then
    exit 0
fi

kill -TERM "$pid"

for i in $(seq 1 20); do
    kill -0 "$pid" 2>/dev/null || exit 0
    sleep 1
done
