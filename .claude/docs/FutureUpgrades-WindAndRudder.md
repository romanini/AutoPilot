# Future Upgrades Research: Wind Instrument & Rudder Angle Sensor

*Researched 2026-07-05. Prices are US street prices as of research date.*

Two planned additions to the AutoPilot system:
1. **Wind speed + direction** → cockpit display + OpenCPN
2. **Rudder angle sensor** → controller uses actual rudder position in steering

Boat is wheel-steered (pedestal + cable/quadrant), which shapes the rudder options: the
sensor must live at the **rudder stock/quadrant below deck**, never at the wheel (cable
slack and lost motion make wheel position an unreliable proxy for rudder angle).

---

## How each integrates with our architecture

The system's strength is the SoberPilot Wi-Fi + UDP plain-text protocol. Both sensors
slot in cleanly:

**Wind** — best path is a sensor that speaks NMEA 0183:
- A Wi-Fi sensor joins SoberPilot as another station and sends `MWV` sentences over
  TCP/UDP. The controller parses it (same pattern as `garmin.ino` parses Garmin NMEA),
  adds `wind_speed`/`wind_angle` fields to `~APDAT`, and the TFT head unit +
  `autopilot_pi` both display it for free. OpenCPN adds a second network connection
  (Options → Connections → Network, UDP/TCP) pointed at the sensor and gets apparent
  wind natively; its dashboard computes true wind from SOG/heading.
- A wired NMEA 0183 sensor instead runs down the mast to either a controller UART
  (third NMEA input) or a USB-serial dongle on the OrangePi.
- NMEA 2000-only sensors are a poor fit — we have no N2K backbone, so they'd need a
  ~$150–250 gateway plus power/backbone hardware.
- Long-term bonus: wind data in the controller enables a future **wind-vane steering
  mode** (hold apparent wind angle instead of compass heading) — mode 3.

**Rudder** — consumed directly by the controller PID:
- Analog voltage (potentiometer or hall sensor) into a Nano ESP32 ADC pin, or an
  AS5600 magnetic encoder on I2C (only if the mounting point is within ~1 m of the
  controller; I2C doesn't like long runs — use the AS5600's analog-out mode or a pot
  for longer runs).
- Add a `rudder_angle` field to `~APDAT` (update `publish.ino`, display parser, and
  `autopilot_pi` `ParsePacket()`/`AutoPilotState` together).
- The real payoff: convert steering from time-based motor pulses to a **cascaded
  loop** — outer loop maps heading error → commanded rudder angle, inner loop drives
  the motor until actual rudder = commanded. Gives end-stop protection (no motor
  stall at the stops), no drift in assumed rudder position, and tighter gains.

---

## Wind instrument options

### W1. DIY "Yachta" wind sensor (Open Boat Projects) — ~$40–80
Cup anemometer + wind vane, 3D-printed, AS5600 magnetic encoder for vane angle
(0.1° resolution, contactless), ESP8266, NMEA 0183 over Wi-Fi (TCP 6666), 12 V from
the masthead light circuit (~50 mA). Open hardware; PCB orderable from Aisler; 30+
built and sailing. Related variants: Norbert Walter's "WiFi 1000", the Ventus W132
conversion (~€50), InqWind, and a universal firmware that covers several of them.
- **Pros:** cheapest by far; *perfect* architecture fit (joins SoberPilot, no signal
  cable down the mast — only 12 V, usually already there for the anchor light);
  fully hackable — controller can ingest it and rebroadcast via `~APDAT`; active
  community + documented builds.
- **Cons:** you build it — 3D printing (UV-stable material: ASA/PETG), soldering,
  bearing assembly, calibration; moving parts at the masthead, the least serviceable
  spot on the boat; DIY waterproofing/longevity is on us; ESP8266 must reliably
  associate with the controller AP at mast height (should be fine — it's line of sight).

### W2. Digital Yacht WND100 — ~$387
Conventional vane/cup masthead unit, wired NMEA 0183 output, 20 m thin cable included.
- **Pros:** commercial reliability at the lowest wired-commercial price; NMEA 0183
  plugs straight into a controller UART (like the Garmin input) or a $15 USB-serial
  on the OrangePi; no gateways, no ecosystem lock-in.
- **Cons:** cable pull down the mast and to the cockpit/nav area; wind data lands on
  one consumer first — needs the controller-rebroadcast pattern to reach everything.

### W3. LCJ Capteurs CV7 (sailboat version) — ~$585–650
Ultrasonic, no moving parts, NMEA 0183 serial output, oblique masthead arm, very low
power. Strong multi-season reliability reports from sailors.
- **Pros:** nothing to wear or freeze at the masthead; NMEA 0183 = same easy
  integration as WND100; excellent light-air sensitivity.
- **Cons:** price; ultrasonic sensors can misread in heavy rain; still a wired install.

### W4. Calypso Ultrasonic (wired ~$550 / portable BLE ~$300–400)
Wired version is NMEA 0183; portable is solar + Bluetooth LE.
- **Pros:** wired version comparable to CV7; portable version needs zero mast wiring
  (solar powered) — an ESP32 BLE→UDP bridge is a fun afternoon project and Calypso's
  BLE protocol is documented.
- **Cons:** reports of fiddly alignment/integration; BLE range masthead→cockpit is
  marginal on some rigs; portable version's battery is another thing to manage.

### W5. B&G WS320 wireless — $524 sensor / $640 with interface
- **Pros:** wireless, solar, race-pedigree sensor.
- **Cons:** proprietary radio to its own interface which outputs **NMEA 2000 only** —
  we'd add a gateway (~$150–250) plus N2K power/backbone. Worst architecture fit
  along with Raymarine i60/gWind packages (~$987, own display, SeaTalkNG/N2K).
  Not recommended for this boat.

### Cockpit display for wind
No new hardware needed: add wind fields to `~APDAT` and render on the existing HX8357
head unit(s) and in `autopilot_pi`. (If a dedicated wind dial is ever wanted, Open Boat
Projects' OBP60 exists, but it's redundant here.)

---

## Rudder angle sensor options

### R1. DIY AS5600/AS5048 directly over the rudder stock — ~$10–25
Diametral magnet epoxied to the top of the rudder stock (or a cap on it), sensor board
on a small bracket 1–3 mm above, reading absolute angle contactlessly.
- **Pros:** cheapest; **zero linkage** — no arms, ball joints, or geometry to get
  wrong; no wear; 12-bit (0.1°) absolute angle, survives power cycles; I2C or analog
  out into the controller.
- **Cons:** needs clear access to the top of the stock — on many wheel boats that spot
  is the **emergency tiller fitting; must not obstruct it** (a swing-away bracket
  solves this); mounting gap tolerance is tight for a part that lives in a damp
  lazarette — needs a potted/conformal-coated board; if the stock top is >1 m of cable
  from the controller, use analog-out mode or a pot instead of I2C.

### R2. DIY potentiometer + linkage arm on the quadrant — ~$20–60
Classic commercial-RPS geometry, self-built: Bourns 3590S-type 10k precision pot
(clones ~$5 on AliExpress; genuine ~$25), stainless arm, two ball-joint rod ends,
threaded rod to a bolt on the quadrant. Three wires to a controller ADC pin
(ratiometric divider off 3.3 V).
- **Pros:** dead simple electrically; analog runs any cable length; mounts wherever
  the quadrant is reachable; proven approach (it's what commercial units are inside —
  pypilot notes marine actuator pots are literally 3590S clones); 1 kΩ series resistor
  at the wiper protects against end-stop burnout.
- **Cons:** fabrication of arm/bracket; linkage geometry rules (pivot in line with
  rudder axis, sized so full rudder travel stays inside pot travel — over-rotation
  breaks sensors); pot wiper wears over years; needs a protective cowl per MarineHowTo.

### R3. pypilot hall-effect rudder feedback — ~$40–60
Sean D'Epagnier's waterproof hall-sensor + magnet unit (pypilot store), analog voltage
output, made specifically for DIY autopilots.
- **Pros:** contactless (no wear) *and* pre-built/waterproof; analog out drops straight
  onto a Nano ESP32 ADC; designed for exactly this use case; supports pypilot-style
  linkage or direct-shaft mounting.
- **Cons:** small-shop availability can be spotty; still needs linkage/bracket
  fabrication like R2.

### R4. Commercial rudder reference unit — $300–395
Raymarine M81105 rotary rudder reference (~$300–383, 10 m cable) or Simrad RF25
(~$394). Internally they're precision pots (1k–100k range) with marine-grade linkage
hardware; the 3-wire output can feed our own ADC — no Raymarine/Simrad brain required.
- **Pros:** fully marinized, robust arm/ball-joint hardware included, proven for
  decades, drop-in mounting instructions everywhere (MarineHowTo has a full guide).
- **Cons:** 10–20× the price of R1/R2 for what is, to our controller, just a
  potentiometer; RF25's SimNet features are wasted on us (its analog/pot side is what
  we'd use).

### Rejected: wheel/motor-shaft encoder as rudder proxy
Free (we already command the motor), but steering-cable stretch, slack, and slippage
mean wheel position ≠ rudder position exactly when it matters (hard over, heavy
weather). Rudder truth must come from the stock/quadrant.

---

## Cost summary

| Option | Cost | Architecture fit | Build effort |
|---|---|---|---|
| **W1 Yachta DIY Wi-Fi** | **$40–80** | Perfect (joins SoberPilot) | High (print+solder) |
| W2 Digital Yacht WND100 | $387 | Good (NMEA 0183 wired) | Low (mast cable pull) |
| W3 LCJ CV7 ultrasonic | $585–650 | Good (NMEA 0183 wired) | Low |
| W4 Calypso wired / BLE | $550 / ~$350 | Good / needs BLE bridge | Low / Medium |
| W5 B&G WS320 (+gateway) | $640 + ~$200 | Poor (N2K only) | Medium |
| **R1 AS5600 on stock** | **$10–25** | Perfect (I2C/analog to controller) | Medium (bracket) |
| R2 Pot + quadrant arm | $20–60 | Perfect (analog) | Medium (linkage) |
| R3 pypilot hall unit | $40–60 | Perfect (analog) | Low-Medium |
| R4 Raymarine M81105 / Simrad RF25 | $300–395 | Fine (use as a pot) | Low |

## Recommendation

- **Wind:** build the **Yachta (W1)**. It matches the project's DIY/Wi-Fi-first DNA,
  costs a tenth of commercial, and its NMEA-over-Wi-Fi output feeds the controller,
  head unit, plugin, and OpenCPN with no new wiring beyond masthead 12 V. Fallback if
  masthead serviceability worries win: **WND100 (W2)** wired into a controller UART.
- **Rudder:** inspect the top of the rudder stock first. If accessible → **AS5600
  (R1)**. If only the quadrant is reachable or the cable run is long → **pot + arm
  (R2)** or the **pypilot unit (R3)**. Either way it's a <$60 sensor feeding a
  controller ADC, plus the firmware work (new `~APDAT` field + cascaded rudder loop),
  which is where the real autopilot performance gain lives.

## Firmware/software work implied (both projects)

1. `~APDAT` gains fields (wind speed, wind angle, rudder angle) — update
   `controller/publish.ino`, `display/AutoPilot.cpp::parseAPDAT`, and
   `autopilot_pi` `AutoPilotState` + `ParsePacket()` **together**.
2. Controller: new `wind.ino` (NMEA MWV parse over UDP/TCP or UART, garmin.ino
   pattern) and `rudder.ino` (ADC/I2C read + calibration: center offset, degrees/volt,
   end stops stored in NVS).
3. `pid.ino`: optional cascaded control — heading error → target rudder angle →
   motor drive until achieved; end-stop clamp protects the motor.
4. Display + plugin: wind page/fields, rudder angle bar graph.
5. OpenCPN: add network connection for the wind sensor's NMEA stream (no plugin
   change needed for wind — built-in dashboard handles MWV).

## Sources

- Open Boat Projects DIY wind sensor: https://open-boat-projects.org/en/diy-windsensor/
- Windsensor Yachta: https://open-boat-projects.org/en/windsensor-yachta/
- Yachta assembly guide: https://open-boat-projects.org/en/zusammenbauanleitung-windsensor-yachta/
- Universal wind sensor firmware: https://open-boat-projects.org/en/universelle-windsensor-firmware/
- Yachta GitHub: https://github.com/norbert-walter/Windsensor_Yachta
- Ventus W132 conversion (Segeln-Forum): https://www.segeln-forum.de/thread/81141-boots-windsensor-ventus-w132-f%C3%BCr-50-euro/
- InqWind AS5600 sensor: https://inqonthat.com/inqwind-the-no-brainer-sensor-as5600/
- Digital Yacht WND100: https://digitalyachtamerica.com/product/wnd100-wind-sensor/ (~$387: https://nvnmarine.com/products/92155-digital-yacht-wnd100-mast-head-unit)
- LCJ CV7: https://lcjcapteurs.com/en/girouette-anemometres-capteur-vent/cv7-ultrasonic-wind-sensor/ (pricing: https://www.svb24.com/en/lcj-capteurs-cv7-ultrasonic-wind-sensor-sailboat-version.html)
- Calypso NMEA 2000/wired wind (Panbo): https://panbo.com/calypso-instruments-ultrasonic-nmea-2000-wind-meter-plug-and-play-wind/
- Calypso portable (Panbo): https://panbo.com/calypso-ultrasonic-portable-wireless-wind-and-more/
- B&G WS320: https://www.bandg.com/bg/type/instrument-sensors-and-transducers/wind-sensors/ws320-wireless-wind-sensor/ (pricing: https://www.tradeinn.com/waveinn/en/b-g-ws320-wireless-wind-sensor-only/136911957/p)
- Raymarine i60 wind pack (~$987): https://www.hodgesmarine.com/raye70150-raymarine-i60-wind-display-system-wmasthead-wind.html
- Ultrasonic sensor field reports: https://forums.sailinganarchy.com/threads/ultrasonic-wind-sensors.239426/
- MarineHowTo RPS install guide: https://marinehowto.com/installing-an-autopilot-rudder-position-sensor/
- Raymarine M81105 (~$300–383): https://www.svb24.com/en/raymarine-rudder-position-transducer.html / https://www.boatid.com/raymarine/rotary-rudder-feedback-mpn-m81105.html
- Simrad RF25 (~$394): https://www.hodgesmarine.com/sim22014286-simrad-rf25-rudder-reference.html
- pypilot rudder feedback discussion: https://www.cruisersforum.com/forums/f134/rudder-position-pypilot-238333.html / https://forum.openmarine.net/showthread.php?tid=1414
- pypilot user manual: https://pypilot.org/doc/pypilot_user_manual/
- AS5600 sensor: https://www.adafruit.com/product/6357 / https://learn.adafruit.com/adafruit-as5600-magnetic-angle-sensor/overview
- YBW rudder feedback thread (3590S pot clones): https://forums.ybw.com/threads/autopilot-rudder-position-feedback.606707/
