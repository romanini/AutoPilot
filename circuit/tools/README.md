# EasyEDA export automation

Replaces the manual `File -> Export` pass over all seven boards. One command
exports every format for every project, with consistent filenames, straight
into the right `circuit/<Unit>/<Board>/` folder.

## Setup (once)

```bash
cd circuit/tools
npm install
npx playwright install chromium
npm run login          # sign in to EasyEDA in the window that opens
```

The login is saved to `.eda-profile/` (gitignored), so it only has to be done
once — not per run.

## Wiring up the projects (once)

```bash
npm run discover
```

The boards are not in the personal workspace — they live in a folder belonging
to the `Marco Scott` team. `discover` goes straight there via `FOLDER_URL` at
the top of `eda.mjs`:

```
https://u.easyeda.com/account/user/projects/team?team=<team-uuid>&folder=<folder-uuid>
```

Note the `u.easyeda.com` host, which is not the same as the `easyeda.com` the
editor runs on. To point this at a different folder, open it in EasyEDA and
copy the URL.

Blanking out `FOLDER_URL` falls back to finding the folder by name in the
sidebar tree (`Participated > Marco Scott > AutoPilot`, the `TREE_PATH`
constant). That path still works but is much more fragile, since the tree
loads each level over the network, labels carry non-breaking spaces and count
badges, and the expand caret is a toggle that will happily close a node that
is already open. `openTreePath` handles all three; the direct URL avoids
needing to.

Landing on the folder page loads its contents, and `discover` records the JSON
the site fetches for itself rather than guessing endpoint names. If that
listing already includes each project's documents, it is used directly;
otherwise each project is opened so the site fetches its documents.

Two things this deliberately avoids:

- **Reading links from the page at large.** The dashboard has a recents panel
  whose links are unrelated to the folder; scraping `a[href]` picked those up
  instead of the folder's real contents.
- **Pairing schematics to PCBs by document title.** A board's PCB document is
  often titled something generic like `PCB_1`, so title matching mispairs.
  Documents are attributed to the project they belong to instead.
- **Identifying a project by its name.** One project's name is a prefix of
  another's (`AP - Controller` / `AP - Controller - 12 volt power`), so
  projects are located by uuid, falling back to the title only if the markup
  carries no uuid. A project that still comes back with no documents is
  retried the other way before it is believed; `perProject[].foundBy` in the
  dump records which worked.

It writes two files:

- `discover-dump.json` — every API response seen, the endpoints hit, how far
  the tree walk got, and every label that was visible in the tree. This is the
  file to look at when something doesn't line up.
- `projects.draft.json` — a ready-made skeleton, one entry per board, with the
  schematic and PCB editor URLs already paired up by title.

Check it over, then rename the draft to `projects.json`. Each entry carries the
editor URL and the document title for the schematic and the PCB. The titles
matter: EasyEDA keeps the **same URL hash** for a project's schematic and its
PCB, so the address bar cannot be trusted to say which document is loaded.
Before exporting, the tool reads the `docType` out of the document itself and,
if the wrong one is showing, clicks the right one by title. If it still can't
get there it skips that document rather than exporting the wrong board's files
under the right board's name.

There is nothing to fill in by hand: `board` comes from the project's row
(`"AP - Sensor - Wind"` → `AP-Sensor-Wind`) and doubles as the output
directory, giving:

```
circuit/AP-Sensor-Wind/            circuit/AP-Display-Button/
circuit/AP-Sensor-Rudder/          circuit/AP-Display-LCD/
circuit/AP-Controller/             circuit/AP-Display/
circuit/AP-Controller-12-volt-power/
```

Note these follow the EasyEDA project names, which differ from the old
hand-made folder names (`Wind/`, `Display/Button/`, …).

`projects.json` itself is committed, so it doubles as the worked example of
the format.

## Every time after that

```bash
npm run export                          # all seven boards
npm run export -- --only Sensor-Wind    # just one
npm run export -- --dry-run             # show what would be written, touch nothing
```

`--only` matches the `board` name, case-insensitively. An **exact** name wins,
so `--only Display` is the Display board alone, not also `Display-LCD` and
`Display-Button`. Otherwise it matches any board containing the text, which is
how you pick a group (`--only Sensor` → `Sensor-Rudder` and `Sensor-Wind`) or
just type less (`--only wind`). When more than one board matches, the run says
which before starting.

Combine it with `--dry-run` to see the selection without opening a browser.

## After renaming a project in EasyEDA

Board names come from the EasyEDA project name, so a rename flows through to
the directory and every filename. Two commands:

```bash
npm run discover && cp projects.draft.json projects.json
npm run export
```

The document uuids don't change on rename, so the editor URLs stay valid —
`discover` is re-run to pick up the new names, not new links. `schTitle` /
`pcbTitle` are *document* titles, which a project rename doesn't touch.

The old directories are left behind, since nothing here deletes your files:

```bash
npm run stale
```

That lists board folders under `circuit/` that no longer match
`projects.json`, and prints the `rm -rf` lines to run once the new exports
look right. It only counts folders holding files this tool produced, so
hand-made folders are never proposed for deletion.

## Layout and naming

One directory per board, all at the same level under `circuit/`, named after
the board. Filenames are:

```
<SCH|PCB>_<board>_<type>.<ext>
```

where `<board>` is the EasyEDA document title with spaces replaced by hyphens.
Every file carries a type segment, including the two where it restates the
extension (`_PNG.png`, `_DXF.dxf`), so the pattern has no exceptions.

```
circuit/AP-Wind-Sensor/
  SCH_AP-Wind-Sensor_EasyEDA.json
  SCH_AP-Wind-Sensor_SVG.svg
  SCH_AP-Wind-Sensor_PNG.png
  SCH_AP-Wind-Sensor_Altium.schdoc
  PCB_AP-Wind-Sensor_EasyEDA.json
  PCB_AP-Wind-Sensor_SVG.svg
  PCB_AP-Wind-Sensor_PNG.png
  PCB_AP-Wind-Sensor_Gerber.zip
  PCB_AP-Wind-Sensor_Autorouter.dsn
  PCB_AP-Wind-Sensor_DXF.dxf
  PCB_AP-Wind-Sensor_OBJ.zip
  PCB_AP-Wind-Sensor_PhotoView-Top.svg
  PCB_AP-Wind-Sensor_PhotoView-Bottom.svg
```

`npm run export -- --dry-run` prints exactly this list without opening a
browser, so the naming can be checked before any real run.

## What gets produced per board

13 files: `EasyEDA.json`, `SVG.svg` and `PNG.png` for both the schematic and
the PCB, plus `Altium.schdoc` for the schematic and `Gerber.zip`,
`Autorouter.dsn`, `DXF.dxf`, `OBJ.zip`, `PhotoView-Top.svg` and
`PhotoView-Bottom.svg` for the PCB. See the command table below.

Downloads are intercepted by the browser automation and written directly to
their final path, so nothing lands in `~/Downloads` and nothing needs renaming.

## How it works

Every export is fired through **`callCommand()`**, the editor's own command
dispatcher, using the command ids EasyEDA puts on its menu elements:

| Export | Command | Dialog |
|--------|---------|--------|
| EasyEDA JSON | `export_easyEDA` | — |
| SVG | `source(SVG)` | `Download` |
| PNG | `export(fileImage)` | `Export` |
| Altium | `export(altium)` | tick the disclaimer, then `Download` |
| Gerber | `pcb_fabrication` | `Yes, Generate Gerber` → `No, Generate Gerber` (skip DRC) → `Generate Gerber` |
| Autorouter DSN | `exportFile(dsn)` | — |
| DXF | `exportDxf` | — |
| OBJ | `export3DModelObj` | — |
| PhotoView | `convertToPhotoView` once, then `photoView_changeSide_topSide` / `…_bottomSide` + `source(SVG)` | `Download` |

Note it is **SVG Source** (`source(SVG)`), not the `SVG...` menu item.
PhotoView is entered once and both sides exported from there — firing
`convertToPhotoView` again would toggle straight back out to the PCB.

Dialog buttons carry leading icons (`√ Export`, `✗ No, Generate Gerber`), so
those glyphs are stripped before matching, and an exact match is preferred
over a substring one — otherwise `Generate Gerber` would match
`Yes, Generate Gerber`. Disabled buttons are ignored, which is what makes the
Altium flow work: the checkbox has to be ticked before `Download` is live.

The [documented extension API](https://docs.easyeda.com/en/API/3-API-List/index.html)
(`api('getSource', …)`) is deliberately **not** used. That `api()` handle
exists only inside an extension's own context — it is not on the page, so it
cannot be called from outside. `callCommand` can.

`npm run inspect` dumps the live command list to `menu-dump.json`; the
`withCmd` array is where the ids above came from, and where to look if one
stops working.

A failed export warns and moves on rather than aborting the run, and the run
ends with a per-board ok/failed summary.

## Note on the previous layout

Exports used to live in nested folders (`circuit/Controller/Controller/`,
`circuit/Display/Button/`, …) with hand-typed filenames that had drifted:
`SCH_AP-Wind-Sensoraltium.schdoc`, `PCB_AP-Display-2.1_Display-2.1_Gerber.zip`
(name doubled), `SCH_12-volt-power-EasyEDA.json` (hyphen instead of
underscore). The first full run writes the flat layout above; the old nested
directories are left untouched and can be deleted once the new exports are
confirmed good.
