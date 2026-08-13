# AutoPilot (Arduino)

A marine autopilot built on the **Arduino Nano ESP32**. One board (the
**controller**) reads the boat's sensors and drives the steering motor; another
(the **display**) is the cockpit head unit with an LCD and buttons; a third
(the **rudder** sensor) reports rudder angle from an AS5600 magnetic sensor
(boat is wheel-steered); a fourth (the **wind** sensor) sits at the masthead and
reports apparent wind. They talk to each other over Wi-Fi using UDP.

```
   sensors                     Wi-Fi (SoftAP "SoberPilot", 10.20.1.x)
  ┌─────────┐                 ┌───────────────────────────────────────┐
  │ compass │                 │  UDP 8888  telemetry  ~APDAT,...$  ─▶ │
  │  (IMU)  │                 │            (controller broadcasts)    │
  │  GPS    │   ┌──────────┐  │                                       │   ┌──────────┐
  │  Garmin │──▶│CONTROLLER│──┤                                       ├──▶│ DISPLAY  │
  │  NMEA   │   │  (AP)    │  │  UDP 8889  commands   ~APCMD,...$  ◀─ │   │  (STA)   │
  └─────────┘   └────┬─────┘  │            (display sends)            │   └────┬─────┘
                     │        └───────────────────────────────────────┘        │
                steering motor                                         HX8357 LCD + buttons
```

## The four sketches

### `controller/` — sensors, navigation, and steering
Acts as the Wi-Fi access point (`SoftAP` SSID **SoberPilot**, `10.20.1.x`). It
reads the sensors, decides how to steer, runs the motor, and broadcasts the boat
state to any displays on the network. Source files:

| File | Responsibility |
|------|----------------|
| `controller.ino` | `setup()`/`loop()`, FreeRTOS task wiring, top-level orchestration |
| `compass.ino` | Reads the Adafruit BNO08x IMU for heading / pitch / roll + stability |
| `gps.ino` | Parses GPS NMEA (fix, position, speed, course) via Adafruit GPS |
| `garmin.ino` | Ingests Garmin NMEA-0183 sentences (waypoint / bearing data) |
| `pid.ino` | PID loop converting heading error into a steering correction |
| `motor.ino` | Drives the steering motor (direction + timed pulses) |
| `publish.ino` | Builds and **broadcasts** `~APDAT,...$` telemetry on UDP 8888 |
| `subscribe.ino` | **Listens** on UDP 8889 for `~APCMD,...$` commands from displays |
| `telnet.ino` | Telnet console for live debugging |
| `wifi.ino` | Brings up the SoftAP |
| `rudder.ino` | Listens for `~APRUD,...$` from the rudder sensor board on UDP 8890, remembers its IP, relays `~APCMD,z$` back to it on UDP 8891 |
| `wind.ino` | Listens for `~APWND,...$` from the wind sensor board on UDP 8892, remembers its IP, relays `~APCMD,v$`/`,d`/`,k` back to it on UDP 8893 |
| `AutoPilot.{h,cpp}` | Thread-safe shared state model (mutex-guarded getters/setters) |

### `display/` — cockpit head unit
Joins the controller's Wi-Fi as a station, shows the live data on the HX8357 LCD,
and sends button presses back as commands. Buttons switch between navigation and
compass mode (or disable the autopilot) and, in compass mode, turn 1°, 10° or 90°
to port or starboard. To keep the UI feeling instant it updates its own local copy
of the state immediately on a press, then transmits the change.

| File | Responsibility |
|------|----------------|
| `display.ino` | `setup()`/`loop()`, top-level orchestration |
| `screen.ino` | Renders the LCD layout (Adafruit GFX + HX8357 driver, custom fonts) |
| `button.ino` | Reads the physical buttons, applies optimistic local updates |
| `command.ino` | Sends `~APCMD,...$` command datagrams to the controller on UDP 8889 |
| `subscribe.ino` | Listens on UDP 8888 for the controller's `~APDAT,...$` telemetry |
| `volt_meter.ino` | Battery / input voltage measurement and display |
| `wifi.ino` | Connects to the **SoberPilot** access point |
| `AutoPilot.{h,cpp}` | Local mirror of the state model + the `~APDAT/~APCMD` parser |

### `rudder/` — rudder angle sensor

A standalone board, not part of the controller/display protocol. Joins the
SoberPilot Wi-Fi as a station and reports rudder angle straight to the
controller over its own pair of UDP ports (8890/8891) — see the `autopilot`
skill for the full protocol and calibration design. Source files:

| File | Responsibility |
|------|----------------|
| `rudder.ino` | `setup()`/`loop()`, top-level orchestration |
| `angle.ino` | AS5600 read + calibration (offset persisted via `Preferences`) |
| `publish.ino` | Sends `~APRUD,...$` to the controller on UDP 8890 |
| `subscribe.ino` | Listens on UDP 8891 for the relayed `~APCMD,z$` "center now" |
| `wifi.ino` | Connects to the **SoberPilot** access point |

Wiring: AS5600 powered from the Nano ESP32's **3V3 pin** (not 5V — the
module's I2C pull-ups would overvoltage the ESP32's 3.3V-only GPIOs), SDA/SCL
to the Nano's dedicated SDA/SCL pins, DIR tied to GND.

### `wind/` — masthead wind sensor

A standalone board, like the rudder sensor: joins SoberPilot as a station and
unicasts apparent wind to the controller on its own pair of UDP ports
(8892/8893). The wind maths is a port of Norbert Walter's
[Windsensor Yachta](https://github.com/norbert-walter/Windsensor_Yachta)
firmware from the ESP8266, minus its HTTP server, settings pages, OTA updater
and NMEA TCP server (a phone-facing interface will come back over Bluetooth).

| File | Responsibility |
|------|----------------|
| `wind.ino` | `setup()`/`loop()`, FreeRTOS task wiring, sample cadence |
| `Wind.{h,cpp}` | Thread-safe state model **plus all the wind maths** ported from the original's `Calculation.h` |
| `vane.ino` | AS5600 vane angle + bow zero and trim (offset persisted via `Preferences`) |
| `anemometer.ino` | Reed-switch pulse interrupt, rotation timing, zero-wind detection |
| `temperature.ino` | DS18B20 air temperature on 1-Wire (non-blocking conversions) |
| `publish.ino` | Sends `~APWND,...$` to the controller on UDP 8892 |
| `subscribe.ino` | Listens on UDP 8893 for the relayed `~APCMD,v$` "vane is dead ahead" |
| `wifi.ino` | Connects to the **SoberPilot** access point |

Wiring (Yachta head): AS5600 vane encoder from **3V3** on the Nano's dedicated
SDA/SCL pins (same constraints as the rudder board); reed switch between **D2**
and GND (internal pull-up; the Yachta build also fits a 10k/100n snubber);
DS18B20 data on **D4** with a 4k7 pull-up to 3V3. Board power is 12 V from the
masthead light circuit. Unlike the controller, this sketch names its pins `D2`
/`D4` rather than bare integers, so it builds correctly under **either** Pin
Numbering setting. The sensor board ([`../circuit/Sensor-Wind/`](../circuit/Sensor-Wind/))
fits a **Hall switch** rather than a reed switch on D2 — same open-collector
pulse per magnet pass, so the firmware does not care which is fitted.

**Calibration.** Nothing here can be determined before the head is built, so
all of it is runtime commands persisted in NVS rather than build constants —
you should never have to reflash a unit that is up a mast. None is relayed by
the controller yet; for now send them straight to UDP 8893 on the board.

*Vane zero* — `~APCMD,v$`, sent with the vane pointing down the centreline.
Stores the offset in raw AS5600 counts (so re-applying never accumulates
rounding error). Exactly the shape of the rudder board's `~APCMD,z$` centring.
Precise, but it needs a hand on the vane, so it's the **bench-time** form.

*Vane trim* — `~APCMD,d<±degrees>$` shifts the reported angle without touching
the vane, for once the head is up the mast. It also corrects what `v` **can't**:
`v` only fixes the vane's alignment to the sensor body, while the error the boat
actually experiences is that plus any rotation of the mast relative to the hull
plus the aerodynamic bias of sitting in the mast/mainsail upwash — neither of
which is visible from the masthead.

Measure the total by **tacking**. A constant offset makes the two tacks
disagree. Sail close-hauled on starboard in steady breeze and flat water,
average the angle off the bow; tack; repeat on port with identical trim. The
offset is *half* the difference (half, because the error shifts both readings
the same way in absolute terms while the two tacks measure from opposite
sides). Send its negation:

| | Reading | Off the bow |
|---|---|---|
| Starboard | 35° | 35° |
| Port | 335° | 25° |

Spread 10° → offset +5° → `~APCMD,d-5$` → both tacks then read ~30°.

It's *relative* because a difference is the only thing the measurement yields —
the tack test never tells you an absolute encoder offset. Average over a minute
or more (log the 5 Hz stream and take the mean); a single reading is noise. Two
caveats: leeway, uneven trim, current or lumpy water on one board all
masquerade as an offset, so don't bake a trim habit into the instrument; and a
single constant can't represent upwash, which varies with wind speed and point
of sail, so the correction is most accurate near the angle you calibrated at.

*Wind speed* — `~APCMD,k<slope>,<offset>$`, applying `speed[m/s] = raw *
slope + offset`. The cup geometry in `Wind.h` is the **nominal** Yachta design,
not a measurement of your head: `ANEMOMETER_RADIUS_M` is honest (measure the
arm) but `ANEMOMETER_LAMBDA` is where cup shape and size hide and has no closed
form, so the slope absorbs the difference. The offset absorbs what no slope
can — bearing friction and start-up threshold make the real response `v = a +
b·n`, not a line through the origin. Fit it off-board: log the sensor against
a reference at several steady speeds and take the linear fit. The original
project's documented method is a car on a windless day with a GPS speed app as
reference, sensor on a pole clear of the car's pressure field. `speed_hz` on
`~APWND` is the raw, uncorrected quantity to log — it stays re-fittable even
once a calibration is in force. Values outside a sane band are rejected rather
than written to flash. There is no ack: confirm by watching `speed_kn` move
against an unchanged `speed_hz` in the next packet.

Note the two "offsets" are unrelated despite the shared word — the vane offset
is an angle in encoder counts, the speed offset is a scalar in m/s.

## Communication protocol

Plain-text UDP datagrams framed with a leading `~` and trailing `$`:

- **Telemetry** — controller → displays, broadcast on **UDP 8888**:
  `~APDAT,<fields...>$` (mode, fix, heading, pitch/roll, bearing, position,
  speed, distance, etc. — see `publish.ino`/`AutoPilot::parse`).
- **Commands** — display → controller, unicast on **UDP 8889**:
  `~APCMD,<cmd>$` (mode changes, heading nudges, tack, etc.).
- **Reset** — `~RESET,1$`.
- **Rudder sensor** — its own pair of ports, separate from the pair above:
  `~APRUD,<angle>,<magnet_ok>$` rudder → controller on **UDP 8890**, and
  `~APCMD,z$` ("center now") controller → rudder on **UDP 8891**
  (`controller/rudder.ino`). Rudder angle also rides along on `~APDAT` as two
  trailing fields (`rudder_angle`, `isRudderOk`) for displays/plugin -
  `isRudderOk` combines the sensor's magnet-detected flag with a 1s receive
  timeout, so a disconnected rudder board reads as "no data," not a frozen
  stale value. See the `autopilot` skill for the full design (relay rationale,
  calibration math, timeout details).
- **Wind sensor** — again its own pair of ports:
  `~APWND,<direction>,<speed_kn>,<speed_mps>,<bft>,<temp_c>,<vane_ok>,<temp_ok>,<speed_hz>$`
  wind → controller on **UDP 8892**, and `~APCMD,v$` / `~APCMD,d<±degrees>$` /
  `~APCMD,k<slope>,<offset>$`
  controller → wind on **UDP 8893**. `direction` is apparent wind angle,
  0–360° clockwise from the bow; `speed_hz` is the raw anemometer rate, on the
  wire for calibration rather than steering. The two health flags are separate
  because the failures are independent — a dead vane costs direction but not
  speed. Neither covers "the board stopped transmitting"; that is a receive
  timeout on the controller side, same as `isRudderOk`.

Because telemetry is broadcast, multiple displays can listen at once; commands
are unicast to the controller's AP address.

### Two command surfaces

`~APCMD` over UDP and the controller's **telnet console** are separate
dispatchers with separate verb sets — adding a verb to one does not add it to
the other. The sensor-board calibration verbs (`z` for the rudder, `v`/`d`/`k`
for the wind) are on **both**; the OpenCPN plugin also sends `z`. The displays
send only `a`, `m`, `n`, `t` — they have buttons, not a keyboard.

Telnet is the practical place to calibrate: it echoes the full status after
every state-changing command, so you can see what actually landed. `~APCMD` is
fire-and-forget with no ack by design, so nothing on that path can tell you.

## Before you build: `arduino_secrets.h`

Each sketch needs an `arduino_secrets.h` with the Wi-Fi password — **it must
match on the controller, every display, the rudder sensor and the wind
sensor**. Copy the example to get started:

```bash
cp controller/arduino_secrets.h.example controller/arduino_secrets.h
cp display/arduino_secrets.h.example    display/arduino_secrets.h
cp rudder/arduino_secrets.h.example     rudder/arduino_secrets.h
cp wind/arduino_secrets.h.example       wind/arduino_secrets.h
# then edit each and set the password
```

> **Controller pin numbering:** in the Arduino IDE, set **Tools ▸ Pin Numbering**
> to **"By Arduino pin (default)"** when building, or the motor/GPS
> pin assignments will be wrong.
> ![Tools_Pin_Numbering](../assets/ArduinoIDE_PIN_mode.png)

## Required libraries

These are **not** checked into the repo. Either let `arduino-cli` install them
from the `sketch.yaml` profiles (below), or add them by hand in the Arduino IDE
via **Tools ▸ Manage Libraries…** using the names in the table. Everything else
the sketches `#include` (`WiFi`, `AsyncUDP`, `SPI`, `Wire`, `USB`,
`HardwareSerial`, `FreeRTOS`, …) ships with the ESP32 core — no install needed.

| Library (Library Manager name) | Author | Used by |
|--------------------------------|--------|---------|
| Adafruit BNO08x | Adafruit | controller |
| Adafruit GPS Library | Adafruit | controller |
| ESP Telnet | Lennart Hennigs | controller |
| PID | Brett Beauregard | controller |
| Time | Michael Margolis | controller, display |
| Timezone | Jack Christensen | controller |
| Adafruit GFX Library | Adafruit | display |
| Adafruit HX8357 Library | Adafruit | display |
| Adafruit AS5600 Library | Adafruit | rudder, wind |
| OneWire | Paul Stoffregen | wind |
| DallasTemperature | Miles Burton | wind |

Installing the Adafruit libraries also pulls in **Adafruit BusIO** (all four
sketches) and **Adafruit Unified Sensor** (controller) as dependencies (the IDE
offers to add them automatically; the `sketch.yaml` profiles list them
explicitly). Pin **DallasTemperature** to the 3.x line — 4.x changes the API.

## Building with arduino-cli

Each sketch has a `sketch.yaml` defining a profile named **`nano`** that pins the
board (`arduino:esp32:nano_nora`), the core, and the libraries above.

**One-time setup** (install arduino-cli and the ESP32 core index):

```bash
brew install arduino-cli                                   # macOS
arduino-cli config init
arduino-cli config add board_manager.additional_urls \
  https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index
```

**Compile** (the first run auto-installs the core + every library in the
profile — no `Arduino/libraries` folder required):

```bash
cd Arduino/controller        # or Arduino/display, Arduino/rudder, Arduino/wind
arduino-cli compile --profile nano
```

**Find your board's port, then upload:**

```bash
arduino-cli board list                                     # e.g. /dev/cu.usbmodem1101
arduino-cli upload --profile nano -p /dev/cu.usbmodem1101
```

Since each `sketch.yaml` sets `default_profile: nano`, you can drop the
`--profile nano` flag once you're used to it.

> Bump the version pins in `sketch.yaml` as new releases come out
> (`arduino-cli lib search "<name>"` shows what's available). To install just the
> libraries without compiling: `arduino-cli lib install "Adafruit BNO08x"` etc.
