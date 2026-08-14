# DS45PAD

A PlayStation 3 plugin that makes **DualShock 4** and **DualSense** controllers
work in games that demand an official DualShock 3, and fixes their analog stick
response to match a real DS3.

Works on **PS3HEN** and **CFW**. No hardware adapter required.

---

## The problem

The PS3 already accepts a DS4 or DualSense, but treats it as a generic HID
gamepad. Two things break as a result.

### Games that require a DualShock 3 reject it

Those games call `cellPadGetInfo2()` and check `device_type` and the capability
bits. A generic pad reports no PS3 conformity at all.

The obvious fix — register a virtual controller through libpad's LDD interface —
**cannot work.** An LDD pad always reports `CELL_PAD_DEV_TYPE_LDD` (5), never
`STANDARD` (0). Measured on hardware: requesting capability `0x1F` gets you
`0x07` granted, and the device type stays `5`, immutably. That applies to any
plugin taking this approach, including PS3xPAD.

Hardware adapters (Brook, CronusMax, Titan) get past the check by *being* a
DualShock 3 to the console — vendor id `054C:0268`, the DS3's HID descriptor and
report format, adopted by the firmware's own driver. No plugin can do that; you
cannot change what a USB device says about itself.

### The sticks feel wrong at the edges

Not a mapping bug — a response curve difference. A DualShock 3's ADC **saturates
at roughly 80% of mechanical travel**: pushing an axis fully gives `0x00`/`0xFF`
with travel to spare, and a full diagonal reads about `0x0F`. The DS4 and
DualSense are linear all the way to the stop, so the same diagonal reads `0x26`.
That is a long way from what a PS3 game was tuned against, which is why so many
of them show not enough turn, no full sprint, and diagonals that never register
as fully committed.

Hall effect sticks make it worse: linear *and* short-ranged, many only swinging
about `0x14`–`0xEB`, so they fall short even on a single axis.

---

## How this fixes both

Instead of building a virtual controller and hoping the game talks to it, the
plugin is injected **into the game's own process** and answers the questions the
game asks:

- **`cellPadGetInfo2`** is intercepted and reports `device_type = 0` with full
  conformity. The game accepts whatever pad is plugged in.
- **`cellPadGetData`** is intercepted and the stick values are corrected in
  flight, reproducing the DS3 saturation curve.

Because it never touches device identity, it works over **any** controller the
game already receives — DualSense, DS4, an adapter, even a real DualShock 3
whose sticks you want re-curved.

---

## What works

| Feature | Status |
|---|---|
| Games requiring an official DualShock 3 | ✅ |
| DS3-matched analog stick curve | ✅ |
| Automatic per-stick range calibration | ✅ |
| PS button in the XMB | ✅ with the optional VSH module |
| PS button **in-game** | ❌ handled by the VSH, not the game — out of reach |
| Rumble, sixaxis, touchpad | ❌ not implemented on this path |

---

## Requirements

- PS3 with **PS3HEN** or **CFW**
- **webMAN MOD** — a `ps3mapi` variant. This is what loads a module into a
  running game process, and it is mandatory.
- A DualShock 4 or DualSense, connected however you normally would (USB cable
  or the console's own Bluetooth pairing)

---

## Installation

### 1. Install webMAN MOD

Grab a release from
[aldostools/webMAN-MOD](https://github.com/aldostools/webman-mod/releases) and
pick the variant matching your setup:

| Setup | Variant |
|---|---|
| PS3HEN | `webftp_server_ps3mapi.sprx` |
| CFW with Cobra | `webftp_server_cobra_ps3mapi.sprx` |
| Rebug CFW | `webftp_server_rebug_cobra_ps3mapi.sprx` |
| CFW without Cobra | `webftp_server_ps3mapi.sprx` |

It must be a **`ps3mapi`** build. Copy it to `/dev_hdd0/plugins/`.

### 2. Set up the safety net first

A bad plugin path in `boot_plugins.txt` can leave the console hanging at boot.
This takes a minute and saves you an evening:

Create an **empty** file named `boot_plugins.txt` in the **root** of a FAT32 USB
stick. The USB copy takes priority over the one on the HDD, so booting with it
inserted loads no plugins at all.

Fallback with no USB stick: Recovery Menu → **Rebuild Database**, which deletes
`/dev_hdd0/boot_plugins.txt`.

### 3. Copy the plugin files

```
/dev_hdd0/plugins/ds45pad/ds45pad_game.sprx    <- does the actual work
/dev_hdd0/plugins/ds45pad/ds45pad.txt          <- configuration
/dev_hdd0/plugins/ds45pad/ds45pad_vsh.sprx     <- optional, PS button in XMB
```

Transfer over FTP (webMAN's server, port 21, blank credentials) or with a USB
stick and a file manager.

> Windows Explorer (`ftp://<ps3-ip>`) also works and needs no setup.

### 4. Register the boot plugins

Add to `/dev_hdd0/boot_plugins.txt` — one path per line, in the **root** of
`dev_hdd0`, not inside `plugins/`:

```
/dev_hdd0/plugins/webftp_server_ps3mapi.sprx
/dev_hdd0/plugins/ds45pad/ds45pad_vsh.sprx
```

**The second line is optional and only provides the PS button in the XMB. If you do not care about that, leave it out — fewer moving parts.**

On MAMBA setups the file is called `mamba_plugins.txt`.

### 5. Reboot

On CFW that is all. On HEN, enable HEN as you normally do.

### 6. Load the module into your game

1. Start the game and let it load fully
2. Open `http://<your-ps3-ip>` in a browser
3. Go to the **PS3MAPI** tab → **Game Plugins**
4. Select the game's process (it has `EBOOT` in the name) → **Set**
5. In the first free slot, enter:

```
/dev_hdd0/plugins/ds45pad/ds45pad_game.sprx
```

6. Click **Load**

Verify by reading `/dev_hdd0/tmp/ds45pad_boot.txt`:

```
game_mode
hook_cellPadGetData_ok
hook_cellPadGetDataExtra_ok
hook_cellPadGetInfo2_ok
```

If the game had already decided the controller was unsupported before the module
loaded, unplug and replug the controller, or open the game's controller menu, to
force it to re-check.

### 7. Calibrate the sticks

Rotate both sticks to their stops a few times. This teaches the plugin each
axis's real range and is what makes a short-travel Hall effect module reach full
scale. It is saved to `/dev_hdd0/plugins/ds45pad/ds45pad_cal.txt` — the same folder as the SPRX files — and does not need repeating.

---

## Configuration

`ds45pad.txt`, plain `KEY = VALUE` lines. The ones that matter:

| Key | Default | Purpose |
|---|---|---|
| `STICK_SATURATION` | `80` | The response curve. `80` = DualShock 3. Lower to `75`/`71` for more reach in the corners, raise to `85` if diagonals feel sticky. `100` disables correction |
| `STICK_DEADZONE` | `4` | Centre deadzone, percent of travel. Hall effect sticks have no drift — try `0` |
| `STICK_CALIBRATION` | `AUTO` | Learn each axis's real range at runtime |
| `SPOOF_PAD_INFO` | `1` | Report as a conformant DualShock 3. Turn off if a game misbehaves |
| `INSERT_INTO_GAME` | `0` | **Leave at 0.** With it on, no app started afterwards finishes loading |

`71` deserves a note: that is where a full diagonal reaches the absolute corner.
A real DS3 does not go that far — it stops around `0x0F`. Use `71` if a specific
game only reacts at the extreme, but `80` is what reproduces the original
controller.

---

## Building

Requires the **Cell SDK PPU toolchain** (`ppu-lv2-gcc` with `-mprx`) and
`scetool`. There is no open-source alternative: a VSH/game plugin is a PRX
module with a `.sceModuleInfo` section, and emitting that needs SCE's compiler.
`ps3toolchain`/PSL1GHT has every API this plugin uses but cannot produce the
format.

```bash
cd src
./build.sh          # ds45pad_vsh.sprx
./build.sh game     # ds45pad_game.sprx
```

Signing matters and differs per target:

| Target | Auth ID | Key revision |
|---|---|---|
| VSH process | `1070000052000001` | `0A` (3.55) |
| Game process | `1010000001000003` | `0A` (3.55) |

Key revision `0A` is **mandatory on PS3HEN** — anything older hangs the console
at boot. The auth ID must match where the module runs, or the loader cannot
place it.

The in-game build compiles only 7 files and imports 2 libraries on purpose: a
game process loads `libio` and `libfs` but **not** `libusbd`, and a single
reference to the latter leaves the loader unable to resolve the module, hanging
PS3MAPI on "Load" indefinitely.

---

## How the hook works

On PowerPC64 ELFv1 a function pointer *is* the address of a two-word descriptor
`{entry point, TOC}`. Every module importing a given function points at the same
descriptor in the exporting library, so overwriting those two words redirects
the entire process:

```c
uint32_t *opd = (uint32_t *)cellPadGetData;
saved[0] = opd[0];  saved[1] = opd[1];
opd[0] = hook[0];   opd[1] = hook[1];
```

No table walking, no NIDs, no dependency on process layout. Confirmed by
measurement: the descriptor sits ~1.6 MB outside the injected module, i.e. it is
the shared one inside `sys_io`.

For contrast, the technique published VSH plugins use — walking from
`*(uint32_t *)0x1008C + 0x984` — only resolves inside the VSH process. In a game
process `0x10000` is the EBOOT's ELF and there is no table there.

---

## Findings

Things measured on hardware that were not documented anywhere, and that may save
someone else the effort:

- **An LDD virtual pad always reports `device_type = 5`.** Immutable. No
  virtual-pad plugin can satisfy a game that gates on device type.
- **Syscall 573 (`INSERT_DATA_INTO_GAME_MODE_ON`) stops apps from loading.**
  With it on, no app started afterwards — multiMAN included — finishes coming
  up. The virtual pad itself is harmless; the exposure to games is what hangs.
  This is why PS3xPAD's README tells you to press `START+SELECT+R3` *after* the
  game boots.
- **`CELL_PAD_CTRL_LDD_PS` works, but only in the XMB.** The in-game PS button
  is handled by the VSH, so no hook inside a game process can reach it.
- **The firmware does not release a DualSense over USB.**
  `cellUsbdRegisterExtraLdd` never gets the device — the attach callback never
  fires. The pad stays on port 0 as a generic device, `device_type = 1`,
  capability `0x00`. PS3xPAD hits the same wall ("0 USB controllers detected").
- **A game process does not load `libusbd`.**

More detail in [docs/PESQUISA.md](docs/PESQUISA.md) (Portuguese).

---

## Repository layout

```
src/          plugin sources + build script
data/         default configuration files
release/      built, signed .sprx ready to install
```

---

## Credits

- **[PS3xPAD](https://github.com/aldostools/PS3xPAD)** by OsirisX — the libpad
  LDD approach, syscalls 573/574, and `CELL_PAD_CTRL_LDD_PS` as the carrier for
  the PS button. Its limits also mapped out where this project had to go
  instead.
- **[Linux `hid-playstation`](https://github.com/torvalds/linux/blob/master/drivers/hid/hid-playstation.c)**
  — the best public reference for DS4 and DualSense HID report layouts, report
  IDs, Bluetooth CRC32 seeding and sensor resolutions.
- **[RPCS3](https://github.com/RPCS3/rpcs3)** — exact numeric values for the
  `cellPad` constants.
- **[webMAN MOD](https://github.com/aldostools/webMAN-MOD)** by aldostools —
  PS3MAPI, without which loading a module into a game process would not be
  possible.
