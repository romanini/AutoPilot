# LCD carrier — `AutoPilot LCD Carrier`, v1.0

Adapts the **VIEWE UEED035HV-RX40-L001A** panel (Sitronix **ST7365P**
controller, 320×480) to the same 8-pin JST-XH the button board already speaks.
It is the alternative to fitting an HX8357 breakout — the display firmware
detects which panel is present at boot (`tft.ino`) and drives either one.

![LCD carrier schematic](SCH_Display-LCD_PNG.png)

![LCD carrier PCB](PCB_Display-LCD_PNG.png)

## Components

| Ref | Part | Role |
|-----|------|------|
| **U3** | JST-XH 8-pin (**Input**) | From the button board: `BL_PWM`, `D/C`, `CS`, `MOSI`, `MISO`, `CLK`, Vin (5 V), GND |
| **FPC1** | **HRS FH12A-40S-0.5SH(55)** | 40-pin 0.5 mm FPC connector the panel's flex tail plugs into |
| **U1** | **TI TPS7A0333DBVR** | 3.3 V LDO — the panel logic runs at 3V3 while the ribbon arrives at 5 V (C1 10 µF in, C2 1 µF, C3 10 µF out) |
| **Q1** | **AO3400A** N-channel MOSFET | Low-side switch for the **backlight**, gated by `BL_PWM` (R4 100 Ω gate series, R5 100 kΩ pull-down) |
| **R1–R3** | 51 Ω | Ballast for the three backlight LED strings (FPC 34/35/36; anodes on 5 V at FPC 33) |
| **R7–R9** | 0 Ω | `IM[2:0]` strapped to 3V3 = **111 → 4-line SPI**. Alternate pads to GND re-strap the panel to 8080 8-bit later |
| **R6 / C8** | 100 kΩ / 100 nF | Power-on **reset** for the panel (FPC 15) |
| **R10** | 10 kΩ | `MISO` pull-down |
| **C4–C7** | 100 nF / 10 µF | Panel 3V3 and backlight-rail decoupling |

Only the 4-line SPI pins are wired: `CS` (9), `SCK` (10), `D/C` (11), `MOSI`
(13) and `MISO` (14). The unused parallel bus `DB[17:0]` (FPC 16–32) is tied to
**GND**, which the panel's datasheet §8.4 requires, and the four capacitive-touch
pins (FPC 1–4) are left unconnected — this panel variant has no touch layer.

---

One of the [AutoPilot circuit boards](../README.md) — that README covers how the
boards fit together and the open review items. It is the optional third board in
the display unit, fed from the [button board](../Display-Button/README.md).
Firmware: [`../../Arduino/README.md`](../../Arduino/README.md).
