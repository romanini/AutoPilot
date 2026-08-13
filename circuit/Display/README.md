# Display mainboard — `SM AP Display`, rev 2.2

The cockpit head-unit board. It carries a second Arduino Nano ESP32, drives the
TFT over SPI, and includes an on-board **LiPo battery + charger** so the display
can run with or without 12 V present. It also measures both the 12 V input and
the battery voltage.

![Display schematic](SCH_Display_PNG.png)

![Display PCB](PCB_Display_PNG.png)

## Components

| Ref | Part | Role |
|-----|------|------|
| **U1** | **Arduino Nano ESP32** | Runs the display firmware; SPI to the TFT (D/C, CS, MOSI, MISO, CLK) |
| **U3** | **TP5100 module** | 1–2 A Li-ion/LiPo battery charger (IN+/IN−, BAT+/BAT−, CHRG/STDBY status) |
| **TPS61023** | **Adafruit MiniBoost 5 V @ 1 A** | Boost converter — steps the LiPo up to 5 V to run the board (`+5V switched`) |
| **D1 / D2** | **1N5817** Schottky diodes | OR the 12 V input and USB 5 V onto `V_in`, the TP5100's **charger** input — neither one runs the board directly |
| **Q1** | **2N2222A** NPN transistor | Drives the buzzer from a GPIO |
| **P1** | Buzzer | Audible alert |
| **R (BUZZER-R1)** | 1 kΩ | Base resistor for Q1 |
| **V_IN_R1 / V_IN_R2** | 10 kΩ / **1.8 kΩ** | Voltage divider — measures the **12 V input** on `A1` (ratio 0.1525) |
| **BATTERY_R1 / BATTERY_R2** | 10 kΩ / **12 kΩ** | Voltage divider — measures the **LiPo battery** on `A0` (ratio 0.5455) |
| **U2** | **CH224K** | USB-C PD sink — negotiates on `CC1`/`CC2` (it provides both Rd terminations, so no external 5.1 kΩ), `CFG1` = 56 kΩ selects the requested voltage |
| — | JST-PH 2-pin (**BATT**) | LiPo battery connection |
| — | HDR-M 2×10 (**Button Board**) | Ribbon to the button / TFT pass-through board |
| — | JST-XH 1×3 / 1×5 | USB + power break-outs |

The two voltage dividers are what the firmware's `volt_meter.ino` reads to show
input and battery voltage on the LCD. **The ratios in that file (0.1803 and
0.6875, i.e. 2.2 kΩ and 22 kΩ) do not match the resistors on this schematic** —
see the [review notes](../README.md#review-notes-open-items).

**The display always runs off its battery.** 12 V and USB are *charging* inputs
only: both feed `V_in` through their Schottky diodes, `V_in` goes to the
TP5100's `IN+` and nowhere else, and the board's own `+5V switched` rail comes
from the TPS61023 boosting the LiPo. Pulling 12 V and USB does not turn the
display off, and the boost's `EN` line (from the button board) is what actually
switches the unit on.

---

One of the [AutoPilot circuit boards](../README.md) — that README covers how the
boards fit together and the open review items. It shares a case with the
[button board](../Display-Button/README.md) and, optionally, the
[LCD carrier](../Display-LCD/README.md). Firmware:
[`../../Arduino/README.md`](../../Arduino/README.md). Enclosure: [`../../cad/display/`](../../cad/display/README.md).
