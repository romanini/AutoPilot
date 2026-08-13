# Button / LED board — `SM Button board V2`, rev 1.0

The board behind the cockpit buttons. It hosts the five physical buttons, the
illuminated power switch connector, and **passes the TFT's SPI bus through** to
the display panel.

![Button board schematic](SCH_Display-Button_PNG.png)

![Button board PCB](PCB_Display-Button_PNG.png)

## Components

| Ref | Part | Role |
|-----|------|------|
| 5 × | **K2-1107ST** SMD tactile switches | The cockpit buttons: **Port**, **Enable** (compass), **Mode** (GPS), **Starboard**, **Tack** |
| **U3** | JST-XH 8-pin | Connector to the TFT — passes D/C, CS, MOSI, MISO, CLK, Vin, GND |
| **CN2** | **XY2500R-T-2.5-4P** | **Illuminated power switch.** Pins 1/2 (net-named `5Volt` / `5Voltswitched`, but actually GND and the TPS61023's `EN` on the mainboard) are the switch contacts; pins 3/4 are its LED, driven by the TP5100's `LED+` / `STDBY` charge-status pins |
| — | HDR-M 2×10 (**Display Board**) | Ribbon back to the display mainboard |

Functionally this board sits between the display mainboard and the panel: the
2×10 ribbon brings the button signals and SPI bus over from the mainboard, the
buttons tie into the matching net labels, and the SPI bus continues out of U3 —
either straight to an HX8357 module, or to the
[LCD carrier board](../Display-LCD/README.md). CN2 powers the button backlights.

One naming trap: the display mainboard calls the fifth button's net
`BacklightBtn`, but the firmware currently reads that pin (`D4`) as
`NAVIGATION_DISABLE_BUTTON_PIN` — today it is the **Enable** button. The net
name is deliberate and forward-looking: once these boards are in, enable/disable
moves to the motor **kill switch** on the controller, which frees this button up
to do what its net says.

---

One of the [AutoPilot circuit boards](../README.md) — that README covers how the
boards fit together and the open review items. It shares a case with the
[display mainboard](../Display/README.md). Firmware:
[`../../firmware/Arduino/README.md`](../../firmware/Arduino/README.md). Enclosure: [`../../cad/display/`](../../cad/display/README.md).
