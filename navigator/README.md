# Navigator
The navigation computer is not a raspberryPI with OpenNavigator, rather it is an Orange Pi Zero 2W.  The reason for this is simply one of power consumption.  The Orange Pi has just as much CPU as the Raspberry PI 4 Plus and just as much RAM, but consumes 25% the power.


## SD card backup and restore

The OrangePi setup is not distributed as a disk image — it is too large for Git.
To set up a fresh card, follow the rest of this README. Use the commands below
to back up an existing working card or restore from a backup.

**Two things make `dd` fast on macOS:**
- Use `/dev/rdisk2` not `/dev/disk2` — the `r` prefix (raw device) bypasses the buffer cache and is ~10× faster.
- Use `bs=4m` for large block transfers instead of the default 512-byte blocks.

Pipe through `gzip` to compress the output; this cuts file size roughly in half
and is usually faster overall because writing less to disk beats the CPU cost of
compression. Hit **Ctrl+T** at any time to print progress.

### Backup: SD card → compressed file

1. Find your SD card device:
   ```bash
   diskutil list
   ```
2. Unmount it (replace `disk2` with your actual device):
   ```bash
   diskutil unmountDisk /dev/disk2
   ```
3. Copy and compress:
   ```bash
   sudo dd if=/dev/rdisk2 bs=4m | gzip > orangepi-$(date +%Y-%m-%d).img.gz
   ```

### Restore: compressed file → SD card

1. Unmount the card:
   ```bash
   diskutil unmountDisk /dev/disk2
   ```
2. Decompress and write:
   ```bash
   gunzip -c orangepi-YYYY-MM-DD.img.gz | sudo dd of=/dev/rdisk2 bs=4m
   ```

## Networking

The OrangePi has two Wi-Fi interfaces, both managed by NetworkManager
(`/etc/netplan/orangepi-default.yaml` just sets `renderer: NetworkManager` and
delegates everything to it):

- `wlan0` (onboard) joins **SoberPilot**, the controller's AP, on `10.20.1.x`.
  Route metric `600`.
- A USB Wi-Fi adapter (`wlx...`) joins the home/internet network
  (`milosmeadow-fast`) on `172.16.0.x`. Route metric `100`.

Lower metric wins, so the USB adapter's default route takes priority for
general internet traffic, while `10.20.1.0/24` traffic (talking to the
controller) still goes out `wlan0`. Set with:

```
nmcli connection modify "milosmeadow-fast 1" ipv4.route-metric 100
nmcli connection modify "SoberPilot" ipv4.route-metric 600
```

### Keeping `wlan0` awake for UDP telemetry

The controller broadcasts `~APDAT` telemetry over UDP on port 8888
(see the main `AutoPilot` skill for the protocol). Left alone, `wlan0` would
go into power-save and stop reliably receiving these broadcasts. Two things
fix this:

- `/etc/udev/rules.d/10-wifi-disable-powermanagement.rules` — runs
  `iwconfig wlan0 power off` whenever the `wlan0` interface is added.
- `/etc/systemd/system/wifi-keepalive.service` — continuously pings the
  controller's AP gateway (`ping -i 0.3 10.20.1.1`, `Restart=always`) to keep
  the link active. Enabled with `systemctl enable --now wifi-keepalive.service`.

## OpenCPN

OpenCPN is installed as a **Flatpak** from Flathub (`org.opencpn.OpenCPN`,
user install):

```
flatpak install --user flathub org.opencpn.OpenCPN
```

### Plugins

- **o-charts_pi** (v2.0.0.66) — encrypted vector charts from o-charts.org.
  Installed via OpenCPN's in-app plugin manager.

### Serial NMEA inputs

OpenCPN reads two serial devices, configured as Data Connections in
OpenCPN's connection settings:

- `/dev/ttyUSB0` @ 4800 baud — a
  [GlobalSat BU-353-N5](https://www.amazon.com/GlobalSat-BU-353N5-GNSS-Receiver-Black/dp/B0B1W1YBZC)
  USB GPS receiver (NMEA-0183).
- `/dev/ttyACM0` @ 38400 baud — a
  [dAISy AIS receiver](https://shop.wegmatt.com/products/daisy-ais-receiver)
  (NMEA-0183 AIS).

Because OpenCPN runs sandboxed under Flatpak, it needs broad device access to
open these serial ports:

```
flatpak override --user org.opencpn.OpenCPN --device=all
```

`/etc/udev/rules.d/70-serial-opencpn.rules` also sets `MODE="0666"` on
`ttyUSB*`, `ttyACM*`, and `ttyS*` so the ports are world-readable/writable
regardless of group membership.

