# Wind sensor

Masthead **wind speed and direction** sensor for the AutoPilot project.

A cup anemometer and a wind vane on a common 3D-printed housing, both read
magnetically — no slip rings, no exposed contacts. Four alternating magnets on
the cup-wheel shaft sweep past a hall sensor for speed; a diametric magnet on the
vane shaft is read by an **AS5600** magnetic encoder for direction (0.1°,
contactless). An **Arduino Nano ESP32** on the board joins the controller's
`SoberPilot` Wi-Fi and reports over UDP, so the only thing that runs down the
mast is 12 V — usually already there for the anchor light.

Derived from the **Yachta** wind sensor by Norbert Walter
([Open Boat Projects](https://open-boat-projects.org/en/zusammenbauanleitung-windsensor-yachta/)),
rebuilt around the Nano ESP32 to match the rest of this project.

**→ [Assembly.md](Assembly.md) — full build instructions.**

---

## Where it fits in AutoPilot

| | |
|---|---|
| Joins | `SoberPilot` Wi-Fi as a station, like a display or the rudder sensor |
| Sends | wind speed + angle to the controller |
| Ends up in | `~APDAT` telemetry → TFT head unit, and `autopilot_pi` in OpenCPN |
| Enables later | a wind-vane steering mode — hold an apparent wind angle instead of a compass heading |

Background and the options that were weighed:
[`.claude/docs/FutureUpgrades-WindAndRudder.md`](../../.claude/docs/FutureUpgrades-WindAndRudder.md).

## What is in this directory

```
cad/wind/
├── README.md                       this file
├── Assembly.md                     build instructions
├── 3D-Parts/                       STLs — print from here
├── Mechanics/
│   ├── IGES/                       the original CAD masters (German names)
│   ├── FreeCad/                    FreeCAD documents built from the IGES
│   ├── Fusion360/                  upstream Fusion sources
│   ├── BOM.txt                     upstream's original bill of materials
│   ├── build_wind_sensor.py        regenerates FreeCad/ and the modified STLs
│   └── check_rotation_clearance.py macro: does a rotating part clear a fixed one
```

The board lives in [`circuit/Wind/`](../../circuit/Wind/); build photos in
[`assets/wind/`](../../assets/wind/).

### Printed parts

German IGES name → English FreeCAD document → STL you print.

| STL | FreeCAD / IGES | Qty | What it is |
|---|---|---|---|
| `bot.stl` | `BottomHousing` ← `Unterteil` | 1 | **Modified.** Main housing: board pocket, bearing seat, arm socket |
| `top_1.stl` | `TopCover` ← `Oberteil` | 1 | **Modified.** Cover; its hub carries the vane bearing |
| `top_2.stl` | `TopCap` ← `Oberteil` | 1 | Top cap |
| `bot_ball_bearing.stl` | `BottomBearingHolder` ← `Unterteil-2` | 1 | **Modified.** Carries the two lower bearings; stem 5 mm longer |
| `magnetholder.stl` | `MagnetHolder` ← `Magnethalter` | 1 | Holds the four speed magnets |
| `base_cup_wheel.stl` | `CupWheelHub` ← `Loeffel_mitte` | 1 | Hub the three cups glue into |
| `cup_round.stl` | `CupRound` ← `Loeffel_rund` | **3** | The cups |
| `cup_pointed.stl` | `CupPointed` ← `Loeffel_spitz` | — | Alternative cup profile |
| `fane.stl` | `WindVane` ← `Windfahne` | 1 | The vane blade |
| `fane_support_big.stl` | — | 1 | 78 mm dome the vane mounts on |
| `fane_support_smal.stl` | — | 1 | Clamps the vane bearing into the cover hub |
| `base_power.stl` | `MastBase` ← `Fuss_regler` | 1 | Mast base the standpipe plugs into |

`magnet_holder_2.stl` and `cup_pointed_long.stl` are upstream variants, not used
in this build. `WindexBase` has no STL — it is an upstream alternative top.

### What was changed from upstream

The Nano ESP32 board is **103.25 × 40.13 mm** — longer than the 70 mm housing —
so `bot.stl` and `top_1.stl` grew a nose to cover it:

- Board pocket now follows the board outline with 0.4 mm clearance
- A **49 × 23 × 14.8 mm well** under the nose for the Nano, which hangs 14 mm
  below the board in its socket
- The **arm-tube socket moved 47.8 mm outboard along its own axis** to clear the
  Nano. Same 20° angle, same Ø10 bore — so the same tube and mast base still fit,
  **the tube just needs shortening by 47.8 mm**
- A fifth cover screw at the nose tip
- Cable duct and riser bring the 12 V cable to the terminal block

Everything within **r = 20 mm of the axis is untouched** — bearing seats, magnet
gaps, mounting bosses and screw positions are all exactly as the original, so
the rotating assembly is unaffected.

**`bot_ball_bearing.stl` — stem lengthened 36 → 41 mm.** The relocated arm
socket reaches about 2.5 mm deeper than the bare tube did, into the band the cup
wheel sweeps. Rather than thin the socket wall, the bearing carrier's stem was
stretched 5 mm, which drops the cup wheel clear with margin.

This works because the rotating assembly hangs from the **top** (625) bearing at
the flange end, and the speed-magnet holder sits up there under the hall sensor.
Stretching the stem moves only the lower (695) bearing and the cup wheel that
clamps against it — **the 1 mm magnet gap is untouched**, and so are the Ø31
spigot, the Ø35 flange, both bearing seats and the three Ø3.2 screw holes. The
inserted 5 mm is a prism of the carrier's own cross-section, so the taper simply
pauses rather than stepping.

Two consequences:

- **The shaft screw must change** — see the shopping list. Both bearings run on
  the *smooth* part of the shank, and the bearing span goes 30 → 35 mm, which
  the original M5×60's 35 mm plain shank no longer covers with any margin.
- **The cups now hang 5 mm lower.** Check clearance to anything under the
  masthead — anchor light, mast crane, backstay.

### Rebuilding the CAD

All three modified parts are generated from the IGES masters, not hand-edited.
To regenerate `FreeCad/*.FCStd` and the STLs:

```bash
/Applications/FreeCAD.app/Contents/Resources/bin/freecadcmd cad/wind/Mechanics/build_wind_sensor.py
```

It reads the board outline straight from the EasyEDA export, so if the PCB
changes shape the housing follows. The modified documents keep a live Part
boolean tree, so every feature stays editable in the FreeCAD GUI. The carrier's
extra length is the `STEM_EXTRA` constant at the top of the script — change it
and re-run if the cups need to drop further.

---

## Shopping list

Everything below is per sensor. Sizes are from
[`Mechanics/BOM.txt`](Mechanics/BOM.txt) and the build photos.

Almost everything is available from **Amazon**; **McMaster-Carr** is worth the
separate order for the shaft screw and for genuine A4 stainless, where the exact
spec matters. Links below are searches and category pages, not specific
listings — stock and sellers change, so check the spec against the Notes column
before you buy.

### Bearings, fasteners, magnets

All stainless — **A4 (V2A/316)**, not A2. This lives at the masthead.

| Qty | Item | Notes | Buy |
|---|---|---|---|
| 2 | Ball bearing **625** (16×5×5 mm) | Stainless. ABEC9 if you can get it | [Amazon](https://www.amazon.com/%EF%BC%BB12-Pack%EF%BC%BD625-2RS-Ball-Bearings-Miniature/dp/B0BRQP2QG7/ref=sr_1_1?dib=eyJ2IjoiMSJ9.4x0Wtiq5qQ_17NQ4QzVgRnq18-aLFRPu08qFq8vXf0iK1o7CrTeeH2gTfIYVrMECfK-ucRak0VXojilJzjPlj0QP1dmHYxOjnx0FdPpKyCm1cFlPnuECsvnxYDI_mews9Yr8reKcHB_IKgnnsYdjp4iciRY7FSnUkgvoD2Bt-NXyi2QMgDmfDryM5M7L6qdzrQMGOJ2nao8qstjJ7GVWia1VwZpzBzw0HxMtglFkB4s.nZfNSqzpm53_VU8evNhEOtbyATBElFZnZ7nOes0dJR0&dib_tag=se&keywords=625%2Bstainless%2Bball%2Bbearing%2B5x16x5&qid=1785684561&sr=8-1&th=1) |
| 1 | Ball bearing **695** (13×5×4 mm) | Stainless; full-ceramic is a nice upgrade here | [Amazon](https://www.amazon.com/uxcell-695-2RS-Miniature-Bearings-5x13x4mm/dp/B0CRVKG96C/ref=sr_1_13?crid=FMEH3V0FETX3&dib=eyJ2IjoiMSJ9.cqOikR3grDKrSpYp7B9CnzDcrTEYloKaRUfrPfb_6FL9qLV4_R_8f8jXJCEjoCpLNg_SSSYuEghskjiLhjEmRyJqtjkBTKdX3Nn-5VyCyyfarHi8NAimMARwlHh2es2b3dtSKYb3_9L4MJ6n3cyLu_njEF6GxYfy_h72jaWJyaS9AXAX0-mGRtFKYaxzAIyeKXlZkuks91dMi4bQDIBqwdtUGJ9jLNc0BEr2mni2YT4.Ieqxu_fr4wVvgVzp-q6eeeBDoKBc0UD7Iw5yR4Kcm6E&dib_tag=se&keywords=695%2Bceramic%2Bball%2Bbearing%2B5x13x4&qid=1785698512&sprefix=695%2Bceramic%2Bball%2Bbearing%2B5x13x4%2Caps%2C168&sr=8-13&th=1) |
| 4 | Neodymium bar magnet **5 × 1.5 × 1 mm** | Speed ring — **fit alternating**, see note | [buyneomagnets](https://www.buyneomagnets.com/p/5x1-5x1-mm-thick-neodymium-block-magnets-n35-powerful-small-rectangular-magnets-tiny-magnetic-blocks-lowes-home-depot/?srsltid=AfmBOoq7fTtj0ZF1OnwfmJ0Us6J_bl_mUZTujYgh4wVVySbhZA26m0kB) · [UMagnets](https://www.umagnets.com/p/neodymium-block-magnet-5-x-1-5-x-1mm-thick-strong-n35-strong-square-rare-earth-rectangular-magnets-100-pack/?srsltid=AfmBOoo9vX7XZ_gluh5jqX0zbZ_4ckCh6K82abPCF2J38M1UUHFYKRr6) |
| 1 | Neodymium magnet **10 × 5 mm N45 disc** | Direction — **must be diametric**, see note | [buyneomagnets](https://www.buyneomagnets.com/p/10mm-x-5mm-diametrically-magnetized-disc-magnet-neodymium-round-magnets-n35-rare-earth-radial-magnets-20-pack/) · [umagnets](https://www.umagnets.com/p/n42-10mm-x-5mm-strong-disc-neodymium-magnets-round-rare-earth-magnets-ebay/) |
| 1 | **M5×70 screw, DIN 931** (partially threaded) | **Changed from M5×60.** Cup-wheel shaft. Needs ≥40 mm of *plain* shank — both bearings run on it, span is now 35 mm. DIN 931 M5×70 gives 48 mm plain | [McMaster](https://www.mcmaster.com/92095A484/) |
| 1 | **M5×25** countersunk screw | Vane shaft | [McMaster](https://www.mcmaster.com/92010A330/) |
| 1 | **M6×60** Allen screw | Vane counterweight — mass matters more than grade | [McMaster](https://www.mcmaster.com/91263A468/) |
| 11 | **M3×10** Allen screws | Housing. 3×10 wood screws also work | [Amazon](https://www.amazon.com/Etauwe-100-Piece-10mm-Self-Tapping-Screws/dp/B0G7ZPTS7B/ref=sr_1_4?crid=32B61CL7BERWG&dib=eyJ2IjoiMSJ9.XanwaqPHyUHSN7eyTio83BJu_cdNxjJKwsKFrIgFIkdb47J0skBLdBH2CGQY_Nmae59m6LIllpV8XhvIZGuco6VYJs2H7G_7wIg6cpIdZ51TqTN1lh8Ca0eozUXOepCI83yG-S0n9LphDOzUZb5pcY8kgb3V3pIWETJen7BsuwxreOSHA3FnFS7RmtK5VrqOWFULxVwzTJsNJTsfCb-lBap5BY9OvVKDrMFPBCjczd0.BSbMmoANCpl9Fm55AVWAX_zHJoswU9GGE3LVcZcD45Y&dib_tag=se&keywords=m3%2B10mm%2Bstainless%2Bsteel%2Ballen%2Bhex%2Bsocket%2Bhead%2Bcap%2Bself-tapping%2Bscrews&nsdOptOutParam=true&qid=1785711152&sprefix=m3%2B10mm%2Bstainless%2Bsteel%2Ballen%2Bhex%2Bsocket%2Bhead%2Bcap%2Bself-tapping%2Bscrews%2Caps%2C189&sr=8-4&th=1) |
| 3 | **M5** washers | | [McMaster](https://www.mcmaster.com/93475A240/) |
| 1 | **M5** nut | | [McMaster](https://www.mcmaster.com/91828A241/) |
| 2 | **M5** stop nuts (nyloc) | | [McMaster](https://www.mcmaster.com/94645A102/) |
| 1 | **Ø10 × 1 mm wall × 300 mm** aluminium tube | Standpipe — **cut 47.8 mm shorter than upstream** | [Amazon - Alunium](https://www.amazon.com/VictorsHome-Aluminum-Thickness-Seamless-Straight/dp/B09VDKB1NG/ref=sr_1_2?dib=eyJ2IjoiMSJ9._UjsRnzu_Qjin47WzAOLjWHsra0nGoJMJHi6q0bh-DQ4GCUTvdT3Drna8TmuyJ61OiwGkogTrswNtXDKBIeVLuzrglxWadjWEEwMaLf0cztAHNv_KOe34NBJxujLbR_BwWRylLdrfAEw-hYLGjdp645_2iF648Sbu33R6RSVakY5XaeirOjjuUtmSmKyCLsRjtTkeMkDdx1-yKFGNrQUsXNsU4UTYrInrn1bS_8sUN8.FTjPqplI53nOL3eTThdrytpTb1Pq8KRu3__CnRno3u4&dib_tag=se&keywords=10mm%2Bod%2B1mm%2Bwall%2Baluminum%2Btube&qid=1785724307&sr=8-2&th=1) · [Amamzon - Stainless](https://www.amazon.com/MECCANIXITY-Stainless-Capillary-Thickness-Construction/dp/B0G52G6S5T/ref=sr_1_1_sspa?dib=eyJ2IjoiMSJ9._UjsRnzu_Qjin47WzAOLjWHsra0nGoJMJHi6q0bh-DQ4GCUTvdT3Drna8TmuyJ61OiwGkogTrswNtXDKBIeVLuzrglxWadjWEEwMaLf0cztAHNv_KOe34NBJxujLbR_BwWRylLdrfAEw-hYLGjdp645_2iF648Sbu33R6RSVakY5XaeirOjjuUtmSmKyCLsRjtTkeMkDdx1-yKFGNrQUsXNsU4UTYrInrn1bS_8sUN8.FTjPqplI53nOL3eTThdrytpTb1Pq8KRu3__CnRno3u4&dib_tag=se&keywords=10mm%2Bod%2B1mm%2Bwall%2Baluminum%2Btube&qid=1785724307&sr=8-1-spons&sp_csd=d2lkZ2V0TmFtZT1zcF9hdGY&th=1) |

> **The M5×70 is the one item worth reading twice.** It replaces the M5×60 in the
> upstream list because the carrier stem is 5 mm longer. Both bearings have a
> 5 mm bore and must ride on the **unthreaded** part of the shank — a bearing
> sitting on thread run-out will be sloppy and will chew the bore. Fully
> threaded M5×70 is the wrong part. You want DIN 931 (partially threaded, 22 mm
> of thread, 48 mm plain). Head must be a cross-recess or hex that still drops
> into the magnet-holder recess.

> **The two magnets are the parts most often bought wrong.**
>
> **Direction magnet — must be *diametrically* magnetised** (poles across the
> diameter, not top to bottom). An axial magnet of identical size will not work
> with an AS5600. Sources disagree on size: the build photo is annotated
> **10 × 5 mm N45 disc**, while `BOM.txt` and the upstream page say **5 × 5 × 5 mm
> square**. Either works if it is diametric; the 10×5 disc is easier to align
> because you can see the pole boundary on the rim.
>
> **Speed magnets — four small bars, fitted alternating N-S-N-S.** All four the
> same way round makes every wind speed read exactly **double**. `BOM.txt` and the
> photos say 5 × 1.5 × 1 mm; the upstream web page says Ø3 × 4 mm, which is a
> different variant.

### Consumables

| Item | Notes | Buy |
|---|---|---|
| PETG or ASA filament, **white or light** | Dark filament softens in masthead sun; black PETG is unsuitable | [Amazon](https://www.amazon.com/s?k=white+PETG+filament+1.75mm) |
| Clear lacquer that bonds to PETG without primer | Upstream specifies Dupli-Color Aerosol Art — 3 thin coats | [Amazon](https://www.amazon.com/s?k=clear+coat+spray+lacquer+UV+resistant+plastic) |
| 2-component acrylic adhesive, retains some elasticity | Upstream specifies **Weicon RK-1300**. Rigid glues crack over the −10 … +90 °C swing | [Amazon](https://www.amazon.com/s?k=Weicon+RK-1300+structural+acrylic+adhesive) |
| Superglue | Captive nuts and the small magnets only | [Amazon](https://www.amazon.com/s?k=cyanoacrylate+super+glue+gel) |
| Threadlocker (blue) | Cup-wheel shaft, after setting the running clearance | [Amazon](https://www.amazon.com/s?k=loctite+242+243+blue+threadlocker) |
| 99 % isopropyl or ethanol + cotton buds | Degrease before lacquering | [Amazon](https://www.amazon.com/s?k=99%25+isopropyl+alcohol) |
| Silicone or fine machine oil | Bearings | [Amazon](https://www.amazon.com/s?k=fine+machine+oil+precision+bearing) |
| 2-core cable, masthead to 12 V | ~50 mA, so conductor size is not critical; UV-resistant jacket is | [Amazon](https://www.amazon.com/s?k=marine+2+conductor+tinned+wire+18awg) |

### Electronics you will need to buy

**You are fabricating your own board**, so this is the component list, not a
finished module. Board files, schematic and Gerbers are in
[`circuit/Wind/`](../../circuit/Wind/).

| Ref | Part | Role | Buy |
|---|---|---|---|
| U1 | **Arduino Nano ESP32** | Wi-Fi + firmware. Socketed on the **underside** | [Amazon](https://www.amazon.com/s?k=Arduino+Nano+ESP32) |
| DIRECTION | **AS5600L-ASOT** (SOIC-8) | Magnetic angle encoder — wind direction | [Amazon](https://www.amazon.com/s?k=AS5600+magnetic+encoder) · [LCSC C499458](https://www.lcsc.com/search?q=AS5600L-ASOT) |
| SPEED | **SS40AF** hall sensor (TO-92) | Wind speed pulses. A3144 is a common substitute | [Amazon](https://www.amazon.com/s?k=A3144+hall+effect+sensor+TO-92) |
| TEMP | **DS18B20** (TO-92) | Air temperature (optional) | [Amazon](https://www.amazon.com/s?k=DS18B20+TO-92+temperature+sensor) |
| R1, R2, R4 | 4.7 kΩ resistor | I²C pull-ups and 1-Wire pull-up | [Amazon](https://www.amazon.com/s?k=resistor+assortment+kit+1%25) |
| R3 | 10 kΩ resistor | Hall sensor pull-up | (same kit) |
| C1 | 100 nF ceramic capacitor | Decoupling | [Amazon](https://www.amazon.com/s?k=100nF+ceramic+capacitor+kit) |
| J2 | 2-way 3.5 mm screw terminal | 12 V in | [Amazon](https://www.amazon.com/s?k=3.5mm+pitch+2+pin+screw+terminal+block+pcb) |

Also a **15-pin socket header pair** (2 × 15, 2.54 mm) for the Nano — it mounts
in a socket so it can be pulled without unsoldering, which is why the housing
has a 14 mm well beneath it —
[Amazon](https://www.amazon.com/s?k=15+pin+female+header+2.54mm).
And the board itself: [PCBWay](https://www.pcbway.com/) or
[JLCPCB](https://jlcpcb.com/) will take the Gerbers in
[`circuit/Wind/`](../../circuit/Wind/) directly.

> The upstream project sells a populated ESP8266 board through Aisler. That board
> is **not** compatible with this housing — the pocket, the well and the
> connector positions are all cut for the Nano ESP32 board in `circuit/Wind/`.

---

## Credits

Original design by **Norbert Walter**, [Open Boat Projects](https://open-boat-projects.org/).
Hardware and documentation **CC-BY-NC-SA**; firmware **GPL-3.0**.
Upstream: <https://github.com/norbert-walter/Windsensor_Yachta>
