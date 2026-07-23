# Navigator

The navigation computer runs Ubuntu with OpenCPN (Flatpak) and the `autopilot_pi` plugin.
Version depends on hardware: 22.04 on the OrangePi and RPi 4, 24.04 on the RPi 5 (22.04
predates Pi 5 hardware support). Hardware options evaluated so far:

| Hardware | Power @ 12 V | OpenGL | Notes |
|----------|-------------|--------|-------|
| OrangePi Zero 2W | ~2.4-3.3 W (0.2-0.28 A) | Off — GPU driver unreliable | Development unit |
| Raspberry Pi 4 Model B (8 GB) | ~3.0-6.0W (0.25-0.5 A) | On — VideoCore VI (vc4-kms-v3d) works | Better chart rendering |
| Raspberry Pi 5 (8 GB) | TBD — being measured | On — VideoCore VII, faster than RPi 4 | SD card + NVMe (see below) |

OpenGL makes chart panning and zooming significantly faster. The RPi 4 and RPi 5 support it;
the OrangePi does not. Measure actual power draw with a meter before choosing which to install
on the boat.

**RPi 5 boot media:** the Pi 5 uses both an SD card and an NVMe drive (attached via a PCIe
M.2 HAT). NVMe is the primary boot device — faster and no SD wear-out concerns for an
appliance that's powered on/off with the boat. The SD card stays installed as a fallback: if
the NVMe drive ever fails or is removed, the bootloader falls back to booting it. Both cards
get a full, independent Ubuntu install — see [Part 1](#part-1--flash-the-boot-media-on-your-mac)
and [Enable NVMe boot](#raspberry-pi-5--enable-nvme-boot) below.

---

## SD card backup and restore

The OrangePi setup is not distributed as a disk image — it is too large for Git.
To set up a fresh card, follow the from-scratch setup below. Use the commands here
to back up an existing working card or restore from a backup.

**Two things make `dd` fast on macOS:**
- Use `/dev/rdisk3` not `/dev/disk3` — the `r` prefix (raw device) bypasses the buffer cache and is ~10× faster.
- Use `bs=4m` for large block transfers instead of the default 512-byte blocks.

Pipe through `gzip` to compress the output; this cuts file size roughly in half
and is usually faster overall because writing less to disk beats the CPU cost of
compression. Hit **Ctrl+T** at any time to print progress.

### Backup: SD card → compressed file

1. Find your SD card device:
   ```bash
   diskutil list
   ```
2. Unmount it (replace `disk3` with your actual device):
   ```bash
   diskutil unmountDisk /dev/disk3
   ```
3. Copy and compress (use the filename for your hardware/media):
   ```bash
   # OrangePi Zero 2W
   sudo dd if=/dev/rdisk3 bs=4m | gzip > navigator-OrangePiZero2W-$(date +%Y-%m-%d).img.gz
   # Raspberry Pi 4 Model B
   sudo dd if=/dev/rdisk3 bs=4m | gzip > navigator-RaspberryPi4ModelB-$(date +%Y-%m-%d).img.gz
   # Raspberry Pi 5 — SD card (fallback boot)
   sudo dd if=/dev/rdisk3 bs=4m | gzip > navigator-RaspberryPi5-SD-$(date +%Y-%m-%d).img.gz
   # Raspberry Pi 5 — NVMe (primary boot; remove from the M.2 HAT and attach via a
   # USB-to-M.2 NVMe enclosure — the device node will differ, check `diskutil list`)
   sudo dd if=/dev/rdisk4 bs=4m | gzip > navigator-RaspberryPi5-NVMe-$(date +%Y-%m-%d).img.gz
   ```

### Restore: compressed file → SD card

1. Unmount the card:
   ```bash
   diskutil unmountDisk /dev/disk3
   ```
2. Decompress and write:
   ```bash
   # OrangePi Zero 2W
   gunzip -c navigator-OrangePiZero2W-YYYY-MM-DD.img.gz | sudo dd of=/dev/rdisk3 bs=4m
   # Raspberry Pi 4 Model B
   gunzip -c navigator-RaspberryPi4ModelB-YYYY-MM-DD.img.gz | sudo dd of=/dev/rdisk3 bs=4m
   # Raspberry Pi 5 — SD card
   gunzip -c navigator-RaspberryPi5-SD-YYYY-MM-DD.img.gz | sudo dd of=/dev/rdisk3 bs=4m
   # Raspberry Pi 5 — NVMe (via USB-to-M.2 enclosure; check `diskutil list` for the device node)
   gunzip -c navigator-RaspberryPi5-NVMe-YYYY-MM-DD.img.gz | sudo dd of=/dev/rdisk4 bs=4m
   ```

---

## From-scratch setup

### Part 1 — Flash the boot media (on your Mac)

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

#### Raspberry Pi 5 (SD card + NVMe)

Flash **both** cards now, before the Pi 5 ever powers on — it's easiest to do them
back-to-back on the Mac while Imager is already open.

1. **SD card first** (this becomes the fallback boot media): same steps as RPi 4 above, except
   choose **Ubuntu Server 24.04 LTS (64-bit)** and use hostname `navigator`. Set the same
   username/password/SSH settings you'll use on the NVMe drive.
2. **NVMe drive** (this becomes the primary boot media): connect the NVMe drive to your Mac
   via a USB-to-M.2 NVMe enclosure/adapter (it isn't readable directly — the Pi 5's M.2 HAT
   is the only thing that talks to it over PCIe). In Imager, repeat the same OS choice
   (**Ubuntu Server 24.04 LTS (64-bit)**) and the same ⚙ settings (hostname `navigator`,
   same username/password, SSH enabled) → **Choose Storage** → select the NVMe device (not
   the SD card!) → **Write**.
3. Do **not** assemble the Pi 5 yet. NVMe boot has to be enabled from the EEPROM bootloader
   first, and that has to happen while running from the SD card — see
   [Enable NVMe boot](#raspberry-pi-5--enable-nvme-boot) after Part 2 below. Insert only the
   SD card for now; leave the NVMe drive out of the M.2 HAT until that step tells you to
   install it.

**Don't have the NVMe drive yet?** Skip step 2 above and do everything through Post-setup on
the SD card alone — see
[Set up on the SD card now, clone to NVMe later](#set-up-on-the-sd-card-now-clone-to-nvme-later)
below for how to catch the NVMe drive up once it arrives.

#### Set up on the SD card now, clone to NVMe later

You don't need the NVMe drive in hand to get started — flash and set up the SD card only
(steps above minus step 2), work all the way through Part 2 → Post-setup on it, and validate
everything works. When the NVMe drive arrives:

1. Shut down, remove the SD card, and image it on your Mac using the
   [backup commands](#backup-sd-card--compressed-file) (the `navigator-RaspberryPi5-SD-...img.gz`
   line). Re-insert the SD card and boot the Pi 5 normally again.
2. Connect the new NVMe drive to the Pi 5 itself (not the Mac) via a USB-to-M.2 enclosure,
   using a spare USB port — not the internal M.2 HAT yet. It'll show up as an external disk;
   confirm the device name before touching anything:
   ```bash
   lsblk    # e.g. /dev/sda — do NOT run the following against mmcblk0, the live SD card!
   ```
3. Write the SD image onto it directly from the running Pi 5 (all the Linux partition tools
   you'll need next are already there, so it's simpler than round-tripping through the Mac):
   ```bash
   gunzip -c navigator-RaspberryPi5-SD-YYYY-MM-DD.img.gz | sudo dd of=/dev/sda bs=4M status=progress
   sync
   ```
   This is now a byte-for-bit copy of the SD card.
4. **De-duplicate identifiers before the two ever run together.** A straight clone means the
   NVMe has the exact same MBR disk signature (and therefore `PARTUUID`s), filesystem UUIDs,
   machine-id, and SSH host keys as the SD card. With both physically installed at once —
   which is the whole point of the fallback design — that's not cosmetic: the kernel/udev/blkid
   can't reliably tell the two partitions apart and may mount the wrong one. Fix it now, while
   the clone is still just an external disk (adjust `/dev/sda1`/`/dev/sda2` below if `lsblk`
   showed different partition numbers):

   a. **MBR disk signature** — this is what each partition's `PARTUUID` is derived from
      (`<disk-id>-<partition-number>`):
      ```bash
      sudo sfdisk --disk-id /dev/sda 0x$(openssl rand -hex 4)
      sudo partprobe /dev/sda
      sudo blkid /dev/sda1 /dev/sda2     # note the new PARTUUIDs, you'll need them in (d)
      ```
   b. **ext4 root filesystem UUID** (the root partition — normally `sda2`):
      ```bash
      sudo tune2fs -U random /dev/sda2
      ```
   c. **FAT32 boot partition volume ID** (the boot partition — normally `sda1`). `dosfstools`
      has no in-place command for this, so patch the 4-byte `BS_VolID` field directly at its
      fixed offset (67) in the boot sector instead of reformatting:
      ```bash
      sudo dd if=/dev/urandom of=/dev/sda1 bs=1 count=4 seek=67 conv=notrunc
      ```
   d. **Update `cmdline.txt` / `fstab` on the clone to match** — mount the clone's partitions
      and check which scheme your image actually uses before assuming (Ubuntu Raspberry Pi
      images have used both `PARTUUID=` and `LABEL=` across releases):
      ```bash
      sudo mkdir -p /mnt/nvme-boot /mnt/nvme-root
      sudo mount /dev/sda1 /mnt/nvme-boot
      sudo mount /dev/sda2 /mnt/nvme-root
      grep -H root= /mnt/nvme-boot/cmdline.txt
      grep -H -E "PARTUUID|LABEL" /mnt/nvme-root/etc/fstab
      ```
      If either references `PARTUUID=`, replace the old value with the matching new one from
      (a)'s `blkid` output. If it references `LABEL=` instead, there's nothing to change —
      labels weren't touched by any of this.
   e. **machine-id and SSH host keys** — not partition-related, but the same "identical clone"
      problem: both media would otherwise claim the same machine identity on the network.
      ```bash
      sudo rm /mnt/nvme-root/etc/machine-id
      sudo systemd-machine-id-setup --root=/mnt/nvme-root
      sudo rm /mnt/nvme-root/etc/ssh/ssh_host_*
      sudo ssh-keygen -A -f /mnt/nvme-root
      sudo umount /mnt/nvme-boot /mnt/nvme-root
      ```
5. Shut down, move the NVMe drive from the USB enclosure into the internal M.2 HAT slot, and
   pick up at step 3 of [Enable NVMe boot](#raspberry-pi-5--enable-nvme-boot) below (EEPROM
   update → `BOOT_ORDER` → verify it actually boots from `nvme0n1`).

> I haven't verified the exact partition numbering or the `PARTUUID`-vs-`LABEL` scheme against
> the specific Ubuntu 24.04 RPi image — the `lsblk`/`blkid`/`grep` checks above are there so you
> confirm against your actual clone rather than trusting the device names as gospel. If
> something looks off, the SD card image from step 1 is your safety net — you can always
> re-run steps 2–4 from scratch on the NVMe.

---

### Part 2 — First boot

Insert the SD card, connect monitor + keyboard + mouse, power on.

| Platform | Default login |
|----------|--------------|
| OrangePi | `orangepi` / `orangepi` |
| RPi 4 | `navigator` / your choice |
| RPi 5 | `navigator` / your choice (boots from the SD card at this point) |

```bash
# OrangePi only — RPi hostname was already set in Imager
sudo hostnamectl set-hostname navigator

# Full system update
sudo apt update && sudo apt full-upgrade -y
sudo reboot
```

**RPi 5 only:** stop here and do
[Enable NVMe boot](#raspberry-pi-5--enable-nvme-boot) before continuing to Part 3 — it needs
to run while still booted from the SD card.

---

### Raspberry Pi 5 — enable NVMe boot

> Do this once, right after the SD card's first boot/update/reboot above, and before
> installing the NVMe drive in the M.2 HAT.

1. Update the EEPROM bootloader to the latest version (older units shipped without NVMe boot
   support):
   ```bash
   sudo apt update && sudo apt install -y rpi-eeprom
   sudo rpi-eeprom-update -a
   sudo reboot
   ```
2. After reboot, set the boot order to try NVMe first and fall back to the SD card:
   ```bash
   sudo -E rpi-eeprom-config --edit
   ```
   Set (or add) this line, then save and exit the editor:
   ```
   BOOT_ORDER=0xf16
   ```
   (Reading right-to-left: `6`=NVMe tried first, `1`=SD card fallback, `f`=retry the sequence
   forever rather than giving up.) Confirm with `sudo rpi-eeprom-config` — the new config is
   written to the EEPROM and takes effect after the next power cycle.
3. Shut down, install the NVMe drive in the M.2 HAT, and power back on:
   ```bash
   sudo shutdown -h now
   ```
4. If the NVMe drive isn't detected reliably (PCIe signal integrity issues are common with
   longer ribbon cables), force Gen 3 speed by adding this to `/boot/firmware/config.txt` and
   rebooting:
   ```
   dtparam=pciex1_gen=3
   ```
5. Verify you're actually running from NVMe:
   ```bash
   lsblk           # rootfs should be on nvme0n1, not mmcblk0
   df -h /
   ```
   If it still booted from the SD card, double check `BOOT_ORDER` with
   `sudo rpi-eeprom-config` and that the NVMe drive was flashed correctly in Part 1.

From here on, log in and do all further setup (networking, `apt full-upgrade`, Claude Code
bootstrap) on the **NVMe-booted** system — the SD card just sits in the slot as a cold
fallback and doesn't need further updates until you actually fail over to it.

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
> My hardware is [OrangePi Zero 2W / Raspberry Pi 4 Model B / Raspberry Pi 5]. Networking is
> already configured — do not touch the Wi-Fi adapters, connections, or routes."

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

### Desktop environment (RPi 4 and RPi 5 only)

RPi Ubuntu Server ships without a GUI. OpenCPN needs a windowed desktop, so install
XFCE (lightweight, low overhead for a nav computer):

```bash
sudo apt install -y xubuntu-desktop
sudo reboot
```

After reboot the LightDM greeter appears and you can log in graphically. The OrangePi
image ships with a desktop already — skip this step there.

#### Auto-login

```bash
sudo tee /etc/lightdm/lightdm.conf.d/50-autologin.conf > /dev/null <<'EOF'
[Seat:*]
autologin-user=navigator
autologin-user-timeout=0
EOF
```

#### Disable screen lock and configure blank screensaver

Kill and prevent all screen locker daemons from starting. The screen still blanks after
inactivity via DPMS (power manager), but waking requires no password.

```bash
# Kill and disable all screen locker daemons
killall light-locker xfce4-screensaver 2>/dev/null
mkdir -p ~/.config/autostart
printf '[Desktop Entry]\nHidden=true\n' > ~/.config/autostart/light-locker.desktop
printf '[Desktop Entry]\nHidden=true\n' > ~/.config/autostart/xfce4-screensaver.desktop
printf '[Desktop Entry]\nHidden=true\n' > ~/.config/autostart/xscreensaver.desktop

# Clear the session lock command so nothing can trigger a lock
xfconf-query -c xfce4-session -p /general/LockCommand -s ""

# Disable xfce4-screensaver lock settings (belt and suspenders)
xfconf-query -c xfce4-screensaver -p /lock-enabled -s false -n -t bool
xfconf-query -c xfce4-screensaver -p /saver-enabled -s false -n -t bool
xfconf-query -c xfce4-screensaver -p /idle-activation/enabled -s false -n -t bool
xfconf-query -c xfce4-screensaver -p /lock/sleep-activation -s false -n -t bool

# Power manager: blank screen after 5 min, no standby/off, no lock on suspend
xfconf-query -c xfce4-power-manager -p /lock-screen-suspend-hibernate -s false -n -t bool
xfconf-query -c xfce4-power-manager -p /dpms-enabled -s true -n -t bool
xfconf-query -c xfce4-power-manager -p /blank-on-ac -s 5 -n -t int
xfconf-query -c xfce4-power-manager -p /dpms-on-ac-sleep -s 0 -n -t int
xfconf-query -c xfce4-power-manager -p /dpms-on-ac-off -s 0 -n -t int

# GNOME gsettings — xfce4-screensaver reads these as fallback
gsettings set org.gnome.desktop.screensaver lock-enabled false
gsettings set org.gnome.desktop.screensaver idle-activation-enabled false
gsettings set org.gnome.desktop.screensaver ubuntu-lock-on-suspend false
gsettings set org.gnome.desktop.lockdown disable-lock-screen true
gsettings set org.gnome.desktop.session idle-delay 0

# Prevent logind from locking or suspending
sudo mkdir -p /etc/systemd/logind.conf.d
sudo tee /etc/systemd/logind.conf.d/no-lock.conf > /dev/null <<'LOGIND'
[Login]
HandleSuspendKey=ignore
HandleLidSwitch=ignore
IdleAction=ignore
IdleActionSec=0
LOGIND
sudo systemctl restart systemd-logind

# Suppress the "Display Settings" dialog on DPMS wake
xfconf-query -c displays -p /Notify -s false -n -t bool

# Disable DPMS (RPi 4 vc4 driver doesn't forward DPMS signals to hardware)
xset s off
xset dpms 0 0 0

# Install the xrandr-based screen blanking script and autostart entry.
# The script monitors xprintidle and turns HDMI-1 off/on via xrandr.
sudo cp ~/dev/AutoPilot/navigator/usr/local/bin/screen-blank.sh /usr/local/bin/
sudo chmod +x /usr/local/bin/screen-blank.sh
cat > ~/.config/autostart/screen-blank.desktop <<'AUTOSTART'
[Desktop Entry]
Type=Application
Name=Screen Blank
Exec=/usr/local/bin/screen-blank.sh
NoDisplay=true
X-XFCE-Autostart-Phase=Applications
AUTOSTART
```

#### Desktop appearance — match OrangePi layout

Black background, no desktop icons, standard "Applications" menu (not Xubuntu Whisker
Menu):

```bash
# Black background, no wallpaper
xfconf-query -c xfce4-desktop -p /backdrop/screen0/monitor0/image-show -s false -n -t bool
xfconf-query -c xfce4-desktop -p /backdrop/screen0/monitor0/workspace0/image-style -s 0 -n -t int
xfconf-query -c xfce4-desktop -p /backdrop/screen0/monitor0/workspace0/color-style -s 0 -n -t int
xfconf-query -c xfce4-desktop -p /backdrop/screen0/monitor0/workspace0/rgba1 \
    -s 0 -s 0 -s 0 -s 1 -n -t double -t double -t double -t double
xfconf-query -c xfce4-desktop -p /backdrop/screen0/monitorHDMI-1/workspace0/image-style -s 0 -n -t int
xfconf-query -c xfce4-desktop -p /backdrop/screen0/monitorHDMI-1/workspace0/color-style -s 0 -n -t int
xfconf-query -c xfce4-desktop -p /backdrop/screen0/monitorHDMI-1/workspace0/rgba1 \
    -s 0 -s 0 -s 0 -s 1 -n -t double -t double -t double -t double

# No desktop icons
xfconf-query -c xfce4-desktop -p /desktop-icons/style -s 0
xfconf-query -c xfce4-desktop -p /desktop-icons/file-icons/show-home -s false
xfconf-query -c xfce4-desktop -p /desktop-icons/file-icons/show-trash -s false

# Standard "Applications" menu instead of Whisker Menu
xfconf-query -c xfce4-panel -p /plugins/plugin-1 -s applicationsmenu
xfce4-panel --restart
```

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

### OpenCPN — enable OpenGL (RPi 4 and RPi 5 only)

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
