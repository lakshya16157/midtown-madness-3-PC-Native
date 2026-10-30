"""Midtown Madness 3 setup: one window, three questions, one standalone .exe.

  1. Where is your Xbox Europe/Australia or USA game image?
  2. Add the extra cars?            (default: no)
  3. Add the game improvements?     (default: yes)

The installer extracts the game from the image, drops in the native game build,
optionally installs the extra cars, and writes a launcher that applies the chosen
settings.  Everything it needs is packed inside this executable (`payload/`).

Headless use (also how the build is tested):
  MM3-Setup.exe --cli --iso <image> [--dlc <zip-or-folder>] [--cars] [--no-improvements] [--dest <dir>] [--log-file <file>]
"""
from __future__ import annotations

import argparse
import collections
import contextlib
import copy
import glob
import hashlib
import hmac
import importlib.util
import io
import json
import math
import os
import queue
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import typing
import urllib.error
import urllib.request
import zipfile
import zlib
from pathlib import Path

# Modules that the bundled payload scripts import dynamically. Naming them here lets
# PyInstaller see and include them (it only analyses this file's own imports).
_PAYLOAD_STDLIB = (argparse, collections, copy, hashlib, math, re, struct, tempfile, zipfile, zlib)

VERSION = "2.1"
TITLE = "Midtown Madness 3 - Setup"
# "Check for updates" asks GitHub for the newest release and downloads the MM3-Setup.exe attached to it.
RELEASES_API = "https://api.github.com/repos/lakshya16157/midtown-madness-3-PC-Native/releases/latest"
RELEASES_PAGE = "https://github.com/lakshya16157/midtown-madness-3-PC-Native/releases/latest"
RELEASE_ASSET_PREFIX = "https://github.com/lakshya16157/midtown-madness-3-PC-Native/releases/download/"
SETUP_ASSET = "MM3-Setup.exe"
# The car toolkit is in the project repository (it needs Python), not in the install.
CAR_WORKSHOP_URL = "https://github.com/lakshya16157/midtown-madness-3-PC-Native/tree/main/tools/car-mods"
CAR_WORKSHOP_TEXT = (
    "Car Workshop is the project's car toolkit. With it you can change the new cars: their names, "
    "menu ratings, handling (mass, engine, grip, gears), speedometers and which body each one uses, "
    "then apply the result to your game. It is in the GitHub project under tools\\car-mods "
    "(editor.cmd) and runs with Python 3."
)
OWNERSHIP_NOTICE = (
    "Use your own original Xbox disc and a lawfully obtained game image and DLC. "
    "Owning a disc does not automatically authorize third-party downloads."
)

FROZEN = getattr(sys, "frozen", False)
HERE = Path(sys._MEIPASS) if FROZEN else Path(__file__).resolve().parent
PAYLOAD = HERE / "payload"
APP_DIR = Path(sys.executable).resolve().parent if FROZEN else Path(__file__).resolve().parent
DEFAULT_DEST = APP_DIR / "Midtown Madness 3"

GAME_EXE = "mm3_recomp.exe"
LAUNCHER = "Play Midtown Madness 3.cmd"
CARS_CONFIG = PAYLOAD / "tools" / "car-mods" / "cars.local.json"
DLC_TITLE_ID = "4d53002a"
DLC_PACKS = {
    "4d53002a00000001": ("Car Pack 1", (202, 203, 207, 210)),
    "4d53002a00000002": ("Washington races", ()),
    "4d53002a00000003": ("Paris races", ()),
    "4d53002a00000004": ("Car Pack 2", (204, 205, 206)),
    "4d53002a00000005": ("Car Pack 3", (213, 214, 215)),
    "4d53002a00000006": ("Car Pack 4", (201, 208, 211, 212)),
}

MM2X = "https://www.mm2x.com/"
# Credits for the extra cars, by car id: (name, converted for Midtown Madness 2 by, original
# model/texture sources, download page). Shown next to each car in the window and written to
# "Extra cars - credits.txt". Checked against the original readmes and MM2X listings.
CAR_CREDITS = {
    9004: ("Panoz GTR-1", "Aaro4130",
           "ISI (original rFactor model); skins by Thunder-ice and Alexi99",
           MM2X + "download-1336-panoz-gtr-1#1336"),
    9005: ("McLaren F1 GTR (1995)", "Tsutarja495",
           "Firemint (Real Racing 2 model and textures)",
           MM2X + "download-2214-1995-mclaren-f1-gtr#2214"),
    9006: ("Ferrari F2007 (Formula 1)", "Kunal (Kunal2045)",
           "Image Space Incorporated (original rFactor model); tested by Franch88, Riva and Sajmon14",
           MM2X + "download-1220-ferrari-f2007-f1#1220"),
    9009: ("Ford GT (2017)", "mm2mm1fan",
           "Firemonkeys (Real Racing 3 model), extracted by Eclipse 72rus",
           MM2X + "download-2049-2017-ford-gt#2049"),
    9010: ("McLaren Senna (2019)", "Tsutarja495",
           "Firemonkeys (Real Racing 3 model and textures)",
           MM2X + "download-2221-mclaren-senna-2-pack#2221"),
    9011: ("McLaren Senna GTR (2020)", "Tsutarja495",
           "Firemonkeys (Real Racing 3 model and textures)",
           MM2X + "download-2221-mclaren-senna-2-pack#2221"),
    9012: ("Ferrari FXX K (2015)", "Matyii (Matyasd at MMArchive)",
           "Firemonkeys Studios (Real Racing 3 model)",
           MM2X + "download-1806-2015-ferrari-fxx-k#1806"),
    9013: ("Koenigsegg Regera (2015)", "Matyii (Matyasd at MMArchive)",
           "Firemonkeys Studios (Real Racing 3 model)",
           MM2X + "download-1715-2015-koenigsegg-regera#1715"),
    9014: ("Lamborghini Aventador LP700-4", "BUGATTIMAN253MPH",
           "Firemonkeys (Real Racing 3 body); wheels, brake discs and calipers by Gameloft (GT Racing 2)",
           MM2X + "download-1465-lamborghini-aventador-lp700-4#1465"),
}

# The game needs these to run at all; play.ps1 sets the same ones.
LAUNCH_ESSENTIALS = {
    "MM3_LAUNCHER": "1",
    "RECOMP_PB_EXEC": "1",
    "RECOMP_FB_WINDOW": "1",
    "RECOMP_USB": "1",
    "RECOMP_APU_DSP_ACK": "0x80458810",
    "RECOMP_KEYBOARD": "1",
    "RECOMP_KEYBOARD_LAYOUT": "mm3",
}
# "Improvements on" keeps the game's own Balanced defaults (2x resolution, 4x MSAA,
# 16x filtering, extended draw distance, widescreen) and leaves the in-game graphics
# menu in charge. "Off" pins the original Xbox look.
IMPROVED_SETTINGS = {"MM3_PERFORMANCE_PROFILE": "Balanced"}
ORIGINAL_LOOK_SETTINGS = {
    "MM3_PERFORMANCE_PROFILE": "Balanced",
    "RECOMP_RENDER_SCALE": "1.0",
    "RECOMP_MSAA": "1",
    "RECOMP_ANISO": "1",
    "RECOMP_DRAW_DISTANCE": "1",
    "RECOMP_VIEW_DISTANCE": "1",
    "RECOMP_ASPECT": "4:3",
    "MM3_NIGHT_WORLD_LIGHT": "100",
    "MM3_PARTICLE_MAX_OVERDRAW": "12",
    "RECOMP_SHARP_POINT": "0",
}

IMPROVEMENTS_TEXT = (
    "Sharper image (2x resolution, 4x anti-aliasing, 16x texture filtering), longer draw distance,\n"
    "widescreen that fits your window and better night lighting.  Turn off for the original Xbox\n"
    "look (4:3, 480p, retail distances).  Crash and gameplay bug fixes are always included.\n"
    "The launcher's mods tabs choose the Original or Improved look and Xbox or Modded gameplay."
)


class InstallError(Exception):
    """A problem the user can act on; the message is shown as-is."""


# ----------------------------------------------------------------------------- helpers

def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


_extract_disc = None


def extract_disc():
    """The project's own disc checker/extractor, loaded from the payload."""
    global _extract_disc
    if _extract_disc is None:
        scripts = PAYLOAD / "scripts"
        if not (scripts / "extract_disc.py").is_file():
            raise InstallError("This setup program is damaged (disc tools missing).")
        sys.path.insert(0, str(scripts))
        try:
            _extract_disc = importlib.import_module("extract_disc")
        finally:
            sys.path.remove(str(scripts))
    return _extract_disc


def extra_cars() -> list[tuple[str, str]]:
    """(car name, author who converted it) for each extra car in this build."""
    try:
        cars = json.loads(CARS_CONFIG.read_text(encoding="utf-8"))["cars"]
    except (OSError, ValueError, KeyError):
        return []
    result = []
    for car in cars:
        credit = CAR_CREDITS.get(car["id"])
        result.append((credit[0], credit[1]) if credit else
                      (car.get("display_name", car.get("alias", "?")), "see the credits file"))
    return result


def extra_car_list() -> list[str]:
    return [f"{name}  –  by {author}" for name, author in extra_cars()]


def check_iso(path) -> tuple[bool, str]:
    """Is this an Xbox disc image holding a supported Midtown Madness 3?  Reads ~4 MB."""
    ed = extract_disc()
    path = Path(str(path).strip().strip('"'))
    if not path.is_file():
        return False, "File not found."
    try:
        with ed.Xiso(str(path)) as iso:
            entry = iso.find("default.xbe")
            if entry is None:
                return False, "This image has no default.xbe, so it is not an Xbox game disc."
            digest = ed.code_sha256(bytearray(iso.read(entry)))
    except (OSError, ed.XisoError):
        return False, "This is not a readable Xbox disc image (.iso)."
    except Exception:  # truncated or garbage default.xbe
        return False, "This image's default.xbe is damaged or is not an Xbox executable."
    if digest != ed.CODE_SHA256:
        return False, "This is not a Midtown Madness 3 release this setup supports (Europe/Australia or USA)."
    return True, "Midtown Madness 3 image recognised."


def shorten(path: str, limit: int = 52) -> str:
    """Middle-ellipsis for display: keeps the drive/start and the final folder names."""
    if len(path) <= limit:
        return path
    keep = limit - 3
    head = keep // 3
    return path[:head] + "..." + path[-(keep - head):]


def free_bytes(path: Path) -> int:
    probe = path
    while not probe.exists() and probe != probe.parent:
        probe = probe.parent
    return shutil.disk_usage(probe).free


def safe_target(root: Path, directory: str, name: str) -> Path:
    """Destination for a file named inside the disc image, refusing anything that could
    escape `root` (.., drive letters, backslashes, NTFS stream names...)."""
    parts = [p for p in directory.split("/") if p] + [name]
    for part in parts:
        if (not part or part in (".", "..") or any(c in part for c in '/\\:*?"<>|\x00')
                or part != part.strip() or part.endswith(".")):
            raise InstallError(f"The disc image contains an unsafe file name: {'/'.join(parts)!r}")
    target = root.joinpath(*parts)
    if not target.resolve().is_relative_to(root.resolve()):
        raise InstallError(f"The disc image contains an unsafe file name: {'/'.join(parts)!r}")
    return target


class LineSink(io.TextIOBase):
    """File-like object that forwards complete lines (used to capture prepare.py's output)."""

    def __init__(self, emit):
        self.emit, self.buffer = emit, ""

    def write(self, text):
        self.buffer += text
        while "\n" in self.buffer:
            line, self.buffer = self.buffer.split("\n", 1)
            if line.strip():
                self.emit(line.rstrip())
        return len(text)

    def flush(self):
        pass


# ----------------------------------------------------------------------------- install steps

def game_files_ready(game_dir: Path, iso_path: Path | None = None) -> bool:
    xbe = game_dir / "default.xbe"
    if not xbe.is_file() or not (game_dir / "Data" / "Data_hd.zip").is_file():
        return False
    ed = extract_disc()
    try:
        if ed.code_sha256(bytearray(xbe.read_bytes())) != ed.CODE_SHA256:
            return False
        if iso_path is not None:
            # A previous setup may have stopped after writing the two files above.
            # Check every disc file before reusing it, without rejecting modified
            # car archives or touching the user's saves and DLC.
            with ed.Xiso(str(iso_path)) as iso:
                for directory, entry in iso.walk():
                    target = safe_target(game_dir, directory, entry.name)
                    if not target.is_file() or (entry.size and not target.stat().st_size):
                        return False
        return True
    except Exception:
        return False


def extract_game(iso_path: Path, dest: Path, extra_cars: bool, log, progress) -> None:
    ed = extract_disc()
    game_dir, partial = dest / "game_files", dest / "game_files.partial"
    if game_dir.exists():
        raise InstallError(
            f"{game_dir} already exists but is not a complete Midtown Madness 3 install.\n"
            "It may hold your saved games, so it was left untouched. Move or delete that folder "
            "yourself, or pick another install folder.")
    if partial.exists():  # leftover of an interrupted run; ours alone
        log("Removing an unfinished earlier extraction...")
        shutil.rmtree(partial)
    with ed.Xiso(str(iso_path)) as iso:
        files = list(iso.walk())
        total = sum(entry.size for _, entry in files) or 1
        needed = total + (1_500_000_000 if extra_cars else 300_000_000)
        free = free_bytes(dest)
        if free < needed:
            raise InstallError(
                f"Not enough disk space on that drive: about {needed / 1e9:.1f} GB needed, "
                f"{free / 1e9:.1f} GB free.")
        log(f"Extracting {len(files)} files ({total / 1e9:.1f} GB) from the disc image...")
        done = 0
        for index, (directory, entry) in enumerate(files, 1):
            target = safe_target(partial, directory, entry.name)
            target.parent.mkdir(parents=True, exist_ok=True)
            if not directory and entry.name.lower() == "default.xbe":
                # Pressed discs ask the drive to authenticate the media, which a PC cannot do;
                # apply the standard media patch (same as scripts/extract_disc.py).
                xbe = bytearray(iso.read(entry))
                offset = ed.media_patch_offset(xbe)
                if offset is None:
                    raise InstallError("default.xbe does not have the expected layout.")
                xbe[offset] = ed.MEDIA_PATCH_BYTES[1]
                target.write_bytes(xbe)
            else:
                iso.extract(entry, str(target))
            done += entry.size
            progress(done / total, f"Extracting game files... {index}/{len(files)}")
    os.replace(partial, game_dir)
    log("Game files extracted.")


def install_game_exe(dest: Path, log) -> None:
    source = PAYLOAD / GAME_EXE
    if not source.is_file():
        raise InstallError("This setup program is damaged (game build missing).")
    sources = [source]
    runtime = PAYLOAD / "vcruntime140.dll"
    if runtime.is_file():
        sources.append(runtime)
    for source in sources:
        target = dest / source.name
        temporary = dest / (source.name + ".new")
        shutil.copyfile(source, temporary)
        try:
            os.replace(temporary, target)
        except PermissionError:
            temporary.unlink(missing_ok=True)
            raise InstallError("Midtown Madness 3 is running. Close the game and run setup again.")
        log(f"Installed {source.name}.")


def dlc_required_files(pack: str) -> list[str]:
    required = ["contentmeta.xbx", "contentimage.xbx"]
    for car in DLC_PACKS[pack][1]:
        required += [f"Cars/Tweak/car{car}.txt", f"Cars/car{car}/body.cmp",
                     f"Cars/car{car}/data.ccol"]
    city = {"4d53002a00000002": "Washington", "4d53002a00000003": "Paris"}.get(pack)
    if city:
        for mode in ("Blitz", "Checkpoint"):
            for race in range(10, 20):
                required.append(f"Menu/Gfx/races/{city}/{mode}/{race}.cdds")
                required += [f"Scripts/{city}/{mode}/{race}{suffix}"
                             for suffix in ("Config", "Logic.lua", "Setup.lua")]
    return required


def native_dlc_metadata(data: bytes, pack: str) -> bytes:
    """Validate content identity and sign for this runtime's synthetic zero HDD key.

    The native runtime exposes XboxHDKey = 16 zero bytes, not a console's key.
    Offline installer archives leave this signature blank; a real console's
    signature is also not valid for the native runtime. Never import title updates.
    """
    if len(data) < 108 or data[20:24] != b"XCSF":
        raise InstallError(f"{pack}: invalid contentmeta.xbx.")
    header_size = struct.unpack_from("<I", data, 24)[0]
    title_id, content_index, content_title = struct.unpack_from("<III", data, 36)
    if (not 108 <= header_size <= len(data) or data[32] != 1 or title_id != int(DLC_TITLE_ID, 16)
            or content_title != title_id or content_index != int(pack[-8:], 16)):
        raise InstallError(f"{pack}: contentmeta.xbx belongs to different or damaged content.")
    title_key = hmac.new(bytes(16), data[36:40], hashlib.sha1).digest()
    signature = hmac.new(title_key, data[20:header_size], hashlib.sha1).digest()
    return signature + data[20:]


def installed_dlc_packs(game_dir: Path) -> list[str]:
    content = game_dir / "TDATA" / DLC_TITLE_ID / "$C"
    present = []
    for pack in DLC_PACKS:
        folder = content / pack
        try:
            if not all((folder / name).is_file() and (folder / name).stat().st_size
                       for name in dlc_required_files(pack)):
                continue
            data = (folder / "contentmeta.xbx").read_bytes()
            if native_dlc_metadata(data, pack) == data:
                present.append(pack)
        except (OSError, InstallError):
            continue
    return present


@contextlib.contextmanager
def dlc_source(source: Path):
    """Read only MM3's six content folders from a ZIP or extracted installer."""
    archive = None
    try:
        if source.is_dir():
            members = [(path.relative_to(source).as_posix(), path.stat().st_size, path)
                       for path in source.rglob("*") if path.is_file()]

            def read(path):
                if path.is_symlink() or any(parent.is_symlink() for parent in path.parents
                                             if parent != source.parent):
                    raise InstallError("DLC source contains a symbolic link.")
                return path.read_bytes()
        elif source.is_file() and zipfile.is_zipfile(source):
            archive = zipfile.ZipFile(source)
            members = [(info.filename, info.file_size, info) for info in archive.infolist()
                       if not info.is_dir()]

            def read(info):
                if (info.external_attr >> 16) & 0o170000 == 0o120000:
                    raise InstallError("DLC ZIP contains a symbolic link.")
                return archive.read(info)
        else:
            raise InstallError("Choose a DLC ZIP or an extracted DLC folder (extract RAR/7z files first).")
        packs = {}
        total = 0
        for name, size, member in members:
            parts = name.replace("\\", "/").split("/")
            matches = [i for i, part in enumerate(parts) if part.lower() in DLC_PACKS]
            if not matches:
                continue  # Console installers and nNTSCJ/uNTSC/uPAL updates are excluded.
            if len(matches) != 1 or any(part in ("", ".", "..") for part in parts):
                raise InstallError("DLC source contains an unsafe or ambiguous path.")
            index = matches[0]
            pack, relative = parts[index].lower(), "/".join(parts[index + 1:])
            safe_target(Path.cwd(), "/".join(parts[index + 1:-1]), parts[-1])
            if not relative or Path(relative).suffix.lower() in (".xbe", ".exe", ".dll"):
                raise InstallError("Executable files are not valid MM3 DLC assets.")
            files = packs.setdefault(pack, {})
            if relative.lower() in files:
                raise InstallError(f"DLC source repeats a file in {pack}: {relative}")
            total += size
            if size > 128_000_000 or total > 512_000_000:
                raise InstallError("DLC content is unexpectedly large; check the selected source.")
            files[relative.lower()] = (relative, size, member)
        if not packs:
            raise InstallError("No Midtown Madness 3 DLC content folders found in that source.")
        for pack, files in packs.items():
            if any(name.lower() not in files or not files[name.lower()][1]
                   for name in dlc_required_files(pack)):
                raise InstallError(f"{DLC_PACKS[pack][0]} is incomplete in the DLC source.")
            native_dlc_metadata(read(files["contentmeta.xbx"][2]), pack)
        yield packs, read, total
    except (OSError, ValueError, zipfile.BadZipFile, RuntimeError) as error:
        raise InstallError(f"Cannot read the DLC source: {error}") from error
    finally:
        if archive is not None:
            archive.close()


def install_dlc(source: Path, game_dir: Path, log) -> None:
    """Import local DLC without replacing saves, car edits, or the game's XBE."""
    with dlc_source(Path(source)) as (packs, read, total):
        content = game_dir / "TDATA" / DLC_TITLE_ID / "$C"
        content.mkdir(parents=True, exist_ok=True)
        if free_bytes(content) < total + 10_000_000:
            raise InstallError("Not enough disk space to import the DLC.")
        copied, preserved = 0, 0
        with tempfile.TemporaryDirectory(prefix=".mm3-dlc-", dir=content) as scratch:
            # Read and CRC-check every selected asset before touching existing content.
            staged = Path(scratch)
            for pack, files in packs.items():
                for relative, _, member in files.values():
                    data = read(member)
                    if relative.lower() == "contentmeta.xbx":
                        data = native_dlc_metadata(data, pack)
                    target = safe_target(staged / pack, str(Path(relative).parent).replace("\\", "/")
                                         if "/" in relative else "", Path(relative).name)
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(data)
            for pack, files in packs.items():
                for relative, _, _ in files.values():
                    target = safe_target(content / pack, "/".join(relative.split("/")[:-1]),
                                         relative.split("/")[-1])
                    if target.is_file() and target.stat().st_size and relative.lower() != "contentmeta.xbx":
                        preserved += 1
                        continue
                    target.parent.mkdir(parents=True, exist_ok=True)
                    os.replace(staged / pack / relative, target)
                    copied += 1
        log(f"DLC import: {len(packs)} packs checked, {copied} files installed; "
            f"{preserved} existing assets preserved.")


def report_dlc(game_dir: Path, log) -> list[str]:
    present = installed_dlc_packs(game_dir)
    missing = [DLC_PACKS[pack][0] for pack in DLC_PACKS if pack not in present]
    log(f"Official DLC: {len(present)}/{len(DLC_PACKS)} complete packs detected." +
        (" Missing: " + ", ".join(missing) + ". Use --dlc or the optional DLC selector to import your files."
         if missing else " All 14 DLC cars and both race packs are installed."))
    return present


def apply_cars(game_dir: Path, enable: bool, log) -> None:
    """Install the extra cars, or put the original game data back if they were installed before."""
    prepare = load_module("mm3_prepare", PAYLOAD / "tools" / "car-mods" / "prepare.py")
    sink = LineSink(lambda line: None)
    state, originals = prepare.read_install(game_dir)
    if enable:
        log("Adding the extra cars and the circuit races (this takes a minute)...")
        config = prepare.load_config(CARS_CONFIG)
    else:
        # The circuit races are always installed; without extra cars the
        # catalog keeps only the original cars.
        log("Adding the circuit races...")
        config = {"version": 1, "cars": []}
    with contextlib.redirect_stdout(sink):
        output, counts = prepare.build_plan(game_dir, originals, config, CARS_CONFIG.parent)
        changed = prepare.apply_plan(game_dir, state, originals, output, config)
    log(f"Installed {counts['cars']} extra cars and {counts.get('races', 0)} circuit races, {changed} files updated.")


def write_credits(dest: Path) -> None:
    """Who made each extra car (summary first), then the authors' original readmes unchanged."""
    lines = ["EXTRA CARS - CREDITS AND TERMS", "=" * 30, "",
             "These cars were made by the community members named below, who converted them",
             "for Midtown Madness 2 from models made by the studios named. They were converted",
             "again for Midtown Madness 3 by the tools in this project. Please respect the",
             "authors' terms: several ask that their cars are not modified or reposted without",
             "their permission.", "",
             "CHANGING THE NEW CARS", "-" * 21, "",
             "Car Workshop, the project's car toolkit, changes the new cars' names, menu",
             "ratings, handling, speedometers and bodies for your own game. It is in the",
             "GitHub project under tools\\car-mods (editor.cmd) and needs Python 3:",
             CAR_WORKSHOP_URL, "",
             "WHO MADE WHAT", "-" * 14, ""]
    for name, converter, sources, page in CAR_CREDITS.values():
        lines += [name, f"  Converted for Midtown Madness 2 by: {converter}", f"  Original model: {sources}",
                  f"  Download page: {page}", ""]
    source = PAYLOAD / "credits"
    if source.is_dir():
        lines += ["ORIGINAL READMES (unchanged)", "=" * 28, ""]
        for readme in sorted(source.glob("*.txt")):
            lines += ["-" * 70, readme.stem, "-" * 70,
                      readme.read_text(encoding="utf-8", errors="replace").strip(), ""]
    # utf-8 with a BOM so Notepad shows names like "Matyii" correctly; newline= does the CRLF
    # conversion once (joining with "\r\n" here would have produced "\r\r\n" on Windows).
    (dest / "Extra cars - credits.txt").write_text("\n".join(lines) + "\n", encoding="utf-8-sig", newline="\r\n")


def launch_settings(improvements: bool) -> dict[str, str]:
    """Environment the game is started with (shared by the .cmd launcher and 'Play now')."""
    settings = dict(LAUNCH_ESSENTIALS)
    settings.update(IMPROVED_SETTINGS if improvements else ORIGINAL_LOOK_SETTINGS)
    return settings


def write_launcher(dest: Path, improvements: bool) -> None:
    settings = launch_settings(improvements)
    lines = [
        "@echo off",
        "rem Midtown Madness 3 launcher, written by MM3 Setup. Safe to edit.",
        f"rem Game improvements: {'ON (native upgrades)' if improvements else 'OFF (original Xbox look)'}",
        'cd /d "%~dp0"',
        f'tasklist /fi "imagename eq {GAME_EXE}" 2>nul | find /i "{GAME_EXE}" >nul && (',
        "  echo Midtown Madness 3 is already running. Close it first.",
        "  pause",
        "  exit /b 1",
        ")",
    ]
    lines += [f'set "{name}={value}"' for name, value in settings.items()]
    # Explicit, quoted paths: the install folder may contain spaces, and cmd may be configured
    # (NoDefaultCurrentDirectoryInExePath) not to find programs in the current folder.
    lines += [
        'if not exist "%~dp0logs" mkdir "%~dp0logs"',
        f'"%~dp0{GAME_EXE}" > "%~dp0logs\\last-run.log" 2>&1',
        "if errorlevel 1 (",
        '  echo The game stopped with an error. Details: "%~dp0logs\\last-run.log"',
        "  pause",
        ")",
    ]
    (dest / LAUNCHER).write_text("\n".join(lines) + "\n", encoding="ascii", newline="\r\n")


def install(iso_path, dest, extra_cars: bool, improvements: bool, log, progress,
            dlc_path: Path | None = None) -> Path:
    iso_path, dest = Path(str(iso_path).strip().strip('"')), Path(dest).resolve()
    ok, message = check_iso(iso_path)
    if not ok:
        raise InstallError(message)
    try:
        dest.mkdir(parents=True, exist_ok=True)
    except OSError as error:
        raise InstallError(f"Cannot create the install folder {dest}: {error.strerror or error}")
    game_dir = dest / "game_files"
    progress(0.0, "Starting...")
    if game_files_ready(game_dir, iso_path):
        log("Game files already present; skipping extraction.")
    else:
        extract_game(iso_path, dest, extra_cars, log, lambda f, text: progress(0.05 + 0.80 * f, text))
    progress(0.86, "Installing the game...")
    install_game_exe(dest, log)
    if dlc_path:
        progress(0.87, "Importing official DLC...")
        install_dlc(Path(dlc_path), game_dir, log)
    dlc_packs = report_dlc(game_dir, log)
    progress(0.88, "Extra cars..." if extra_cars else "Checking car data...")
    apply_cars(game_dir, extra_cars, log)
    if extra_cars:
        write_credits(dest)
        log(f"To change the new cars, use Car Workshop, the car toolkit: {CAR_WORKSHOP_URL}")
    else:
        (dest / "Extra cars - credits.txt").unlink(missing_ok=True)
    progress(0.98, "Writing the launcher...")
    write_launcher(dest, improvements)
    (dest / "setup-options.json").write_text(json.dumps(
        {"setup_version": VERSION, "extra_cars": extra_cars, "game_improvements": improvements,
         "dlc_packs": dlc_packs}, indent=2),
        encoding="utf-8")
    progress(1.0, "Done.")
    log(f"Done. Start the game with '{LAUNCHER}' in {dest}")
    return dest


def saved_options(dest: Path) -> dict:
    """The choices an earlier Setup made for the install in `dest` ({} if there is none)."""
    try:
        options = json.loads((Path(dest) / "setup-options.json").read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}
    return options if isinstance(options, dict) else {}


def game_running() -> bool:
    shown = subprocess.run(["tasklist", "/fi", f"imagename eq {GAME_EXE}", "/nh"], capture_output=True,
                           text=True, creationflags=subprocess.CREATE_NO_WINDOW).stdout
    return GAME_EXE.lower() in shown.lower()


def launch_game(dest: Path) -> None:
    """Start the installed game directly (no console window), with the saved settings."""
    if game_running():
        raise InstallError("Midtown Madness 3 is already running. Close it first.")
    try:
        improvements = bool(json.loads((dest / "setup-options.json").read_text())["game_improvements"])
    except (OSError, ValueError, KeyError):
        improvements = True
    (dest / "logs").mkdir(exist_ok=True)
    env = dict(os.environ)
    env.update(launch_settings(improvements))
    with open(dest / "logs" / "last-run.log", "wb") as log:
        subprocess.Popen([str(dest / GAME_EXE)], cwd=dest, env=env, stdout=log, stderr=subprocess.STDOUT,
                         creationflags=subprocess.CREATE_NO_WINDOW)


# ----------------------------------------------------------------------------- self-update

class Update(typing.NamedTuple):
    version: str
    url: str
    size: int
    sha256_url: str
    notes: str
    page: str


def version_key(text: str):
    """Sort key for version names: 2.0 < 2.0.1 < 2.1, and a beta sorts before its final
    ("2.0 beta 2", "v2.0-beta.1" < "2.0"). None when the text holds no number."""
    match = re.match(r"v?(\d+(?:\.\d+)*)(.*)$", text.strip().lower())
    if not match:
        return None
    numbers = [int(part) for part in match.group(1).split(".")]
    while len(numbers) > 1 and numbers[-1] == 0:
        numbers.pop()
    pre = re.search(r"(alpha|beta|pre|rc)\D*(\d*)", match.group(2))
    rank = {"alpha": 0, "beta": 1, "pre": 1, "rc": 2}[pre.group(1)] if pre else 3
    return tuple(numbers), rank, int(pre.group(2) or 0) if pre else 0


def http_get(url: str, accept: str = "*/*"):
    request = urllib.request.Request(url, headers={"User-Agent": f"MM3-Setup/{VERSION}", "Accept": accept})
    try:
        return urllib.request.urlopen(request, timeout=20)
    except urllib.error.HTTPError as error:
        error.close()
        if error.code in (403, 429):
            raise InstallError("GitHub is limiting requests from this connection. Try again in a while.")
        if error.code == 404:
            raise InstallError("No Setup release was found on GitHub.")
        raise InstallError(f"GitHub answered with error {error.code}.")
    except (urllib.error.URLError, OSError, ValueError) as error:
        raise InstallError("Could not reach GitHub. Check your internet connection and try again.") from error


def check_for_update(api_url: str = RELEASES_API, current: str = VERSION) -> Update | None:
    """The newest published release if it is newer than `current`, else None.
    The pre-releases are not offered (GitHub's "latest" skips them). The files must come from this
    project's own releases; only a test pointing `api_url` at a local fake release may use others."""
    trusted = RELEASE_ASSET_PREFIX if api_url == RELEASES_API else ""
    with http_get(api_url, "application/vnd.github+json") as response:
        try:
            release = json.loads(response.read(2_000_000))
            tag = str(release["tag_name"])
            assets = {a["name"]: a for a in release["assets"]}
        except (ValueError, KeyError, TypeError):
            raise InstallError("GitHub's answer could not be understood.")
    latest, mine = version_key(tag), version_key(current)
    if latest is None or mine is None:
        raise InstallError(f"Cannot compare version {tag!r} with {current!r}.")
    if latest <= mine:
        return None
    setup, checksum = assets.get(SETUP_ASSET), assets.get(SETUP_ASSET + ".sha256")
    if not setup or not checksum:
        raise InstallError(f"Version {tag.lstrip('v')} is published, but its Setup is not attached to the "
                           "release yet. Try again later.")
    urls = (setup["browser_download_url"], checksum["browser_download_url"])
    if not all(isinstance(url, str) and url.startswith(trusted) for url in urls):
        raise InstallError("The release points to a download outside the project, so it was not used.")
    return Update(tag.lstrip("v"), urls[0], int(setup.get("size") or 0), urls[1],
                  str(release.get("body") or "").strip(), str(release.get("html_url") or RELEASES_PAGE))


def download_update(update: Update, target: Path, progress) -> None:
    """Fetch the new Setup into `target`, refusing anything that does not match the release's
    published SHA-256 (and size). `target` is removed again if anything goes wrong."""
    with http_get(update.sha256_url) as response:
        words = response.read(4096).decode("ascii", "replace").split()
    expected = words[0].lower() if words else ""
    if not re.fullmatch(r"[0-9a-f]{64}", expected):
        raise InstallError("The release's checksum file is not valid, so the update was not used.")
    if update.size and free_bytes(target.parent) < update.size * 2:
        raise InstallError(f"Not enough disk space next to Setup: about {update.size * 2 / 1e6:.0f} MB needed.")
    digest, done = hashlib.sha256(), 0
    try:
        with http_get(update.url) as response, open(target, "wb") as out:
            total = update.size or int(response.headers.get("Content-Length") or 0)
            while chunk := response.read(1 << 20):
                out.write(chunk)
                digest.update(chunk)
                done += len(chunk)
                progress(done / total if total else 0.0,
                         f"Downloading Setup {update.version}... {done / 1e6:.0f}" +
                         (f" / {total / 1e6:.0f} MB" if total else " MB"))
        if update.size and done != update.size:
            raise InstallError("The download is incomplete. Check your connection and try again.")
        if digest.hexdigest() != expected:
            raise InstallError("The download does not match the release's checksum, so it was discarded.")
        with open(target, "rb") as check:
            if check.read(2) != b"MZ":
                raise InstallError("The download is not a Windows program, so it was discarded.")
    except BaseException as error:
        target.unlink(missing_ok=True)
        if isinstance(error, OSError):
            raise InstallError(f"Could not save the update: {error.strerror or error}") from error
        raise


def apply_update(new_file: Path, current: Path) -> None:
    """Put `new_file` in place of the program `current`, even though it is running: Windows will
    not overwrite or delete a running .exe but does let it be renamed, so the old file steps
    aside as "<name>.<pid>.old" (cleanup_old_versions removes it on a later start)."""
    old = current.with_name(f"{current.name}.{os.getpid()}.old")
    try:
        os.replace(current, old)
    except OSError as error:
        raise InstallError(f"Setup cannot replace itself in {current.parent} ({error.strerror or error}). "
                           f"Download the new version by hand from {RELEASES_PAGE}") from error
    try:
        os.replace(new_file, current)
    except OSError as error:
        with contextlib.suppress(OSError):
            os.replace(old, current)
        raise InstallError(f"Could not put the new Setup in place: {error.strerror or error}") from error


def cleanup_old_versions(current: Path) -> None:
    """Remove what an earlier update left beside Setup (best effort: it may still be running)."""
    leftovers = list(current.parent.glob(glob.escape(current.name) + ".*.old"))
    leftovers.append(current.with_name(current.name + ".download"))
    for path in leftovers:
        with contextlib.suppress(OSError):
            path.unlink(missing_ok=True)


def relaunch(current: Path, args: list[str]) -> None:
    """Start `current` again, detached from this process. A PyInstaller one-file program that starts
    itself must say it is a fresh launch, or the new copy would reuse this copy's temporary files."""
    env = {key: value for key, value in os.environ.items() if not key.startswith("_PYI") and key != "_MEIPASS2"}
    env["PYINSTALLER_RESET_ENVIRONMENT"] = "1"
    subprocess.Popen([str(current), *args], cwd=current.parent, env=env, close_fds=True,
                     creationflags=subprocess.DETACHED_PROCESS | subprocess.CREATE_NEW_PROCESS_GROUP)


def restart_args(argv: list[str]) -> list[str]:
    """What to start the new Setup with: the same options, but never straight into an install."""
    skip = {"--install-now", "--update", "--check-update", "--relaunch"}
    return [arg for arg in argv if arg not in skip]


def self_update_possible() -> bool:
    """Only the packed MM3-Setup.exe can replace itself; from source `sys.executable` is Python."""
    return FROZEN


# ----------------------------------------------------------------------------- GUI

def run_gui(initial_dest: Path, iso: str = "", cars: bool = False, improvements: bool = True,
            install_now: bool = False, dlc: str = "", update_api: str = RELEASES_API) -> int:
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk

    try:  # crisp text on high-DPI screens
        import ctypes
        ctypes.windll.shcore.SetProcessDpiAwareness(1)
    except Exception:
        pass

    root = tk.Tk()
    root.title(TITLE)
    root.resizable(False, False)
    try:
        style = ttk.Style()
        style.theme_use("vista")
    except tk.TclError:
        pass
    pad = {"padx": 18}
    heading = ("Segoe UI", 10, "bold")
    body = ("Segoe UI", 9)
    grey, green, red = "#777777", "#1a7f37", "#c62828"

    iso_var = tk.StringVar()
    dlc_var = tk.StringVar(value=dlc)
    cars_var = tk.BooleanVar(value=cars)                  # default: no extra cars
    improvements_var = tk.BooleanVar(value=improvements)  # default: improvements on
    dest_var = tk.StringVar(value=str(initial_dest))
    events: queue.Queue = queue.Queue()
    state = {"check_id": 0, "iso_ok": False, "busy": False, "done_dest": None, "auto_started": False}

    frame = ttk.Frame(root)
    frame.pack(fill="both", expand=True, pady=(14, 14))

    # 1 - game image
    ttk.Label(frame, text="1.  Xbox Europe/Australia or USA game image (.iso)", font=heading).grid(row=0, column=0, sticky="w", **pad)
    row = ttk.Frame(frame)
    row.grid(row=1, column=0, sticky="we", padx=18, pady=(4, 0))
    iso_entry = ttk.Entry(row, textvariable=iso_var, width=62)
    iso_entry.pack(side="left", fill="x", expand=True)
    browse = ttk.Button(row, text="Browse...", width=10)
    browse.pack(side="left", padx=(8, 0))
    iso_status = tk.Label(frame, text="Choose a lawfully obtained image of your own Xbox disc.", font=body, fg=grey, anchor="w")
    iso_status.grid(row=2, column=0, sticky="we", padx=18, pady=(2, 10))

    # 2 - extra cars
    cars_check = ttk.Checkbutton(frame, text="2.  Add extra cars", variable=cars_var)
    cars_check.grid(row=3, column=0, sticky="w", padx=18)
    cars = extra_car_list()
    cars_details = ttk.Button(frame, text="Car list and credits...",
                              command=lambda: messagebox.showinfo("Extra cars and credits", "\n".join(cars)))
    cars_details.grid(row=3, column=0, sticky="e", padx=18)
    if not cars:
        cars_details.state(["disabled"])
    cars_label = tk.Label(frame, font=body, fg=grey, justify="left", anchor="w",
                          text=("Adds %d community cars. Review the names and authors with Car list and credits.\n"
                                "The full credits are also saved with the game. Change the new cars\n"
                                "with Car Workshop, the car toolkit." % len(cars))
                          if cars else "The extra cars are not included in this build.")
    cars_label.grid(row=4, column=0, sticky="w", padx=(40, 18), pady=(2, 10))

    def car_workshop():
        if messagebox.askyesno("Car Workshop", CAR_WORKSHOP_TEXT + "\n\nOpen its page on GitHub?"):
            import webbrowser
            webbrowser.open(CAR_WORKSHOP_URL)
    workshop = ttk.Button(frame, text="Car Workshop...", command=car_workshop)
    workshop.grid(row=4, column=0, sticky="ne", padx=18, pady=(2, 0))

    # 3 - improvements
    improvements_check = ttk.Checkbutton(frame, text="3.  Add game improvements", variable=improvements_var)
    improvements_check.grid(row=5, column=0, sticky="w", padx=18)
    tk.Label(frame, text=IMPROVEMENTS_TEXT, font=body, fg=grey, justify="left", anchor="w").grid(
        row=6, column=0, sticky="w", padx=(40, 18), pady=(2, 10))

    # Official DLC is separate from community cars and is never downloaded by setup.
    dlc_row = ttk.Frame(frame)
    dlc_row.grid(row=7, column=0, sticky="we", padx=18, pady=(0, 4))
    ttk.Label(dlc_row, text="Official DLC (optional):", font=heading).pack(side="left")
    dlc_zip = ttk.Button(dlc_row, text="Choose ZIP...")
    dlc_zip.pack(side="right")
    dlc_folder = ttk.Button(dlc_row, text="Choose folder...")
    dlc_folder.pack(side="right", padx=(8, 8))
    dlc_entry = ttk.Entry(frame, textvariable=dlc_var, width=74)
    dlc_entry.grid(row=8, column=0, sticky="we", padx=18, pady=(0, 4))
    tk.Label(frame, text=OWNERSHIP_NOTICE + "\nExisting DLC is kept. Setup does not download game images or DLC.",
             font=body, fg=grey, justify="left", wraplength=540).grid(
                 row=9, column=0, sticky="w", padx=18, pady=(0, 10))

    # destination
    drow = ttk.Frame(frame)
    drow.grid(row=10, column=0, sticky="we", padx=18, pady=(0, 8))
    tk.Label(drow, text="Installs to:", font=body, fg=grey).pack(side="left")
    dest_text = tk.StringVar()  # shortened copy, so a long path never widens the window
    dest_var.trace_add("write", lambda *_: dest_text.set(shorten(dest_var.get())))
    dest_text.set(shorten(dest_var.get()))
    tk.Label(drow, textvariable=dest_text, font=body, anchor="w", width=52).pack(side="left", padx=(6, 0))
    change = ttk.Button(drow, text="Change...", width=10)
    change.pack(side="right")

    # progress
    bar = ttk.Progressbar(frame, maximum=1000, length=540)
    bar.grid(row=11, column=0, sticky="we", padx=18, pady=(6, 0))
    status = tk.Label(frame, text="", font=body, fg=grey, anchor="w", justify="left", wraplength=400)
    status.grid(row=12, column=0, sticky="we", padx=18, pady=(2, 8))
    # wrap long messages (errors) at the window's width, whatever the screen's DPI scaling
    frame.bind("<Configure>", lambda event: status.configure(wraplength=max(200, event.width - 40)))

    buttons = ttk.Frame(frame)
    buttons.grid(row=13, column=0, sticky="we", padx=18)
    update_button = ttk.Button(buttons, text="Check for updates", width=18)
    update_button.pack(side="left")
    tk.Label(buttons, text=f"Version {VERSION}", font=body, fg=grey).pack(side="left", padx=(8, 0))
    play = ttk.Button(buttons, text="Play now")
    install_button = ttk.Button(buttons, text="Install", width=14, state="disabled")
    install_button.pack(side="right")
    ttk.Button(buttons, text="Close", width=10, command=root.destroy).pack(side="right", padx=(0, 8))

    def refresh_cars_colour(*_):
        cars_label.configure(fg="#000000" if cars_var.get() else grey)

    def set_controls(enabled: bool):
        st = "normal" if enabled else "disabled"
        for widget in (iso_entry, browse, change, cars_check, improvements_check,
                       dlc_entry, dlc_zip, dlc_folder, update_button):
            widget.configure(state=st)
        if cars:
            cars_details.configure(state=st)

    def validate(*_):
        state["check_id"] += 1
        token, path = state["check_id"], iso_var.get()
        state["iso_ok"] = False
        install_button.configure(state="disabled")
        if not path.strip():
            iso_status.configure(text="Choose a lawfully obtained image of your own Xbox disc.", fg=grey)
            return
        iso_status.configure(text="Checking the image...", fg=grey)

        def work():
            ok, message = check_iso(path)
            events.put(("iso", token, ok, message))
        threading.Thread(target=work, daemon=True).start()

    def pick_iso():
        chosen = filedialog.askopenfilename(
            title="Select the Midtown Madness 3 Europe/Australia or USA disc image",
            filetypes=[("Xbox disc image", "*.iso *.xiso"), ("All files", "*.*")])
        if chosen:
            iso_var.set(os.path.normpath(chosen))

    def pick_dest():
        chosen = filedialog.askdirectory(title="Where should the game be installed?", mustexist=False)
        if chosen:
            folder = Path(chosen)
            # Use the folder itself if it is empty or already holds an install; otherwise make a
            # "Midtown Madness 3" folder inside it so we never mix files into someone's folder.
            reuse = (folder / "game_files").exists() or not folder.exists() or not any(folder.iterdir())
            dest_var.set(os.path.normpath(folder if reuse else folder / "Midtown Madness 3"))

    def pick_dlc_zip():
        chosen = filedialog.askopenfilename(title="Select your lawfully obtained MM3 DLC ZIP",
                                           filetypes=[("DLC ZIP archive", "*.zip")])
        if chosen:
            dlc_var.set(os.path.normpath(chosen))

    def pick_dlc_folder():
        chosen = filedialog.askdirectory(title="Select your extracted MM3 DLC folder")
        if chosen:
            dlc_var.set(os.path.normpath(chosen))

    def start_install():
        state["busy"] = True
        set_controls(False)
        install_button.configure(state="disabled")
        play.pack_forget()
        status.configure(text="Starting...", fg=grey)
        args = (iso_var.get(), dest_var.get(), cars_var.get(), improvements_var.get())
        chosen_dlc = dlc_var.get().strip().strip('"')

        def work():
            try:
                result = install(*args, log=lambda line: events.put(("log", line)),
                                 progress=lambda f, text: events.put(("progress", f, text)),
                                 dlc_path=Path(chosen_dlc) if chosen_dlc else None)
                events.put(("done", str(result), len(installed_dlc_packs(result / "game_files"))))
            except InstallError as error:
                events.put(("error", str(error)))
            except Exception as error:  # unexpected: show something useful rather than vanish
                events.put(("error", f"Unexpected problem: {type(error).__name__}: {error}"))
        threading.Thread(target=work, daemon=True).start()

    def finish_play():
        try:
            launch_game(Path(state["done_dest"]))
        except (InstallError, OSError) as error:
            messagebox.showerror(TITLE, f"Could not start the game: {error}")

    def check_update():
        """The "Check for updates" button: ask GitHub, and on a yes download and swap in the new Setup."""
        state["busy"] = True
        set_controls(False)
        install_button.configure(state="disabled")
        bar["value"] = 0
        status.configure(text="Checking for updates...", fg=grey)

        def work():
            try:
                events.put(("update", check_for_update(update_api)))
            except InstallError as error:
                events.put(("update_failed", str(error)))
            except Exception as error:
                events.put(("update_failed", f"Unexpected problem: {type(error).__name__}: {error}"))
        threading.Thread(target=work, daemon=True).start()

    def update_finished(text: str, colour: str):
        state["busy"] = False
        bar["value"] = 0
        status.configure(text=text, fg=colour)
        set_controls(True)
        install_button.configure(state="normal" if state["iso_ok"] else "disabled")

    def offer_update(update: Update):
        if not self_update_possible():
            if messagebox.askyesno(TITLE, f"Version {update.version} is available (this is {VERSION}).\n\n"
                                   "This copy is not the packed MM3-Setup.exe, so it cannot update itself. "
                                   "Open the download page?"):
                import webbrowser
                webbrowser.open(update.page)
            return update_finished(f"Version {update.version} is available.", grey)
        notes = update.notes if len(update.notes) <= 700 else update.notes[:700].rstrip() + "..."
        size = f" ({update.size / 1e6:.0f} MB)" if update.size else ""
        if not messagebox.askyesno(TITLE, f"Version {update.version} is available (this is {VERSION}).\n\n"
                                   + (notes + "\n\n" if notes else "")
                                   + f"Download it{size} and update Setup now?\n"
                                   "Your installed game and saved games are not touched."):
            return update_finished(f"Version {update.version} is available. Press Check for updates to get it.", grey)
        current = Path(sys.executable).resolve()
        target = current.with_name(current.name + ".download")

        def work():
            try:
                download_update(update, target,
                                lambda f, text: events.put(("progress", f, text)))
                apply_update(target, current)
                events.put(("update_ready", str(current)))
            except InstallError as error:
                events.put(("update_failed", str(error)))
            except Exception as error:
                target.unlink(missing_ok=True)
                events.put(("update_failed", f"Unexpected problem: {type(error).__name__}: {error}"))
        threading.Thread(target=work, daemon=True).start()

    def poll():
        try:
            while True:
                event = events.get_nowait()
                kind = event[0]
                if kind == "iso" and event[1] == state["check_id"]:
                    _, _, ok, message = event
                    state["iso_ok"] = ok
                    iso_status.configure(text=("\u2714  " if ok else "\u2716  ") + message, fg=green if ok else red)
                    install_button.configure(state="normal" if ok and not state["busy"] else "disabled")
                    if ok and install_now and not state["auto_started"]:
                        state["auto_started"] = True
                        start_install()
                elif kind == "progress":
                    bar["value"] = event[1] * 1000
                    status.configure(text=event[2], fg=grey)
                elif kind == "done":
                    state["busy"], state["done_dest"] = False, event[1]
                    bar["value"] = 1000
                    status.configure(text=f"Done!  Installed to {shorten(event[1], 60)}\n"
                                     f"Official DLC: {event[2]}/{len(DLC_PACKS)} complete packs detected.\n"
                                     f"Start it with \u201c{LAUNCHER}\u201d, or press Play now.", fg=green)
                    set_controls(True)
                    install_button.configure(text="Install again", state="normal")
                    play.pack(side="right", padx=(0, 8))
                    play.configure(command=finish_play)
                elif kind == "error":
                    state["busy"] = False
                    bar["value"] = 0
                    status.configure(text=event[1], fg=red)
                    set_controls(True)
                    install_button.configure(state="normal" if state["iso_ok"] else "disabled")
                elif kind == "update":
                    if event[1] is None:
                        update_finished(f"You have the latest version ({VERSION}).", green)
                    else:
                        offer_update(event[1])
                elif kind == "update_failed":
                    update_finished(event[1], red)
                elif kind == "update_ready":
                    status.configure(text="Updated. Restarting Setup...", fg=green)
                    try:
                        relaunch(Path(event[1]), restart_args(sys.argv[1:]))
                    except OSError as error:
                        update_finished(f"Setup was updated but could not restart ({error.strerror or error}). "
                                        "Start it again by hand.", green)
                        return
                    root.destroy()
                    return
        except queue.Empty:
            pass
        root.after(100, poll)

    def on_close():
        if state["busy"] and not messagebox.askyesno(TITLE, "Setup is still working. Quit anyway?"):
            return
        root.destroy()

    browse.configure(command=pick_iso)
    dlc_zip.configure(command=pick_dlc_zip)
    dlc_folder.configure(command=pick_dlc_folder)
    change.configure(command=pick_dest)
    update_button.configure(command=check_update)
    install_button.configure(command=start_install)
    iso_var.trace_add("write", lambda *_: root.after(350, validate))
    cars_var.trace_add("write", refresh_cars_colour)
    refresh_cars_colour()
    root.protocol("WM_DELETE_WINDOW", on_close)
    root.after(100, poll)
    if FROZEN:  # what an earlier update left behind; the old copy may still be closing, so twice
        for delay in (500, 6000):
            root.after(delay, lambda: cleanup_old_versions(Path(sys.executable).resolve()))
    if iso:
        iso_var.set(iso)
    root.mainloop()
    return 0


# ----------------------------------------------------------------------------- entry point

def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--cli", action="store_true", help="install without opening the window")
    parser.add_argument("--iso", help="Xbox game image (pre-fills the window; required with --cli)")
    parser.add_argument("--dlc", type=Path, help="optional lawfully obtained MM3 DLC ZIP or extracted folder")
    parser.add_argument("--dest", type=Path, default=DEFAULT_DEST, help="install folder")
    parser.add_argument("--cars", action="store_true", help="add the extra cars (default: off)")
    parser.add_argument("--no-improvements", action="store_true", help="skip the game improvements (default: on)")
    parser.add_argument("--install-now", action="store_true", help="open the window and start at once (needs --iso)")
    parser.add_argument("--log-file", type=Path, help="also write progress to this file (with --cli)")
    parser.add_argument("--check-update", action="store_true",
                        help="say whether a newer Setup is published on GitHub, then exit")
    parser.add_argument("--update", action="store_true",
                        help="download and install the newer Setup if there is one, then exit")
    parser.add_argument("--relaunch", action="store_true", help="with --update: start the new Setup afterwards")
    parser.add_argument("--update-api", default=RELEASES_API, help=argparse.SUPPRESS)  # tests: a local fake release
    args = parser.parse_args(argv)
    headless = args.cli or args.check_update or args.update
    if not headless:
        # Over an existing install the window starts with what was chosen last time, so installing
        # again (to update the game) does not take the extra cars away by default.
        saved = saved_options(args.dest)
        return run_gui(args.dest, args.iso or "", args.cars or saved.get("extra_cars") is True,
                       not args.no_improvements and saved.get("game_improvements") is not False,
                       args.install_now, str(args.dlc) if args.dlc else "", args.update_api)
    if args.cli and not args.iso:
        parser.error("--cli needs --iso")
    log_stream = open(args.log_file, "w", encoding="utf-8", buffering=1) if args.log_file else None

    def log(line):
        if log_stream:
            log_stream.write(line + "\n")
        elif sys.stdout:
            print(line, flush=True)

    if args.check_update or args.update:
        try:
            update = check_for_update(args.update_api)
            if update is None:
                log(f"Setup {VERSION} is the latest version.")
                return 0
            log(f"Setup {update.version} is available (this is {VERSION}).")
            if args.update:
                if not self_update_possible():
                    raise InstallError("Only the packed MM3-Setup.exe can update itself.")
                current = Path(sys.executable).resolve()
                target = current.with_name(current.name + ".download")
                last = [-1]

                def download_progress(fraction, text):
                    if int(fraction * 10) != last[0]:
                        last[0] = int(fraction * 10)
                        log(text)
                download_update(update, target, download_progress)
                apply_update(target, current)
                log(f"Updated {current.name} to {update.version}.")
                if args.relaunch:
                    relaunch(current, restart_args(argv if argv is not None else sys.argv[1:]))
            return 0
        except InstallError as error:
            log(f"ERROR: {error}")
            return 1
        finally:
            if log_stream:
                log_stream.close()

    last = [-1]

    def progress(fraction, text):
        step = int(fraction * 20)
        if step != last[0]:
            last[0] = step
            log(f"[{fraction * 100:3.0f}%] {text}")

    try:
        log(OWNERSHIP_NOTICE)
        install(args.iso, args.dest, args.cars, not args.no_improvements, log, progress, args.dlc)
        return 0
    except (InstallError, OSError) as error:
        log(f"ERROR: {error}")
        return 1
    finally:
        if log_stream:
            log_stream.close()


if __name__ == "__main__":
    sys.exit(main())
