# MM3 Setup (the installer)

Players: you do not need this folder. Download `MM3-Setup.exe` from the
[latest release](https://github.com/lakshya16157/midtown-madness-3-PC-Native/releases/latest)
and follow the steps at the top of the [main README](../README.md).

This folder holds the source for that installer, for developers.

## What it does

`MM3-Setup.exe` is one self-contained Windows program (Python + tkinter, packed with
PyInstaller). It needs no Python, .NET, Visual Studio or CMake on the target PC. It asks for
your image, destination and optional content, then:

1. checks the chosen disc image with the same code-hash gate as `scripts/extract_disc.py`
   (the Europe/Australia and USA releases share the same code) and extracts it to
   `game_files\`, applying the standard XISO media patch;
2. copies the bundled `mm3_recomp.exe` and its app-local Visual C++ runtime next to it;
3. **Add extra cars** (default off): runs `tools/car-mods/prepare.py`'s own plan/apply
   functions with the bundled car packages, or restores the original archives if the cars were
   installed before and are now unticked. The window names each car's author, and
   `Extra cars - credits.txt` is written next to the game with every author, the original
   model sources, the download pages and the authors' readmes unchanged (data lives in
   `CAR_CREDITS` in `installer.py`; keep it in step with the table in the main README);
4. **Add game improvements** (default on): writes `Play Midtown Madness 3.cmd` with the
   matching settings. Most fixes are compiled into the game and always on. The tickbox only
   chooses between the game's Balanced defaults (2x render scale, 4x MSAA, 16x anisotropic
   filtering, draw/view distance 2, adaptive aspect, night world light 25%) and variables that
   pin the original look (`RECOMP_RENDER_SCALE=1.0`, `RECOMP_MSAA=1`, `RECOMP_ANISO=1`,
   `RECOMP_DRAW_DISTANCE=1`, `RECOMP_VIEW_DISTANCE=1`, `RECOMP_ASPECT=4:3`,
   `MM3_NIGHT_WORLD_LIGHT=100`, `MM3_PARTICLE_MAX_OVERDRAW=12`, `RECOMP_SHARP_POINT=0`).
   Both modes also set the variables `scripts/play.ps1` sets (`RECOMP_PB_EXEC`, `RECOMP_USB`,
   `RECOMP_FB_WINDOW`, `RECOMP_APU_DSP_ACK`, keyboard layout).
5. **Official DLC** (optional local ZIP or folder): imports Midtown Madness 3 content
   packs into `game_files\TDATA\4d53002a\$C` and reports which of the six official packs
   are installed or missing. Xbox title updates are excluded: replacing the retail
   executable would invalidate the native build's code hash. DLC is separate from
   the community cars and is never downloaded or bundled by Setup.

Players must already own the original Xbox game and supply lawfully obtained game
and DLC files. Owning a disc does not automatically authorize third-party downloads
or redistribution. The [main README](../README.md#ownership-disc-images-and-dlc) explains
the preferred own-disc backup route, local-law limits and the requested source links.

Safety rules: it never deletes or overwrites an existing `game_files\` that is not a complete
install (saved games live there); it extracts into `game_files.partial` and renames only when
finished; it rejects file names in a disc image that could escape the install folder
(`..`, drive letters, backslashes, NTFS stream names); and it checks free disk space first.

## Files

| File | Purpose |
|---|---|
| `installer.py` | The whole program: the window, the checks and every install step. |
| `build_car_packages.py` | Converts the downloaded community `.ar` cars into `car-packages/` (once). |
| `build.py` | Stages the payload and runs PyInstaller to produce `MM3-Setup.exe`. |
| `.gitignore` | Keeps `MM3-Setup.exe`, `car-packages/`, `downloads/` and `build-work/` out of git. |

## Build the installer

You need a built game (`scripts\setup.ps1` does it, giving
`build\RelWithDebInfo\mm3_recomp.exe` and the extracted `game_files`), and a Python with
PyInstaller (`pip install pyinstaller`).

```powershell
# 1. Once: convert the community cars (skip if you do not want the extra-car option).
#    Put the eight MM2X packs listed in tools/car-models/README.md anywhere under
#    installer\downloads (or pass --downloads <folder>). Hashes are checked.
tools\venv\Scripts\python.exe installer\build_car_packages.py

# 2. Build the exe. Result: installer\MM3-Setup.exe
python installer\build.py
```

`build.py` bundles the newest `mm3_recomp.exe` under `build\`, `vcruntime140.dll` from the
Windows Python installation, `scripts/extract_disc.py`,
`xdvdfs.py`, the car-mods tools, `tools/car-mods/cars.local.json` (your tuned manifest; it
falls back to `cars.imported.json`), the car packages and each pack's original readme.
Rebuild whenever you rebuild the game.

## Headless use and testing

```text
MM3-Setup.exe --cli --iso <image> [--dlc <zip-or-folder>] [--cars] [--no-improvements] [--dest <dir>] [--log-file <file>]
MM3-Setup.exe --iso <image> --cars --install-now      (opens the window and starts at once)
```

`--cli` installs without a window. Without `--cli`, `--iso`, `--dlc`, `--cars` and `--no-improvements`
pre-fill the window. A quick way to test without a retail disc is to build a bare XDVDFS image
from an existing `game_files` (pristine `Data_*.zip` from `.mm3-car-mods`, minus the files in
its `state.json`).

### Validation of Setup 1.3 (21 October 2026)

Same payload as 1.2 apart from the game executable, which adds the `[MM3_ZONE]`
mission-zone log lines. A fresh `--cli` install (disc, DLC ZIP, nine cars) and a second run
over it passed as for 1.2: all 456 disc files, six DLC packs, no car file changed on the
second run, an existing save kept, and the installed executable is the newest build.

### Validation of Setup 1.2 (21 October 2026)

- Built with `--packages out\car-models\packages`: the nine cars as installed on the
  maintainer's PC (recreated by `tools/car-models/rebuild_packages_from_install.py`, lamps
  anchored to the body). Setup carries `tools/car-mods` but not the model tools, so
  `prepare.py` leaves the traffic cars' retail reflections alone in its installs.
- A `--cli` install from the same redump and DLC ZIP as 1.1 extracted all 456 disc files,
  detected all six packs (596 files) and installed the nine cars. Its `mm3_recomp.exe` is
  the newest local build, and the installed car models match the packages byte for byte.
- Running Setup again over that install skipped extraction, replaced the executable and
  changed no car file. A run without the cars restored the original car data and kept
  the DLC and an existing save; only the cars' empty folders remain.
- The installed game was not started in these runs; the same executable ran in the
  maintainer's own sessions.

### Validation of Setup 1.1 (18 October 2026)

- The supplied Europe/Australia multilingual redump (SHA-1
  `3411caf817c8d63bfbd582d9a347da81ef652cb5`) passed the code check. All 456 extracted
  files matched the image, allowing the documented one-byte media patch.
- The rebuilt standalone executable imported the supplied `MidtownMadness3DLC.zip`
  (SHA-256 `69ce447f8382b33b1a69eea7a5f77333f44d05ca386918c8c64350565a0bc647`), detected
  all six packs, installed all nine optional cars and preserved an existing save file.
- The installed game executable matches the newest local build; its required
  `vcruntime140.dll` is included. Source tests cover DLC signatures, incomplete packs,
  CRC failures, path traversal, repeat imports, saved data and incomplete disc extracts.
- A second standalone run with Python removed from `PATH` restored the original cars,
  selected the original graphics settings and preserved all 596 DLC files and the save.
  The installed game reached rendered output at about 60 FPS and closed with exit code 0.
  These checks used this Windows PC; a separate clean Windows VM and interactive gameplay
  were not tested.

```powershell
tools\venv\Scripts\python.exe -m unittest discover -s tests -p test_installer.py -v
```

## Publishing

Attach `MM3-Setup.exe` to a GitHub Release (title it e.g. `MM3 Setup 1.0`, asset name exactly
`MM3-Setup.exe` so the README link works). Do not commit it: it holds the compiled game and
the converted cars.

Before publishing, remember what is inside:

- the compiled `mm3_recomp.exe` is a translation of the game's code, which this repository
  otherwise never distributes (`src/recomp/gen/` and `build*/` are ignored on purpose);
- the car packages come from community conversions whose authors ask that their cars are not
  modified or reposted without permission (see the readmes under `Downloaded_Cars`, shipped
  with the cars as `Extra cars - credits.txt`).

Obtain the necessary redistribution rights for both before sharing the installer.
Requiring players to own a disc does not grant the publisher permission to redistribute
translated game code or community assets. Currently the executable always carries the
cars; unticking the installation option does not remove them from the download.
