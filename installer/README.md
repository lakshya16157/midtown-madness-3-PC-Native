# MM3 Setup (the installer)

Players: you do not need this folder. Download `MM3-Setup.exe` from the
[latest release](https://github.com/lakshya16157/midtown-madness-3-PC-Native/releases/latest)
and follow the steps at the top of the [main README](../README.md).

This folder holds the source for that installer, for developers.

## What it does

`MM3-Setup.exe` is one self-contained Windows program (Python + tkinter, packed with
PyInstaller). It needs no Python, .NET, Visual Studio or CMake on the target PC. It asks three
things and then:

1. checks the chosen disc image with the same code-hash gate as `scripts/extract_disc.py`
   (the Europe/Australia and USA releases share the same code) and extracts it to
   `game_files\`, applying the standard XISO media patch;
2. copies the bundled `mm3_recomp.exe` next to it;
3. **Add extra cars** (default off): runs `tools/car-mods/prepare.py`'s own plan/apply
   functions with the bundled car packages, or restores the original archives if the cars were
   installed before and are now unticked;
4. **Add game improvements** (default on): writes `Play Midtown Madness 3.cmd` with the
   matching settings. Most fixes are compiled into the game and always on. The tickbox only
   chooses between the game's Balanced defaults (2x render scale, 4x MSAA, 16x anisotropic
   filtering, draw/view distance 2, adaptive aspect, night world light 25%) and variables that
   pin the original look (`RECOMP_RENDER_SCALE=1.0`, `RECOMP_MSAA=1`, `RECOMP_ANISO=1`,
   `RECOMP_DRAW_DISTANCE=1`, `RECOMP_VIEW_DISTANCE=1`, `RECOMP_ASPECT=4:3`,
   `MM3_NIGHT_WORLD_LIGHT=100`, `MM3_PARTICLE_MAX_OVERDRAW=12`, `RECOMP_SHARP_POINT=0`).
   Both modes also set the variables `scripts/play.ps1` sets (`RECOMP_PB_EXEC`, `RECOMP_USB`,
   `RECOMP_FB_WINDOW`, `RECOMP_APU_DSP_ACK`, keyboard layout).

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

`build.py` bundles the newest `mm3_recomp.exe` under `build\`, `scripts/extract_disc.py`,
`xdvdfs.py`, the car-mods tools, `tools/car-mods/cars.local.json` (your tuned manifest; it
falls back to `cars.imported.json`), the car packages and each pack's original readme.
Rebuild whenever you rebuild the game.

## Headless use and testing

```text
MM3-Setup.exe --cli --iso <image> [--cars] [--no-improvements] [--dest <dir>] [--log-file <file>]
MM3-Setup.exe --iso <image> --cars --install-now      (opens the window and starts at once)
```

`--cli` installs without a window. Without `--cli`, `--iso`, `--cars` and `--no-improvements`
pre-fill the window. A quick way to test without a retail disc is to build a bare XDVDFS image
from an existing `game_files` (pristine `Data_*.zip` from `.mm3-car-mods`, minus the files in
its `state.json`).

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

Check that you are comfortable redistributing both, and ask the car authors for permission,
before sharing the installer widely. (Currently the exe always carries the cars; a build
without them would need a small change to `build.py` and `installer.py`.)
