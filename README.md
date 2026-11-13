# Snorca MultiAce

**Mnemonic3D Edition.** A slicer for the Snapmaker U1 with the multiACE add-on. It is built on the stock slicer, with the changes below.

> **Status: test release.** Linux and Windows builds are on the [Releases page](https://github.com/Mnemonic3D/Snorca-MultiAce/releases). [Watch the 2½-minute tour](https://youtu.be/1fYa3H8Mst8).

## What's different from the stock slicer

| | Stock slicer | Snorca MultiAce |
|---|---|---|
| **Filament list** | You set it up by hand, and filaments from your other printers get mixed in. | Pick your printer and the list fills itself in: exactly what is loaded on that printer, in the printer's own order. |
| **Switching printers** | Old rows stay behind and pile up. | The list changes with the printer. |
| **Getting the list back** | Rebuild it by hand. | One click on the sync button. Painted objects move to the same material in the closest colour. |
| **Printing ACE colours** | The print window only knows four heads, so two colours can land on the same slot. | Press Print. It spots an ACE job, sends it to multiACE and starts the print. No extra button. |
| **Picking spools** | You match them yourself on the printer. | Material must match exactly; colour picks the closest and tells you when it is not exact. |
| **Bed and flow calibration** | The ACE path had no way to ask. | Press Print and pick: probe the print area only, the full bed, or nothing, and whether to calibrate flow. It remembers your choice. *(new in snorca.2)* |
| **After you press Print** | No clear answer. | A window says "Printing on your printer" with each colour, its name and its ACE slot. |
| **When a job can't start** | An unclear error. | A plain reason, what the job needs next to what is loaded, and a button that opens the multiACE page. |
| **Saving settings** | Some line-width and speed boxes do not save. | Every one of those boxes saves into your own profile and survives a restart. |
| **Sending a job** | Could crash the slicer now and then. | Fixed. |

## New in snorca.2: pick bed and flow calibration when you press Print

![The print options window: bed calibration (print area only, full bed, none) and flow calibration](docs/images/snorca-print-options.png)

When you press Print on an ACE job, this window opens before anything is sent to the printer.

- **Bed calibration:** print area only (quicker), full bed, or none.
- **Flow calibration:** on or off, for each head the print uses.
- It remembers what you picked. Cancel sends nothing.

**This fixes a fault in snorca.1:** there, every ACE job started from Print ran with bed calibration and flow calibration switched off, with no way to turn them on. If you use snorca.1 for ACE prints, update.

Tested so far against a simulated printer and by running the printer's own code on the files the slicer sends. Not yet confirmed on a real print; if you try it, please [tell us what happened](https://github.com/Mnemonic3D/Snorca-MultiAce/issues). If your printer's firmware has "Force Bed Mesh" switched on, the printer still probes when you pick "none".

## Watch the tour

[![Snorca MultiAce: what's new - watch on YouTube](https://img.youtube.com/vi/1fYa3H8Mst8/hqdefault.jpg)](https://youtu.be/1fYa3H8Mst8)

A 2½-minute click-through of the new features: **[watch on YouTube](https://youtu.be/1fYa3H8Mst8)**. It was recorded on a test setup with a simulated printer; where a step is a stand-in, it says so on screen.

## What you need

- **Printer:** Snapmaker U1.
- **Firmware:** PAXX extended firmware **2.0.0, build 24**.
- **multiACE add-on:** version **1.27b**.

**Where to get them**

- multiACE add-on (downloads and install steps): **[github.com/decay71/multiACE](https://github.com/decay71/multiACE)** — see its [Releases](https://github.com/decay71/multiACE/releases) for version 1.27b.
- PAXX extended firmware for the Snapmaker U1: **[github.com/paxx12-snapmaker-u1/SnapmakerU1-Extended-Firmware](https://github.com/paxx12-snapmaker-u1/SnapmakerU1-Extended-Firmware)**.

Those projects are made by other people; follow their own instructions for installing on your printer.

That is the combination it was built and tested on. Older versions have not been tested. Without multiACE the slicer still works and prints the normal way; only the automatic ACE printing needs it.

## Downloads

Get them from the **[Releases page](https://github.com/Mnemonic3D/Snorca-MultiAce/releases)**.

| System | Status |
|---|---|
| Linux (64-bit) | **Available** (snorca.2): the `...linux-x86_64.tar.xz` file. Unpack it and run `AppRun`. |
| Windows (64-bit) | **Available, new and lightly tested**: the Windows installer `.exe` or the portable `.zip`. Read the Windows warnings below first. The snorca.2 Windows files have the print options window but have not been run yet; the Windows testing was done on snorca.1. |
| Mac | Not planned for now |

## Optional: "ACE" on the printer's touch screen

A separate small patch for the printer. When an ACE is connected to a toolhead and assigned through multiACE, the touch screen shows **ACE** under that toolhead's icon, on the home screen and on the filament screen. Nobody has to pick anything; the printer already knows.

- It is separate from the slicer. The slicer works without it.
- It is made for one firmware version (2.0.0, build 24) and switches itself off on any other.
- Status: **experimental**. Built on a test bench and tried on two real printers: loaded the temporary way on one, installed permanently on the other. After a permanent install you need to reboot the printer and enter your Wi-Fi again on its touch screen. It is on the [Releases page](https://github.com/Mnemonic3D/Snorca-MultiAce/releases) with its own instructions.

## Known limits

- **More than one ACE at once:** the slicer is written to handle it, but it has only been tested with one ACE so far.
- **Windows:** new. It has only been tried in a Windows 11 test machine against a simulated printer, not on a real Windows PC or a real printer. The installer is unsigned, so Windows shows an "unknown publisher" warning. A few places still show the old name (FOrcaSlicer), and it shares the FOrcaSlicer settings folder if you have that installed too. The portable zip has not been run at all. Full list on the [Releases page](https://github.com/Mnemonic3D/Snorca-MultiAce/releases).
- **Mac:** not planned for now.
- **Other languages:** some screens still show the old name.

## Found a problem?

Please [open an issue](https://github.com/Mnemonic3D/Snorca-MultiAce/issues). Say which download you used, your printer firmware and multiACE version, what you clicked and what happened. A screenshot helps.

## Credits

Built on the work of the OrcaSlicer and FOrcaSlicer projects, the multiACE add-on, and the PAXX extended firmware for the Snapmaker U1. Thank you to their makers.

## Licence

Open source, under the same licence as the slicer it is built on.

## Source and building

This repository holds the full source: upstream FOrcaSlicer v2.3.2-fos.8.6.8 (commit `0b1f80cd22793785d8ae5c4296d2fe4a9248bd21` of [jiyang1018/FOrcaSlicer](https://github.com/jiyang1018/FOrcaSlicer)) with the Snorca MultiAce changes on top.
The upstream build instructions are kept in [README-FOrcaSlicer-upstream.md](README-FOrcaSlicer-upstream.md).
The Windows and Linux builds run on GitHub's machines from `.github/workflows/` and start only when someone presses "Run workflow".
