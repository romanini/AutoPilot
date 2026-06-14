# Navigator
The navigation computer is not a raspberryPI with OpenNavigator, rather it is an Orange Pi Zero 2W.  The reason for this is simply one of power consumption.  The Orange Pi has just as much CPU as the Raspberry PI 4 Plus and just as much RAM, but consumes 25% the power.


## How to use this image
You will want to copy this image to the SD card for the OrangePi follow these steps:
1. Insert the SD card into a card reader and attach to the computer
2. Find the device identifier for the SD card. On a mac you can do this with:
```
diskutil list
```
3. Unmount the device
```
diskutil unmountDisk /dev/disk2
```
4. Copy the image to the device
```
sudo dd of=/dev/disk2 if=orangepi-2024-03-28.img
```

## How to create this image (ie how to backup the Pi)
The steps are identical to how to use the image except for the copy is the reverse:
```
sudo dd if=/dev/disk2 of=orangepi-2024-03-28.img
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

