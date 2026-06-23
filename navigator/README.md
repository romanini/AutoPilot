# Navigator

The navigation computer runs Ubuntu 22.04 with OpenCPN (Flatpak) and the `autopilot_pi` plugin.
Two hardware options have been evaluated:

| Hardware | Power @ 12 V | OpenGL | Notes |
|----------|-------------|--------|-------|
| OrangePi Zero 2W | ~2.4 W (0.2 A) | Off — GPU driver unreliable | Development unit |
| Raspberry Pi 4 Model B (8 GB) | TBD — measure before committing | On — VideoCore VI (vc4-kms-v3d) works | Better chart rendering |

OpenGL makes chart panning and zooming significantly faster. The RPi 4 supports it; the
OrangePi does not. Measure actual power draw with a meter before choosing which to install
on the boat.

---

## SD card backup and restore

The OrangePi setup is not distributed as a disk image — it is too large for Git.
To set up a fresh card, follow the from-scratch setup below. Use the commands here
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
3. Copy and compress (use the filename for your hardware):
   ```bash
   # OrangePi Zero 2W
   sudo dd if=/dev/rdisk2 bs=4m | gzip > navigator-OrangePiZero2W-$(date +%Y-%m-%d).img.gz
   # Raspberry Pi 4 Model B
   sudo dd if=/dev/rdisk2 bs=4m | gzip > navigator-RaspberryPi4ModelB-$(date +%Y-%m-%d).img.gz
   ```

### Restore: compressed file → SD card

1. Unmount the card:
   ```bash
   diskutil unmountDisk /dev/disk2
   ```
2. Decompress and write:
   ```bash
   # OrangePi Zero 2W
   gunzip -c navigator-OrangePiZero2W-YYYY-MM-DD.img.gz | sudo dd of=/dev/rdisk2 bs=4m
   # Raspberry Pi 4 Model B
   gunzip -c navigator-RaspberryPi4ModelB-YYYY-MM-DD.img.gz | sudo dd of=/dev/rdisk2 bs=4m
   ```

---

## From-scratch setup

### Part 1 — Flash the SD card (on your Mac)

#### OrangePi Zero 2W

1. Go to the OrangePi Zero 2W product page at orangepi.org → **Resources** and download
   the **Ubuntu Jammy (22.04)** image.
2. Decompress the `.xz` archive:
   ```bash
   xz -d Orangepi*.img.xz
   ```
3. Unmount the SD card and flash it directly (no gzip since the image is already unpacked):
   ```bash
   diskutil unmountDisk /dev/disk2
   sudo dd if=Orangepi*.img of=/dev/rdisk2 bs=4m
   ```

#### Raspberry Pi 4 Model B

1. Install [Raspberry Pi Imager](https://www.raspberrypi.com/software/) on your Mac.
2. Open Imager → **Choose OS** → **Other general-purpose OS** → **Ubuntu** →
   **Ubuntu Server 22.04 LTS (64-bit)**.
3. Click the ⚙ settings icon and configure:
   - **Hostname:** `navigator`
   - **Username:** `navigator`
   - **Password:** your choice
   - **Enable SSH:** yes
4. **Choose Storage** → select your SD card → **Write**.

The root filesystem expands to fill the card automatically on first boot — no manual step needed.

---

### Part 2 — First boot

Insert the SD card, connect monitor + keyboard + mouse, power on.

| Platform | Default login |
|----------|--------------|
| OrangePi | `orangepi` / `orangepi` |
| RPi 4 | `navigator` / your choice |

```bash
# OrangePi only — RPi hostname was already set in Imager
sudo hostnamectl set-hostname navigator

# Full system update
sudo apt update && sudo apt full-upgrade -y
sudo reboot
```

---

### Part 3 — Networking (do this manually, before Claude)

> **Do this by hand — do not let Claude do it.** Claude Code needs a live internet
> connection to the model. If Claude reconfigures the Wi-Fi adapters, it drops its own
> connection mid-command and stops. Bring networking up yourself now so Claude starts from
> an already-connected machine.

Connect both Wi-Fi networks and set route metrics so internet goes out your home network,
not SoberPilot. **Lower metric wins**, so give home the lower number. (Full rationale and
the power-save / keepalive details are in [Dual Wi-Fi networking](#dual-wi-fi-networking)
below.)

```bash
# Connect to each network (nmcli picks the right adapter automatically)
nmcli device wifi connect SoberPilot password <password>
nmcli device wifi connect <home-ssid> password <password>

# SoberPilot = high metric (600), home internet = low metric (100)
nmcli connection modify SoberPilot ipv4.route-metric 600
nmcli connection modify "<home-network-connection-name>" ipv4.route-metric 100
nmcli connection up "<home-network-connection-name>"
nmcli connection up SoberPilot
```

To find the connection name nmcli assigned to the home network: `nmcli connection show`.

**Verify the default route goes out your home network:**

```bash
ip route list
```

The top `default` line must be via your **home** gateway. If SoberPilot's gateway has the
lower metric, internet traffic (and Claude) tries to route through the controller's AP,
which has no internet. Fix it:

```bash
# Immediate override — drop SoberPilot's default route.
# This keeps its subnet route, so telemetry to the controller still works.
sudo ip route del default via <soberpilot-gateway>

# Confirm
ip route list          # top default line should now be via your home gateway
ping -c2 1.1.1.1       # confirm internet
```

The `nmcli ... ipv4.route-metric` commands above make the correct ordering permanent; the
`ip route del` is just an immediate override until the next reconnect. If `ip route list`
already showed home on top, you can skip the `ip route del` step.

---

### Part 4 — Bootstrap: git and Claude Code

After reboot, log back in and run:

```bash
# Node.js 20 (Claude Code runtime) + git
curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash -
sudo apt-get install -y nodejs git

# Claude Code
sudo npm install -g @anthropic-ai/claude-code

# Authenticate — opens a browser window for OAuth
claude login

# Clone this repo
mkdir -p ~/dev
git clone https://github.com/romanini/AutoPilot.git ~/dev/AutoPilot
```

---

### Part 5 — Let Claude Code complete the setup

```bash
cd ~/dev/AutoPilot
claude
```

Tell Claude Code:

> "Set up this machine as the navigator computer. Follow `navigator/README.md` exactly.
> My hardware is [OrangePi Zero 2W / Raspberry Pi 4 Model B]. Networking is already configured —
> do not touch the Wi-Fi adapters, connections, or routes."

Claude Code will work through the remaining System Configuration sections below — udev
rules, systemd service, OpenCPN, and the autopilot_pi plugin. **Networking is already
done in Part 3** and Claude must leave it alone, otherwise it will lose its connection to
the model.

---

## System configuration

These are the steps Claude Code follows. You can also run them manually in order.

### NetworkManager

Both platforms use NetworkManager for Wi-Fi. On RPi Ubuntu Server it is not installed
by default.

```bash
sudo apt install -y network-manager
sudo systemctl enable --now NetworkManager
```

Update netplan to delegate to NetworkManager. The existing netplan filename varies by
platform (`orangepi-default.yaml` on OrangePi, `50-cloud-init.yaml` on RPi). Replace
its contents with:

```yaml
network:
  version: 2
  renderer: NetworkManager
```

Then apply:

```bash
sudo netplan apply
```

---

### Dual Wi-Fi networking

> Done manually in [Part 3](#part-3--networking-do-this-manually-before-claude), **not** by
> Claude — reconfiguring the adapters would drop Claude's connection to the model. This
> section is the full reference for those steps.

Two Wi-Fi adapters are required:

- **`wlan0`** (onboard) → joins **SoberPilot** (the controller's AP) on `10.20.1.x`
- **USB Wi-Fi adapter** (`wlx...`) → joins your home/internet network

**Important — USB adapter is 2.4 GHz only.** The USB adapter tested with this setup
does not support 5 GHz. Any internet network you connect it to (home router, hotspot,
marina Wi-Fi) must broadcast on 2.4 GHz. On an iPhone hotspot, enable
**Settings → Personal Hotspot → Maximize Compatibility** to force 2.4 GHz.

Route metrics control which adapter handles which traffic. Lower metric wins:

```bash
# Connect to each network (nmcli will pick the right adapter automatically)
nmcli device wifi connect SoberPilot password <password>
nmcli device wifi connect <home-ssid> password <password> ifname wlx0013efc00bc4

# Lock SoberPilot to wlan0 so nothing can accidentally pull it off that interface
nmcli connection modify SoberPilot connection.interface-name wlan0

# Set metrics — SoberPilot stays on wlan0 (metric 600), internet goes out USB (metric 100)
nmcli connection modify SoberPilot ipv4.route-metric 600
nmcli connection modify "<home-network-connection-name>" ipv4.route-metric 100
```

To find the connection name nmcli assigned to the home network: `nmcli connection show`.

**Switching internet networks** (e.g. home → hotspot → marina): always specify the
USB adapter explicitly so `wlan0`/SoberPilot is never touched:

```bash
nmcli device wifi connect <new-ssid> password <password> ifname wlx0013efc00bc4
```

NetworkManager saves the password, so it will reconnect automatically next time
you're in range of that network.

---

### Keep wlan0 awake for UDP telemetry

The controller broadcasts `~APDAT` telemetry on UDP port 8888. Without these,
`wlan0` enters power-save mode and drops the broadcasts.

The config files are checked into this repo under `navigator/etc/`:

```bash
# Disable power management on wlan0 whenever the interface is added
sudo cp ~/dev/AutoPilot/navigator/etc/udev/rules.d/10-wifi-disable-powermanagement.rules \
     /etc/udev/rules.d/
sudo udevadm control --reload-rules

# Continuously ping the controller's AP gateway to keep the link alive
sudo cp ~/dev/AutoPilot/navigator/etc/systemd/system/wifi-keepalive.service \
     /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now wifi-keepalive.service
```

---

### Serial port permissions

OpenCPN runs sandboxed inside Flatpak and needs world-readable serial ports to open the
GPS and AIS receivers:

```bash
sudo cp ~/dev/AutoPilot/navigator/etc/udev/rules.d/70-serial-opencpn.rules \
     /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
```

---

### Desktop environment (RPi 4 only)

RPi Ubuntu Server ships without a GUI. OpenCPN needs a windowed desktop, so install
XFCE (lightweight, low overhead for a nav computer):

```bash
sudo apt install -y xubuntu-desktop
sudo reboot
```

After reboot the LightDM greeter appears and you can log in graphically. The OrangePi
image ships with a desktop already — skip this step there.

---

### OpenCPN

```bash
# Install Flatpak if not already present
sudo apt install -y flatpak
flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo

# Install OpenCPN
flatpak install --user flathub org.opencpn.OpenCPN

# Grant the Flatpak sandbox access to serial ports
flatpak override --user org.opencpn.OpenCPN --device=all
```

**System updates:** The GUI Software Updater silently fails on this system (no PolicyKit
auth agent). Use the terminal instead:

```bash
sudo apt update && sudo apt full-upgrade
```

#### Plugins

- **o-charts_pi** — install via OpenCPN's in-app Plugin Manager
  (Toolbox → Plugin Manager → search "o-charts"). Provides encrypted vector charts.
- **autopilot_pi** — see next section.

---

### autopilot_pi plugin

Full details and build prerequisites are in `opencpn_plugin/autopilot_pi/README.md`.

```bash
# Build prerequisites (run once)
sudo apt install -y flatpak-builder
flatpak install --user flathub org.freedesktop.Sdk//25.08

# Build and install the plugin
cd ~/dev/AutoPilot/opencpn_plugin/autopilot_pi
flatpak-builder --user --install --force-clean \
    build-dir flatpak/org.opencpn.OpenCPN.Plugin.autopilot.yaml
```

If OpenCPN refuses to load the plugin after a crash, remove the load stamp:

```bash
rm ~/.var/app/org.opencpn.OpenCPN/config/opencpn/load_stamps/libautopilot_pi
```

---

## Post-setup — manual GUI configuration

These steps require the graphical desktop and cannot be automated by Claude Code.

### OpenCPN — enable OpenGL (RPi 4 only)

Launch OpenCPN → Toolbox → Settings → Display → tick **Enable OpenGL rendering** →
restart OpenCPN. This makes chart panning and zooming much faster.
*(Leave OpenGL off on OrangePi — the GPU driver is not reliable enough.)*

### OpenCPN — serial data connections

Toolbox → Settings → Connections → Add Connection for each device:

| Port | Baud rate | Device |
|------|-----------|--------|
| `/dev/ttyUSB0` | 4800 | GlobalSat BU-353-N5 USB GPS (NMEA-0183) |
| `/dev/ttyACM0` | 38400 | dAISy AIS receiver (NMEA-0183) |

### OpenCPN — enable plugins

- **AutoPilot** — Toolbox → Plugin Manager → find "AutoPilot" → **Enable**.
- **o-charts_pi** — Plugin Manager → search "o-charts" → install and configure
  for encrypted vector charts.
