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

6. **Check for updates** (bottom left of the window): asks GitHub for the newest *stable*
   release (pre-releases are skipped) and, if its version is newer than `VERSION` in
   `installer.py`, offers to download `MM3-Setup.exe` from it. The download must come from
   this repository's own release files, must match the release's `MM3-Setup.exe.sha256` and
   its listed size, and must be a Windows program; otherwise it is discarded. Windows will not
   overwrite a running .exe but lets it be renamed, so the running Setup steps aside as
   `MM3-Setup.exe.<pid>.old`, the new file takes its name, Setup restarts itself (with the same
   command-line options, never straight into an install) and the `.old` file is removed on the
   next start. If the swap fails (a folder Setup cannot write to), the old file is put back.
   Nothing is installed or changed in the game folder by an update; run Setup again over the
   install (it skips extraction and refreshes `mm3_recomp.exe`, the cars and the launcher).
   It only works in the packed exe; from source it offers the release page instead.
   Started over an existing install (`--dest`, or beside it), Setup's window begins with that
   install's saved choices (`setup-options.json`: extra cars, improvements), so installing again
   to update does not take the extra cars away.
   The game's launcher has the same button (`src/mm3_update.c`, WinHTTP + BCrypt, same
   rules and the same release files): it saves the new Setup as `updates\MM3-Setup.exe` in the
   install folder, closes without starting the game and runs it with `--dest <install folder>`.
   **Only for installs made by Setup** (it looks for `setup-options.json` beside the game):
   the maintainer's own build, run from the build folder or copied over an install, only
   reports that a newer release exists and never downloads or replaces anything. Players run the
   published version; the developer exe stays the developer's.
7. **Make an image...** (beside the game image heading) shows how to make an image of your own
   disc: copy it with a modded original Xbox (or a Kreon-firmware drive) and build the `.iso`
   with `extract-xiso -c`. Setup links to no game image downloads.

8. **Finds and judges the game image** (`inspect_image`, `find_game_image`). On start Setup looks in
   its own folder, Downloads, Desktop and Documents (and one folder level below) for `.iso`/`.xiso`
   files over 1 GB and fills in the first one that really is Midtown Madness 3. The verdict is in plain
   words: a folder or an archive instead of an image, a different Xbox game (title id read from
   `default.xbe`), another release of this game, an image that stops early (the files' end is beyond
   the file's end, found before extracting), or a file too small to be a disc.
9. **Judges the install folder** (`check_destination`): refuses a drive root, a network path, Program
   Files and the Windows folder (the game and saves are written next to the exe), warns about OneDrive,
   and shows "N GB free, needs about M GB" next to the folder, with Install disabled when it cannot
   fit. An install already in the folder is named ("Setup 2.2 is installed here with extra cars...
   Install again to update or repair it").
10. **Stops and continues** (Cancel install, or closing the window): the extraction writes each file
   under a temporary name and renames it when whole, into `game_files.partial` with a marker naming the
   image (size and time). The next Install from the same image continues file by file; another image
   starts again. `game_files` is only ever the finished result.
11. **Shortcuts** (default on, `--shortcuts` with `--cli`): `Midtown Madness 3` on the Desktop and in the
    Start menu (current user, no administrator rights), running the start script minimised with the
    game's icon. Made through PowerShell's WScript.Shell with the paths in environment variables;
    the list is kept in `setup-options.json` and is what **Uninstall** removes.
12. **Uninstall...** (shown when `setup-options.json` is in the folder; `--uninstall`, `--delete-game-files`
    on the command line): removes the shortcuts, `mm3_recomp.exe`, the runtime DLL, the start script,
    the credits file and `updates\`. `game_files` (saved games, DLC, Car Workshop backups) and `logs`
    stay unless the user answers a second, default-No question; then `game_files` goes, never through
    a link or junction. A folder without `setup-options.json` (the developer's build folder, a folder
    picked by mistake) is refused untouched.
13. **When something fails**: the window shows **Copy details**, **Open the log** and **Report a problem
    on GitHub**. The log is `%TEMP%\MM3-Setup.log` (this run only); the details and the report link
    hide the user's name (`%USERPROFILE%`).

14. **Update this install** (no image, no DLC pack). When the chosen folder holds an install made by Setup
    (`setup-options.json`) and `check_existing_install` finds it healthy, the window offers **Update
    this install** as the main button (Install turns into "Install from image"); `--cli --update-game
    [--dest ...]` does the same from the command line, with the extra-cars/improvements/shortcuts choices
    the install already had. `install(None, dest, ...)` then reuses `game_files` and the DLC that are
    there and refreshes only what Setup writes: `mm3_recomp.exe` (and `mm3_recomp.pdb` when this Setup
    carries it), the runtime DLL, the extra cars, circuit races and DLC loading screens (`apply_cars`,
    which also re-applies them over the existing data), the credits, the start script, the shortcuts and
    `setup-options.json`. Saved games (`UDATA`), the DLC (`TDATA`), settings and Car Workshop edits are not
    touched, and nothing is extracted or read from an image.
    - *Validation:* `default.xbe` must be the retail program (code hash, and the title id tells "another
      game" from "damaged or another release"), and every disc file the install recorded in
      `setup-disc-files.json` (name and size, written by each install from an image) must be there and
      not emptied. An install made before that file existed is judged by `default.xbe` and
      `Data/Data_hd.zip`. Anything essential missing or damaged is named in plain words and ends in
      "Choose your game image": the window then shows no Update button and Install from the image repairs it
      (`restore_missing_files`, which puts back only the missing files).
    - *Image search:* over a healthy install the automatic search for the image does not run (the box says
      the image is not needed); over a damaged one, or a new folder, it runs once. **Find on this PC**
      (beside "Make an image...") searches whenever asked.
    - *Developer exe:* only a folder with `setup-options.json` is ever updated (the developer's build folder
      has none and is refused untouched). Each install records the SHA-256 of the `mm3_recomp.exe` it wrote
      (`exe_sha256`); an update that finds a different program there (a developer's or hand-made build)
      leaves it and its symbols alone and says so. An install from the image always replaces it, and an
      install made before the hash was recorded cannot tell and is refreshed.
    - Uninstall also removes `mm3_recomp.pdb` and `setup-disc-files.json`.

Tests: `tests/test_installer.py` and `tests/test_installer_experience.py` (fake discs, no network; the
shortcut test makes a real .lnk in a temp folder; the window tests drive the real tkinter window with
patched checks). `python -m unittest discover -s tests -p "test_installer*.py"`.

**Look.** Setup's window and the launcher share one look: the game's own logo
(`Menu/Gfx/mm3_logo.cdds`) on a dark header with a gold edge, a blue accent for the main button.
The logo is game artwork, so it is **never committed**: `build.py` extracts it from the maintainer's
`game_files` into the payload (`stage_branding`; without game_files or Pillow, Setup is built without
it and shows plain text), and CMake runs `tools/branding/generate_logo.py` to turn it into the
launcher's `mm3_logo.h` (an empty logo when the game files are missing).
   Releases must therefore attach `MM3-Setup.exe` **and** `MM3-Setup.exe.sha256`
   (`<hex>  MM3-Setup.exe`), and bump `VERSION` before building, or Setup would keep offering
   the same release.

Players must already own the original Xbox game and supply lawfully obtained game
and DLC files. Owning a disc does not automatically authorize third-party downloads
or redistribution. The [main README](../README.md#ownership-disc-images-and-dlc) explains
the preferred own-disc backup route, local-law limits and the requested source links.

Safety rules: it never deletes or overwrites an existing `game_files\` (saved games live
there); it extracts a new install into `game_files.partial` and renames only when finished; it
rejects file names in a disc image that could escape the install folder (`..`, drive letters,
backslashes, NTFS stream names); and it checks free disk space first.

An existing `game_files\` that lacks some disc files (copied without them, cleaned up, a setup
that stopped half way) is repaired, not refused: when its `default.xbe` is this game's, Setup
restores only the files that are missing or empty, each under a temporary name first, and leaves
every other file as it is (modified car archives, DLC, saves). A folder with another
`default.xbe` is still left untouched. The game's `[MM3_POLICE]` log names the case that made
this matter: a missing `Cars\washingtonpolice_lod1\body.cmp` means no cop can be made in Washington.

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
PyInstaller (`pip install pyinstaller`) and Pillow, which Setup bundles to draw the DLC
races' loading screens (`tools\venv` has both).

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

```text
MM3-Setup.exe --check-update [--log-file <file>]      (says whether a newer release exists)
MM3-Setup.exe --update [--relaunch] [--log-file <file>]   (downloads it and replaces Setup; --relaunch restarts it)
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
