"""Midtown Madness 3 setup: one window, three questions, one standalone .exe.

  1. Where is your Xbox Europe/Australia or USA game image?
  2. Add the extra cars?            (default: no)
  3. Add the game improvements?     (default: yes)

The installer extracts the game from the image, drops in the native game build,
optionally installs the extra cars, and writes a launcher that applies the chosen
settings.  Everything it needs is packed inside this executable (`payload/`).

Headless use (also how the build is tested):
  MM3-Setup.exe --cli --iso <image> [--dlc <zip-or-folder>] [--cars] [--no-improvements] [--shortcuts] [--dest <dir>] [--log-file <file>]
  MM3-Setup.exe --cli --update-game [--dest <dir>] [--cars] [--no-improvements] [--shortcuts]   (no image: reuses the installed game files)
  MM3-Setup.exe --uninstall [--delete-game-files] [--dest <dir>]
"""
from __future__ import annotations

import argparse
import base64
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
import platform
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
import urllib.parse
import urllib.request
import zipfile
import zlib
from pathlib import Path

# Modules that the bundled payload scripts import dynamically. Naming them here lets
# PyInstaller see and include them (it only analyses this file's own imports).
_PAYLOAD_STDLIB = (argparse, collections, copy, hashlib, math, re, struct, tempfile, zipfile, zlib)

VERSION = "2.3.1"
TITLE = "Midtown Madness 3 - Setup"
# "Check for updates" asks GitHub for the newest release and downloads the MM3-Setup.exe attached to it.
RELEASES_API = "https://api.github.com/repos/lakshya16157/midtown-madness-3-PC-Native/releases/latest"
RELEASES_PAGE = "https://github.com/lakshya16157/midtown-madness-3-PC-Native/releases/latest"
RELEASE_ASSET_PREFIX = "https://github.com/lakshya16157/midtown-madness-3-PC-Native/releases/download/"
SETUP_ASSET = "MM3-Setup.exe"
ISSUES_URL = "https://github.com/lakshya16157/midtown-madness-3-PC-Native/issues/new"
MM3_TITLE_ID = 0x4D53002A   # in default.xbe's certificate; every release of the game has it
# The extra cars add ~1.5 GB to the game's own files; the exe, launcher and logs ~0.3 GB.
EXTRA_CARS_BYTES, BASE_EXTRA_BYTES = 1_500_000_000, 300_000_000
SHORTCUT_NAME = "Midtown Madness 3.lnk"
DISC_FILES = "setup-disc-files.json"   # beside setup-options.json: the disc's files, for checks without the image
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

OWN_DISC_TEXT = (
    "An ordinary PC DVD drive cannot read original Xbox discs, so the copy is made with an Xbox:\n\n"
    "1. On a modded original Xbox, use a dashboard with a file manager or FTP (for example "
    "XBMC4Xbox or UnleashX) to copy the whole game disc (drive D:) to your PC. Some PC drives "
    "with Kreon firmware can read Xbox discs directly.\n\n"
    "2. Turn the copied folder into an image with extract-xiso:\n"
    "    extract-xiso -c <copied folder> MidtownMadness3.iso\n\n"
    "3. Choose that .iso here. Setup checks it against the retail game before installing."
)

FROZEN = getattr(sys, "frozen", False)
HERE = Path(sys._MEIPASS) if FROZEN else Path(__file__).resolve().parent
PAYLOAD = HERE / "payload"
APP_DIR = Path(sys.executable).resolve().parent if FROZEN else Path(__file__).resolve().parent
DEFAULT_DEST = APP_DIR / "Midtown Madness 3"

GAME_EXE = "mm3_recomp.exe"
LAUNCHER = "Play Midtown Madness 3.cmd"
LINUX_LAUNCHER = "Play Midtown Madness 3 (Linux).sh"
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


class InstallCancelled(InstallError):
    """The user pressed Cancel; what was done is kept and Install continues from there."""


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


class ImageInfo(typing.NamedTuple):
    ok: bool
    message: str          # plain words, shown under the image box
    total_bytes: int = 0  # what the disc's files take once extracted
    files: int = 0


def xbe_title_id(xbe: bytes) -> int | None:
    """The title id in an Xbox executable's certificate (None when the header is not one)."""
    try:
        if xbe[:4] != b"XBEH":
            return None
        base, certificate = struct.unpack_from("<I", xbe, 0x104)[0], struct.unpack_from("<I", xbe, 0x118)[0]
        offset = certificate - base
        if not base or offset < 0x120 or offset + 12 > len(xbe):
            return None
        return struct.unpack_from("<I", xbe, offset + 8)[0]
    except struct.error:
        return None


def inspect_image(path) -> ImageInfo:
    """Is this an Xbox disc image holding a supported Midtown Madness 3, and is all of it there?

    Reads only the disc's directories and default.xbe (a few MB), then says in plain words what
    is wrong: not an image, a different game, another release, or an image that stops early.
    """
    ed = extract_disc()
    path = Path(str(path).strip().strip('"'))
    if path.is_dir():
        return ImageInfo(False, "That is a folder. Choose the .iso file itself: Setup reads the game "
                                "from a disc image.")
    if not path.is_file():
        return ImageInfo(False, "That file is not there. Check the name, or choose the image with Browse.")
    if path.suffix.lower() in (".zip", ".7z", ".rar", ".gz", ".tar"):
        return ImageInfo(False, "That is an archive. Unpack it first, then choose the .iso inside.")
    try:
        have = path.stat().st_size
    except OSError as error:
        return ImageInfo(False, f"That file cannot be read ({error.strerror or error}).")
    try:
        with ed.Xiso(str(path)) as iso:
            entry = iso.find("default.xbe")
            if entry is None:
                return ImageInfo(False, "This image has no default.xbe, so it is not an Xbox game disc.")
            xbe = bytearray(iso.read(entry))
            digest = ed.code_sha256(xbe)
            files = list(iso.walk()) if digest == ed.CODE_SHA256 else []
            base = getattr(iso, "base", 0)
    except (OSError, ed.XisoError):
        if have < 100_000_000:
            return ImageInfo(False, f"This file is only {have / 1e6:.0f} MB, too small to be a "
                                    "game disc. It may be an incomplete download.")
        return ImageInfo(False, f"This is not a readable Xbox disc image, or it is incomplete ({have / 1e9:.1f} GB). "
                                "Choose the .iso of the game disc, or copy or download it again.")
    except Exception:  # truncated or garbage default.xbe
        return ImageInfo(False, "This image's default.xbe is damaged or is not an Xbox executable. "
                                "Copy or download the image again.")
    if digest != ed.CODE_SHA256:
        title = xbe_title_id(xbe)
        if title is None or title == MM3_TITLE_ID:
            return ImageInfo(False, "This is another release of Midtown Madness 3 than the Europe/Australia "
                                    "and USA discs Setup supports, or its program was modified.")
        return ImageInfo(False, f"This is a different Xbox game (title id {title:08X}), not Midtown Madness 3.")
    total = sum(entry.size for _, entry in files)
    last = max((base + entry.sector * 2048 + entry.size for _, entry in files if entry.size), default=0)
    if last > have:
        return ImageInfo(False, f"This image is incomplete: the file is {have / 1e9:.1f} GB but the game "
                                f"needs {last / 1e9:.1f} GB. Copy or download it again.")
    return ImageInfo(True, f"Midtown Madness 3 image recognised ({total / 1e9:.1f} GB of game files).",
                     total, len(files))


def check_iso(path) -> tuple[bool, str]:
    """inspect_image's verdict as (ok, message)."""
    info = inspect_image(path)
    return info.ok, info.message


def known_folder(name: str) -> Path:
    """A Windows library folder (Downloads, Desktop, Documents) even when OneDrive moved it."""
    guids = {"Desktop": "B4BFCC3A-DB2C-424C-B029-7FE99A87C641",
             "Documents": "FDD39AD0-238F-46AF-ADB4-6C85480369C7",
             "Downloads": "374DE290-123F-4565-9164-39C4925E467B"}
    try:
        import ctypes
        import uuid
        folder_id = ctypes.create_string_buffer(uuid.UUID(guids[name]).bytes_le, 16)
        pointer = ctypes.c_void_p()
        if ctypes.windll.shell32.SHGetKnownFolderPath(folder_id, 0, None, ctypes.byref(pointer)) == 0 and pointer.value:
            text = ctypes.wstring_at(pointer.value)
            ctypes.windll.ole32.CoTaskMemFree(pointer)
            if text:
                return Path(text)
    except Exception:
        pass
    return Path.home() / name


def image_candidates(folders=None) -> list[Path]:
    """Disc images lying where people keep downloads: these folders and their direct subfolders.

    Likely names (midtown, mm3) first, then the newest. Only files over 1 GB count; nothing is read.
    """
    roots = folders if folders is not None else [APP_DIR, known_folder("Downloads"),
                                                 known_folder("Desktop"), known_folder("Documents")]
    found, seen = [], set()
    for root in roots:
        try:
            level = [Path(root)] + [Path(entry.path) for entry in list(os.scandir(root))[:60]
                                    if entry.is_dir(follow_symlinks=False) and not entry.name.startswith(".")]
        except OSError:
            continue
        for folder in level:
            try:
                entries = list(os.scandir(folder))
            except OSError:
                continue
            for entry in entries:
                try:
                    suffix = os.path.splitext(entry.name)[1].lower()
                    if suffix in (".iso", ".xiso") and entry.is_file() and entry.stat().st_size >= 1_000_000_000:
                        key = os.path.normcase(os.path.abspath(entry.path))
                        if key not in seen:
                            seen.add(key)
                            likely = any(word in entry.name.lower() for word in ("midtown", "mm3"))
                            found.append((not likely, -entry.stat().st_mtime, Path(entry.path)))
                except OSError:
                    continue
    return [path for _, _, path in sorted(found)]


def find_game_image(folders=None, limit: int = 6) -> Path | None:
    """The first image in image_candidates that really is Midtown Madness 3 (None if there is none)."""
    for path in image_candidates(folders)[:limit]:
        if inspect_image(path).ok:
            return path
    return None


def space_needed(total_bytes: int, extra_cars: bool, dest: Path) -> int:
    """Bytes an install into `dest` still has to write: the disc's files unless game_files is already there."""
    already = (Path(dest) / "game_files").exists()
    return (0 if already else total_bytes) + (EXTRA_CARS_BYTES if extra_cars else BASE_EXTRA_BYTES)


def check_destination(dest, needed_bytes: int = 0) -> tuple[bool, str]:
    """Can the game be installed into this folder?  (ok, plain-words note for the window)."""
    text = str(dest).strip().strip('"')
    if not text:
        return False, "Choose a folder to install into."
    path = Path(text)
    if text.startswith("\\\\"):
        return False, ("Network folders (\\\\server\\share) are not supported: the start script cannot run "
                       "from one. Use a folder on a local drive, or a mapped drive letter.")
    if path.parent == path:
        return False, f"Choose a folder, not the whole drive: for example {path}Midtown Madness 3."
    if path.exists() and not path.is_dir():
        return False, "That is a file, not a folder."
    protected = [os.environ.get(name) for name in ("SystemRoot", "ProgramFiles", "ProgramFiles(x86)", "ProgramW6432")]
    lowered = os.path.normcase(os.path.abspath(path))
    for folder in filter(None, protected):
        base = os.path.normcase(os.path.abspath(folder))
        if lowered == base or lowered.startswith(base + os.sep):
            return False, ("Windows keeps Program Files and the Windows folder read-only for programs, so saved "
                           "games and settings could not be written there. Choose a folder in your own space, "
                           "such as Documents, or another drive.")
    note = ""
    if "onedrive" in lowered:
        note = " It is inside OneDrive, which would sync the whole game and your saves; a folder outside it is better."
    try:
        free = free_bytes(path)
    except OSError:
        return False, "That drive cannot be read. Choose another folder."
    if needed_bytes and free < needed_bytes:
        return False, (f"Not enough disk space: about {needed_bytes / 1e9:.1f} GB needed, "
                       f"{free / 1e9:.1f} GB free on that drive.")
    need = f" (needs about {needed_bytes / 1e9:.1f} GB)" if needed_bytes else ""
    return True, f"{free / 1e9:.0f} GB free{need}.{note}"


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


def not_an_install_error(game_dir: Path) -> InstallError:
    return InstallError(
        f"{game_dir} already exists but is not a complete Midtown Madness 3 install.\n"
        "It may hold your saved games, so it was left untouched. Move or delete that folder "
        "yourself, or pick another install folder.")


def restore_missing_files(iso_path: Path, dest: Path, log, progress) -> int:
    """Put back the disc files a game_files folder lacks, and touch nothing else.

    A folder that lost files (copied without some of them, cleaned up, a setup that stopped half
    way) looks fine until the game needs one: the Washington police models, for one, left the
    city without cops and said nothing. Only a folder whose default.xbe is this game's is
    repaired. A file that is there and not empty stays exactly as it is (modified car archives,
    DLC, saved games), and each restored file is written under a temporary name first, so a stop
    in the middle leaves no half file behind. Returns how many files were restored.
    """
    ed = extract_disc()
    game_dir = dest / "game_files"
    xbe = game_dir / "default.xbe"
    try:
        genuine = xbe.is_file() and ed.code_sha256(bytearray(xbe.read_bytes())) == ed.CODE_SHA256
    except OSError:
        genuine = False
    if not genuine:
        raise not_an_install_error(game_dir)
    with ed.Xiso(str(iso_path)) as iso:
        lost = []
        for directory, entry in iso.walk():
            target = safe_target(game_dir, directory, entry.name)
            if target.is_dir():
                raise InstallError(f"{target} is a folder, but the disc image has a file with that name. "
                                   "Move or delete that folder yourself, then run setup again.")
            if not target.is_file() or (entry.size and not target.stat().st_size):
                lost.append((directory, entry, target))
        if not lost:
            return 0
        total = sum(entry.size for _, entry, _ in lost)
        if free_bytes(dest) < total + 100_000_000:
            raise InstallError(f"Not enough disk space on that drive to restore the missing game files: "
                               f"about {(total + 100_000_000) / 1e6:.0f} MB needed.")
        names = [f"{directory}/{entry.name}" if directory else entry.name for directory, entry, _ in lost]
        log(f"{len(lost)} game files are missing from {game_dir}: {', '.join(names[:6])}"
            f"{', ...' if len(names) > 6 else ''}. Restoring them from the disc image...")
        done = 0
        for index, (_, entry, target) in enumerate(lost, 1):
            target.parent.mkdir(parents=True, exist_ok=True)
            temporary = target.with_name(target.name + ".restoring")
            try:
                iso.extract(entry, str(temporary))
                os.replace(temporary, target)
            finally:
                temporary.unlink(missing_ok=True)
            done += entry.size
            progress(done / (total or 1), f"Restoring missing game files... {index}/{len(lost)}")
    log(f"Restored {len(lost)} missing game files; everything that was already there was left as it is.")
    return len(lost)


def image_stamp(iso_path: Path) -> str:
    """Identifies one image file, so a stopped extraction is only continued from the same image."""
    status = Path(iso_path).stat()
    return f"{status.st_size}:{status.st_mtime_ns}"


def extract_game(iso_path: Path, dest: Path, extra_cars: bool, log, progress, cancel=None) -> None:
    """Extract the disc into game_files.partial, then rename it to game_files.

    Each file is written under a temporary name and renamed when it is whole, so a stopped or
    crashed extraction (Cancel, power cut) is continued file by file the next time, from the same
    image. `cancel` is a threading.Event; it is looked at between files.
    """
    ed = extract_disc()
    game_dir, partial = dest / "game_files", dest / "game_files.partial"
    marker = partial / ".mm3-setup-image"
    if game_dir.exists():
        raise not_an_install_error(game_dir)
    stamp = image_stamp(iso_path)
    resume = partial.is_dir() and marker.is_file() and marker.read_text(encoding="ascii", errors="replace") == stamp
    if partial.exists() and not resume:  # an unfinished extraction of another image; ours alone
        log("Removing an unfinished extraction of another image...")
        shutil.rmtree(partial)
    with ed.Xiso(str(iso_path)) as iso:
        files = list(iso.walk())
        total = sum(entry.size for _, entry in files) or 1
        needed = space_needed(total, extra_cars, dest)
        if resume:   # the files already there are not written again
            needed -= sum(entry.size for directory, entry in files
                          if safe_target(partial, directory, entry.name).is_file())
        free = free_bytes(dest)
        if free < needed:
            raise InstallError(
                f"Not enough disk space on that drive: about {needed / 1e9:.1f} GB needed, "
                f"{free / 1e9:.1f} GB free.")
        partial.mkdir(parents=True, exist_ok=True)
        marker.write_text(stamp, encoding="ascii")
        for leftover in partial.rglob("*.part"):   # a file that was being written when it stopped
            leftover.unlink(missing_ok=True)
        log(f"{'Continuing the extraction of' if resume else 'Extracting'} {len(files)} files "
            f"({total / 1e9:.1f} GB) from the disc image...")
        done = 0
        for index, (directory, entry) in enumerate(files, 1):
            if cancel is not None and cancel.is_set():
                raise InstallCancelled("Stopped. Press Install to continue where it left off.")
            target = safe_target(partial, directory, entry.name)
            target.parent.mkdir(parents=True, exist_ok=True)
            if not target.is_file():
                temporary = target.with_name(target.name + ".part")
                if not directory and entry.name.lower() == "default.xbe":
                    # Pressed discs ask the drive to authenticate the media, which a PC cannot do;
                    # apply the standard media patch (same as scripts/extract_disc.py).
                    xbe = bytearray(iso.read(entry))
                    offset = ed.media_patch_offset(xbe)
                    if offset is None:
                        raise InstallError("default.xbe does not have the expected layout.")
                    xbe[offset] = ed.MEDIA_PATCH_BYTES[1]
                    temporary.write_bytes(xbe)
                else:
                    iso.extract(entry, str(temporary))
                os.replace(temporary, target)
            done += entry.size
            progress(done / total, f"Extracting game files... {index}/{len(files)}")
    marker.unlink()
    os.replace(partial, game_dir)
    log("Game files extracted.")


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def install_game_exe(dest: Path, log, replace_exe: bool = True) -> None:
    """Copy the game program (and its debug symbols, when this Setup carries them) and the runtime.

    replace_exe=False leaves mm3_recomp.exe and its symbols alone: an update must not replace a
    program that is not the one Setup installed (the developer's own build)."""
    sources = []
    if replace_exe:
        source = PAYLOAD / GAME_EXE
        if not source.is_file():
            raise InstallError("This setup program is damaged (game build missing).")
        sources.append(source)
        symbols = PAYLOAD / (Path(GAME_EXE).stem + ".pdb")
        if symbols.is_file():
            sources.append(symbols)
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
    write_linux_launcher(dest, settings)


def write_linux_launcher(dest: Path, settings: dict) -> None:
    """The same start for Linux with Wine: `sh "Play Midtown Madness 3 (Linux).sh"`.
    Steam (a non-Steam game with Proton 8 or newer), Lutris, Bottles and Heroic can start
    mm3_recomp.exe from the install folder instead; the game sets these itself then."""
    def quoted(value: str) -> str:
        return "'" + value.replace("'", "'\\''") + "'"
    lines = [
        "#!/bin/sh",
        "# Midtown Madness 3 on Linux with Wine, written by MM3 Setup. Safe to edit.",
        "# WINE=/path/to/wine picks another Wine; WINEPREFIX works as usual.",
        'cd "$(dirname "$0")" || exit 1',
        f"if pgrep -f {GAME_EXE} >/dev/null 2>&1; then",
        "  echo 'Midtown Madness 3 is already running. Close it first.'",
        "  exit 1",
        "fi",
    ]
    lines += [f"export {name}={quoted(value)}" for name, value in settings.items()]
    lines += [
        "mkdir -p logs",
        f'"${{WINE:-wine}}" ./{GAME_EXE} > logs/last-run.log 2>&1 || '
        "echo 'The game stopped with an error. Details: logs/last-run.log'",
    ]
    (dest / LINUX_LAUNCHER).write_text("\n".join(lines) + "\n", encoding="ascii", newline="\n")


class InstallCheck(typing.NamedTuple):
    ok: bool              # the install can be updated without the disc image
    message: str          # plain words for the window
    missing: tuple = ()   # disc files that are gone (named for the repair)


def check_existing_install(dest) -> InstallCheck:
    """Is there a healthy install made by Setup in `dest`, so that an update needs no image?

    Needs setup-options.json (Setup's own mark: the developer's build folder and any other folder
    are refused), the retail default.xbe (title id and code hash) and every disc file the install
    recorded still there and not emptied.  An older install recorded no list, so it is judged by
    default.xbe and the main data archive only.  Anything essential missing or damaged is said in
    words that end at the image: that is what repairs it.
    """
    dest = Path(dest)
    if not saved_options(dest):
        return InstallCheck(False, "No install made by Setup is in this folder.")
    game_dir = dest / "game_files"
    xbe = game_dir / "default.xbe"
    if not xbe.is_file():
        return InstallCheck(False, "game_files\\default.xbe is missing. Choose your game image to repair the install.")
    ed = extract_disc()
    try:
        data = bytearray(xbe.read_bytes())
        genuine = ed.code_sha256(data) == ed.CODE_SHA256
    except Exception:   # a cut-off or garbage file
        data, genuine = bytearray(), False
    if not genuine:
        title = xbe_title_id(data)
        who = ("another game's program" if title not in (None, MM3_TITLE_ID)
               else "not the retail Midtown Madness 3 program (damaged, or another release)")
        return InstallCheck(False, f"game_files\\default.xbe is {who}. Choose your game image to repair the install.")
    try:
        recorded = json.loads((dest / DISC_FILES).read_text(encoding="utf-8"))["files"]
        recorded = {str(name): int(size) for name, size in recorded.items()}
    except (OSError, ValueError, KeyError, TypeError, AttributeError):
        recorded = {"Data/Data_hd.zip": 1}
    lost = []
    for name, size in recorded.items():
        directory, _, leaf = name.rpartition("/")
        try:
            target = safe_target(game_dir, directory, leaf)
        except InstallError:
            continue
        if not target.is_file() or (size and not target.stat().st_size):
            lost.append(name)
    if lost:
        shown = ", ".join(lost[:4]) + (", ..." if len(lost) > 4 else "")
        return InstallCheck(False, f"{len(lost)} game files are missing or empty ({shown}). Choose your game "
                                   "image to put them back; your saved games stay.", tuple(lost))
    return InstallCheck(True, "The game files here are complete.")


def record_disc_files(dest: Path, iso_path: Path, log) -> None:
    """Remember which files the disc has, so a later update can notice a lost one without the image."""
    try:
        with extract_disc().Xiso(str(iso_path)) as iso:
            files = {f"{directory}/{entry.name}".lstrip("/"): entry.size for directory, entry in iso.walk()}
        (dest / DISC_FILES).write_text(json.dumps({"files": files}), encoding="utf-8")
    except Exception as error:   # only a convenience for updates: a failure must not fail the install
        log(f"Could not record the disc's file list ({error}); updates will check fewer files.")


def own_exe_changed(dest: Path, recorded) -> bool:
    """True when mm3_recomp.exe is not the program Setup installed (its recorded hash differs).

    That is the developer's own build or a hand-made one: an update leaves it alone.  An install
    that recorded no hash (made by an older Setup) cannot tell, and its program is replaced."""
    exe = Path(dest) / GAME_EXE
    try:
        return isinstance(recorded, str) and exe.is_file() and file_sha256(exe) != recorded
    except OSError:
        return False


def install(iso_path, dest, extra_cars: bool, improvements: bool, log, progress,
            dlc_path: Path | None = None, shortcuts: bool = False, cancel=None) -> Path:
    """Install from the disc image, or (with no image) update the install already in `dest`.

    An update reuses the extracted game files and the DLC that are there (check_existing_install
    must say they are healthy), and refreshes only what Setup writes: the program, its runtime
    and symbols, the extra cars and circuit races with their loading screens, the credits, the
    start script, the shortcuts.  Saved games, settings, Car Workshop edits and DLC stay.
    """
    update_only = iso_path is None or not str(iso_path).strip()
    dest = Path(dest).resolve()
    if not update_only:
        iso_path = Path(str(iso_path).strip().strip('"'))
        ok, message = check_iso(iso_path)
        if not ok:
            raise InstallError(message)
    ok, message = check_destination(dest)
    if not ok:
        raise InstallError(message)
    if update_only:
        status = check_existing_install(dest)
        if not status.ok:
            raise InstallError(status.message)
    try:
        dest.mkdir(parents=True, exist_ok=True)
    except OSError as error:
        raise InstallError(f"Cannot create the install folder {dest}: {error.strerror or error}")
    game_dir = dest / "game_files"
    progress(0.0, "Starting...")
    if update_only:
        log(f"Updating the install in {dest}: its game files and DLC stay as they are.")
        progress(0.1, "Game files checked.")
    elif game_files_ready(game_dir, iso_path):
        log("Game files already present; skipping extraction.")
    elif game_dir.exists():
        restore_missing_files(iso_path, dest, log, lambda f, text: progress(0.05 + 0.80 * f, text))
        if not game_files_ready(game_dir, iso_path):
            raise InstallError("Some game files could not be restored from the disc image. Check that the "
                               "image is the one this game was installed from, then run setup again.")
    else:
        extract_game(iso_path, dest, extra_cars, log, lambda f, text: progress(0.05 + 0.80 * f, text), cancel)
    if not update_only:
        record_disc_files(dest, iso_path, log)
    progress(0.86, "Installing the game...")
    previous = saved_options(dest).get("exe_sha256")
    keep_exe = update_only and own_exe_changed(dest, previous)
    if keep_exe:
        log(f"Kept {GAME_EXE}: it is not the program Setup installed (a developer's or hand-made build), so an "
            "update leaves it alone. Install from your game image to replace it.")
    install_game_exe(dest, log, replace_exe=not keep_exe)
    exe = dest / GAME_EXE
    exe_hash = previous if keep_exe else (file_sha256(exe) if exe.is_file() else None)
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
    # Shortcuts made by an earlier Setup are replaced (or removed when this run has none), so the
    # list below always names exactly the ones that exist.
    remove_shortcuts(saved_options(dest).get("shortcuts"))
    made = create_shortcuts(dest, log) if shortcuts else []
    (dest / "setup-options.json").write_text(json.dumps(
        {"setup_version": VERSION, "extra_cars": extra_cars, "game_improvements": improvements,
         "dlc_packs": dlc_packs, "shortcuts": [str(path) for path in made], "exe_sha256": exe_hash}, indent=2),
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


# ----------------------------------------------------------------------------- shortcuts and uninstall

def shortcut_folders() -> dict[str, Path]:
    """Where the Desktop and Start menu shortcuts go (the current user's, no administrator rights)."""
    folders = {"Desktop": known_folder("Desktop")}
    appdata = os.environ.get("APPDATA")
    if appdata:
        folders["Start menu"] = Path(appdata) / "Microsoft" / "Windows" / "Start Menu" / "Programs"
    return folders


def create_shortcuts(dest: Path, log, folders: dict[str, Path] | None = None) -> list[Path]:
    """Put 'Midtown Madness 3' on the Desktop and in the Start menu.  Never fails the install.

    The shortcut runs the same start script as double-clicking it (minimised, so only the game
    window shows) and wears the game's icon.  Paths travel in environment variables, so a folder
    name with quotes or apostrophes cannot change what PowerShell runs.  Returns the files made.
    """
    dest = Path(dest)
    script = ("$s=(New-Object -ComObject WScript.Shell).CreateShortcut($env:MM3_LNK);"
              "$s.TargetPath=$env:MM3_TARGET;$s.WorkingDirectory=$env:MM3_DIR;$s.IconLocation=$env:MM3_ICON;"
              "$s.WindowStyle=7;$s.Description='Midtown Madness 3 PC Native';$s.Save()")
    made = []
    for label, folder in (folders if folders is not None else shortcut_folders()).items():
        link = Path(folder) / SHORTCUT_NAME
        env = dict(os.environ, MM3_LNK=str(link), MM3_TARGET=str(dest / LAUNCHER), MM3_DIR=str(dest),
                   MM3_ICON=f"{dest / GAME_EXE},0")
        try:
            Path(folder).mkdir(parents=True, exist_ok=True)
            result = subprocess.run(["powershell", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
                                     "-Command", script], env=env, capture_output=True, text=True, timeout=60,
                                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            if result.returncode != 0 or not link.is_file():
                raise OSError((result.stderr or "no shortcut was written").strip().splitlines()[0])
            made.append(link)
            log(f"Shortcut added to the {label}.")
        except (OSError, subprocess.SubprocessError) as error:
            log(f"Could not add the {label} shortcut ({error}). The game starts with '{LAUNCHER}' as before.")
    return made


def remove_shortcuts(paths) -> list[Path]:
    """Delete shortcut files an earlier Setup recorded; anything that is not our .lnk is left alone."""
    removed = []
    for text in paths if isinstance(paths, list) else []:
        link = Path(str(text))
        if link.name == SHORTCUT_NAME and link.is_file():
            try:
                link.unlink()
                removed.append(link)
            except OSError:
                pass
    return removed


def uninstall(dest, log, delete_game_files: bool = False) -> list[str]:
    """Remove what Setup put in `dest`; the game's own files and saves stay unless asked.

    Only a folder with setup-options.json (written by Setup) is touched, so the developer's build
    folder and a folder the user picked by mistake are refused.  Removed: the shortcuts Setup
    made, the game program and its runtime, the start script, the credits file, the downloaded
    Setup in updates\\.  Kept: game_files (saved games, DLC, Car Workshop backups) and logs, unless
    delete_game_files is set; then game_files goes too, but never through a link or junction.
    Returns what was removed, for the window.
    """
    dest = Path(dest)
    options = saved_options(dest)
    if not options:
        raise InstallError(f"{dest} was not installed by Setup (no setup-options.json), so nothing was removed.")
    if game_running():
        raise InstallError("Midtown Madness 3 is running. Close it first.")
    game_dir = dest / "game_files"
    isjunction = getattr(os.path, "isjunction", lambda _: False)
    if delete_game_files and game_dir.exists() and (
            game_dir.is_symlink() or isjunction(game_dir) or not (game_dir / "default.xbe").is_file()):
        raise InstallError(f"{game_dir} is a link or does not look like the game's folder, so nothing was removed.")
    removed = [f"shortcut {path}" for path in remove_shortcuts(options.get("shortcuts"))]
    for name in (GAME_EXE, GAME_EXE + ".new", Path(GAME_EXE).stem + ".pdb", "vcruntime140.dll", LAUNCHER, LINUX_LAUNCHER,
                 "Extra cars - credits.txt", DISC_FILES):
        try:
            (dest / name).unlink()
            removed.append(name)
        except FileNotFoundError:
            pass
    shutil.rmtree(dest / "updates", ignore_errors=True)
    if delete_game_files and game_dir.exists():
        shutil.rmtree(game_dir.with_name("game_files.partial"), ignore_errors=True)
        shutil.rmtree(game_dir)
        removed.append("game_files (the game's files and saved games)")
    (dest / "setup-options.json").unlink(missing_ok=True)
    if not any(dest.iterdir()):
        dest.rmdir()
    log("Removed: " + (", ".join(removed) or "nothing"))
    return removed


# ----------------------------------------------------------------------------- problem reports

LOG_FILE = Path(tempfile.gettempdir()) / "MM3-Setup.log"


class SessionLog:
    """Everything one Install printed, kept in memory and in MM3-Setup.log (in %TEMP%) for support."""

    def __init__(self, path: Path = LOG_FILE):
        self.path, self.lines, self.lock = path, collections.deque(maxlen=400), threading.Lock()
        self.write(f"Setup {VERSION}, {platform.platform()}")

    def write(self, line: str) -> None:
        with self.lock:
            self.lines.append(line)
            try:
                with open(self.path, "a", encoding="utf-8") as handle:
                    handle.write(line + "\n")
            except OSError:
                pass

    def clear(self) -> None:
        with self.lock:
            self.lines.clear()
            try:
                self.path.write_text("", encoding="utf-8")
            except OSError:
                pass


def problem_details(error: str, lines, **choices) -> str:
    """What to paste into a bug report: versions, the choices made, the error and the last log lines.

    The user's name is hidden: their home folder shows as %USERPROFILE%.
    """
    text = "\n".join([f"Midtown Madness 3 Setup {VERSION}", platform.platform(),
                      "Choices: " + ", ".join(f"{name}={value}" for name, value in choices.items()),
                      "", "Problem:", error, "", "Last log lines:", *list(lines)[-40:]])
    home = str(Path.home())
    return text.replace(home, "%USERPROFILE%").replace(home.replace("\\", "/"), "%USERPROFILE%")


def report_url(details: str) -> str:
    """The GitHub new-issue page with the details filled in (kept short enough for a link)."""
    title = "Setup problem: " + next((line for line in details.split("Problem:\n", 1)[-1].splitlines() if line),
                                     "")[:80]
    body = "What I was doing:\n\n```\n" + details[-3000:] + "\n```\n"
    return ISSUES_URL + "?" + urllib.parse.urlencode({"title": title, "body": body})


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

BRANDING = PAYLOAD / "branding"


def branding(name: str):
    """A picture build.py staged into the payload (the game's logo, the icon), or None."""
    path = BRANDING / name
    return path if path.is_file() else None


def banner_png(width: int, height: int, scale: float) -> bytes | None:
    """The header picture, as PNG: the game's logo on a dark blue gradient with a gold edge.
    None when Pillow or the logo is missing (the window then draws plain text)."""
    logo_path = branding("mm3_logo.png")
    if logo_path is None:
        return None
    try:
        from PIL import Image, ImageDraw, ImageFilter
        logo = Image.open(logo_path).convert("RGBA")
    except (ImportError, OSError):
        return None
    top, bottom = (9, 16, 30), (31, 55, 102)
    image = Image.new("RGBA", (width, height))
    draw = ImageDraw.Draw(image)
    for y in range(height):
        t = y / max(1, height - 1)
        draw.line([(0, y), (width, y)], fill=tuple(round(a + (b - a) * t) for a, b in zip(top, bottom)) + (255,))
    stripes = Image.new("RGBA", image.size, (0, 0, 0, 0))   # faint speed stripes at the right
    stripe_draw = ImageDraw.Draw(stripes)
    for i in range(5):
        x = width - round((190 - i * 34) * scale)
        stripe_draw.polygon([(x + round(46 * scale), 0), (x + round(62 * scale), 0), (x + round(16 * scale), height),
                             (x, height)], fill=(255, 255, 255, 12 + i * 3))
    image = Image.alpha_composite(image, stripes)
    target_height = min(round(86 * scale), height - round(18 * scale))
    logo = logo.resize((round(logo.width * target_height / logo.height), target_height), Image.LANCZOS)
    left, upper = round(30 * scale), (height - target_height) // 2 - round(2 * scale)
    shadow = Image.new("RGBA", image.size, (0, 0, 0, 0))
    shadow.paste((0, 0, 0, 150), (left + round(2 * scale), upper + round(4 * scale)), logo.getchannel("A"))
    image = Image.alpha_composite(image, shadow.filter(ImageFilter.GaussianBlur(round(5 * scale))))
    image.alpha_composite(logo, (left, upper))
    ImageDraw.Draw(image).rectangle([0, height - max(3, round(4 * scale)), width, height], fill=(242, 183, 5, 255))
    out = io.BytesIO()
    image.convert("RGB").save(out, "PNG")
    return out.getvalue()


def run_gui(initial_dest: Path, iso: str = "", cars: bool = False, improvements: bool = True,
            install_now: bool = False, dlc: str = "", update_api: str = RELEASES_API,
            shortcuts: bool = True) -> int:
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
    scale = max(1.0, root.winfo_fpixels("1i") / 96.0)

    def px(value: float) -> int:
        return int(round(value * scale))

    icon = branding("mm3.ico")
    if icon:
        try:
            root.iconbitmap(default=str(icon))
        except tk.TclError:
            pass
    page, card_bg, line, ink = "#eceff4", "#ffffff", "#d3d9e2", "#1b2430"
    grey, green, red, accent, accent_dark = "#667085", "#1a7f37", "#c62828", "#0067c0", "#004f95"
    root.configure(bg=page)
    style = ttk.Style()
    try:
        style.theme_use("clam")
    except tk.TclError:
        pass
    heading = ("Segoe UI Semibold", 10)
    body = ("Segoe UI", 9)
    style.configure(".", background=page, foreground=ink, font=body, focuscolor=page)
    style.configure("TButton", padding=(px(12), px(5)), background="#e3e7ee", bordercolor=line,
                    lightcolor="#e3e7ee", darkcolor="#e3e7ee", relief="flat")
    style.map("TButton", background=[("disabled", "#eef0f4"), ("pressed", "#cdd3dd"), ("active", "#d6dbe5")],
              foreground=[("disabled", "#9aa3b2")], lightcolor=[("active", "#d6dbe5")], darkcolor=[("active", "#d6dbe5")])
    style.configure("Accent.TButton", background=accent, foreground="#ffffff", bordercolor=accent,
                    lightcolor=accent, darkcolor=accent, padding=(px(16), px(6)), font=("Segoe UI Semibold", 9))
    style.map("Accent.TButton", background=[("disabled", "#a9c4e0"), ("pressed", "#003d75"), ("active", accent_dark)],
              foreground=[("disabled", "#ffffff")], lightcolor=[("active", accent_dark)], darkcolor=[("active", accent_dark)],
              bordercolor=[("disabled", "#a9c4e0"), ("active", accent_dark)])
    style.configure("Card.TCheckbutton", background=card_bg, foreground=ink, font=heading,
                    indicatorsize=px(17), indicatormargin=(0, 0, px(8), 0), indicatorbackground="#ffffff",
                    indicatorforeground=accent, upperbordercolor="#8b95a5", lowerbordercolor="#8b95a5")
    style.map("Card.TCheckbutton", background=[("active", card_bg)], indicatorbackground=[("pressed", "#e3e7ee")])
    style.configure("Page.TCheckbutton", background=page, foreground=ink, font=body,
                    indicatorsize=px(15), indicatormargin=(0, 0, px(6), 0), indicatorbackground="#ffffff",
                    indicatorforeground=accent, upperbordercolor="#8b95a5", lowerbordercolor="#8b95a5")
    style.map("Page.TCheckbutton", background=[("active", page)], indicatorbackground=[("pressed", "#e3e7ee")])
    style.configure("TEntry", fieldbackground="#ffffff", bordercolor=line, lightcolor=line, darkcolor=line,
                    padding=px(4))
    style.configure("Setup.Horizontal.TProgressbar", troughcolor="#d9dee7", background=accent, bordercolor=line,
                    lightcolor=accent, darkcolor=accent, thickness=px(8))

    iso_var = tk.StringVar()
    dlc_var = tk.StringVar(value=dlc)
    cars_var = tk.BooleanVar(value=cars)                  # default: no extra cars
    improvements_var = tk.BooleanVar(value=improvements)  # default: improvements on
    dest_var = tk.StringVar(value=str(initial_dest))
    shortcuts_var = tk.BooleanVar(value=shortcuts)        # default: Desktop + Start menu shortcuts
    events: queue.Queue = queue.Queue()
    session = SessionLog()
    state = {"check_id": 0, "iso_ok": False, "iso_bytes": 0, "dest_ok": True, "busy": False, "done_dest": None,
             "auto_started": False, "auto_image": "", "cancel": None, "error": "", "installed": False,
             "update_ok": False, "update_message": "", "checking": False, "existing_id": 0, "updating": False,
             "searched": False, "layout": None}

    # Header: the game's logo on a dark gradient.  On a short screen (1080p at 125-150% scaling) the
    # header shrinks, so the Install button is never below the bottom edge.
    compact = root.winfo_screenheight() < px(930)
    width, height = px(700), px(72 if compact else 112)
    banner = tk.Canvas(root, width=width, height=height, bg="#0c1424", highlightthickness=0)
    banner.pack(fill="x")
    picture = banner_png(width, height, scale)
    if picture:
        banner.photo = tk.PhotoImage(data=base64.b64encode(picture))
        banner.create_image(0, 0, anchor="nw", image=banner.photo)
    else:
        banner.create_text(px(28), height // 2, anchor="w", text="MIDTOWN MADNESS 3", fill="#f2b705",
                           font=("Segoe UI", 22, "bold italic"))
    banner.create_text(width - px(26), height * 38 // 100, anchor="e", text="PC Native Setup", fill="#ffffff",
                       font=("Segoe UI Semibold", 15))
    banner.create_text(width - px(26), height * 66 // 100, anchor="e", text=f"Version {VERSION}", fill="#9db4d6",
                       font=("Segoe UI", 10))

    frame = tk.Frame(root, bg=page)
    frame.pack(fill="both", expand=True, padx=px(22), pady=(px(16), px(14)))
    frame.columnconfigure(0, weight=1)

    def card(row: int, number: int, title: str):
        """A white section with a numbered badge; returns the frame to fill, and its heading row."""
        outer = tk.Frame(frame, bg=line)
        outer.grid(row=row, column=0, sticky="we", pady=(0, px(8)))
        inner = tk.Frame(outer, bg=card_bg)
        inner.pack(fill="x", padx=1, pady=1)
        head = tk.Frame(inner, bg=card_bg)
        head.pack(fill="x", padx=px(16), pady=(px(10), px(3)))
        badge = tk.Canvas(head, width=px(24), height=px(24), bg=card_bg, highlightthickness=0)
        badge.create_oval(1, 1, px(24) - 1, px(24) - 1, fill=accent, outline=accent)
        badge.create_text(px(12), px(12), text=str(number), fill="#ffffff", font=("Segoe UI Semibold", 9))
        badge.pack(side="left")
        if title:
            tk.Label(head, text=title, font=heading, bg=card_bg, fg=ink).pack(side="left", padx=(px(8), 0))
        content = tk.Frame(inner, bg=card_bg)
        content.pack(fill="x", padx=px(16), pady=(0, px(10)))
        return head, content

    # 1 - game image
    head, content = card(0, 1, "Xbox Europe/Australia or USA game image (.iso)")
    ttk.Button(head, text="Make an image...",
               command=lambda: messagebox.showinfo("Make an image of your own disc", OWN_DISC_TEXT)).pack(side="right")
    find_button = ttk.Button(head, text="Find on this PC")
    find_button.pack(side="right", padx=(0, px(8)))
    row = tk.Frame(content, bg=card_bg)
    row.pack(fill="x")
    iso_entry = ttk.Entry(row, textvariable=iso_var, width=62)
    iso_entry.pack(side="left", fill="x", expand=True)
    browse = ttk.Button(row, text="Browse...")
    browse.pack(side="left", padx=(px(8), 0))
    iso_status = tk.Label(content, text="Choose a lawfully obtained image of your own Xbox disc.", font=body,
                          fg=grey, bg=card_bg, anchor="w")
    iso_status.pack(fill="x", pady=(px(6), 0))

    # 2 - extra cars
    cars_head, content = card(1, 2, "")
    cars_check = ttk.Checkbutton(cars_head, text="Add extra cars", variable=cars_var, style="Card.TCheckbutton")
    cars_check.pack(side="left", padx=(px(6), 0))
    cars = extra_car_list()

    def car_workshop():
        if messagebox.askyesno("Car Workshop", CAR_WORKSHOP_TEXT + "\n\nOpen its page on GitHub?"):
            import webbrowser
            webbrowser.open(CAR_WORKSHOP_URL)
    workshop = ttk.Button(cars_head, text="Car Workshop...", command=car_workshop)
    workshop.pack(side="right")
    cars_details = ttk.Button(cars_head, text="Car list and credits...",
                              command=lambda: messagebox.showinfo("Extra cars and credits", "\n".join(cars)))
    cars_details.pack(side="right", padx=(0, px(8)))
    if not cars:
        cars_details.state(["disabled"])
    cars_label = tk.Label(content, font=body, fg=grey, bg=card_bg, justify="left", anchor="w",
                          wraplength=px(620),
                          text=("Adds %d community cars. Review the names and authors with Car list and credits. "
                                "The full credits are also saved with the game. Change the new cars "
                                "with Car Workshop, the car toolkit." % len(cars))
                          if cars else "The extra cars are not included in this build.")
    cars_label.pack(fill="x")

    # 3 - improvements
    head, content = card(2, 3, "")
    improvements_check = ttk.Checkbutton(head, text="Add game improvements", variable=improvements_var,
                                         style="Card.TCheckbutton")
    improvements_check.pack(side="left", padx=(px(6), 0))
    tk.Label(content, text=" ".join(IMPROVEMENTS_TEXT.split()), font=body, fg=grey, bg=card_bg, justify="left",
             anchor="w", wraplength=px(620)).pack(fill="x")

    # 4 - official DLC is separate from community cars and is never downloaded by setup.
    head, content = card(3, 4, "Official DLC (optional)")
    dlc_zip = ttk.Button(head, text="Choose ZIP...")
    dlc_zip.pack(side="right")
    dlc_folder = ttk.Button(head, text="Choose folder...")
    dlc_folder.pack(side="right", padx=(0, px(8)))
    dlc_entry = ttk.Entry(content, textvariable=dlc_var, width=74)
    dlc_entry.pack(fill="x")
    tk.Label(content, text=OWNERSHIP_NOTICE + "\nExisting DLC is kept. Setup does not download game images or DLC.",
             font=body, fg=grey, bg=card_bg, justify="left", anchor="w", wraplength=px(620)).pack(fill="x", pady=(px(6), 0))

    # destination
    drow = tk.Frame(frame, bg=page)
    drow.grid(row=4, column=0, sticky="we", pady=(0, px(8)))
    tk.Label(drow, text="Installs to:", font=body, fg=grey, bg=page).pack(side="left")
    dest_text = tk.StringVar()  # shortened copy, so a long path never widens the window
    dest_var.trace_add("write", lambda *_: dest_text.set(shorten(dest_var.get())))
    dest_text.set(shorten(dest_var.get()))
    tk.Label(drow, textvariable=dest_text, font=heading, fg=ink, bg=page, anchor="w", width=52).pack(
        side="left", padx=(px(6), 0))
    change = ttk.Button(drow, text="Change...")
    change.pack(side="right")
    note_row = tk.Frame(frame, bg=page)
    note_row.grid(row=5, column=0, sticky="we", pady=(0, px(6)))
    shortcuts_check = ttk.Checkbutton(note_row, text="Add Desktop and Start menu shortcuts",
                                      variable=shortcuts_var, style="Page.TCheckbutton")
    shortcuts_check.pack(side="right", anchor="n")
    dest_note = tk.Label(note_row, text="", font=body, fg=grey, bg=page, anchor="w", justify="left",
                         wraplength=px(380))
    dest_note.pack(side="left", fill="x", expand=True)

    # progress
    bar = ttk.Progressbar(frame, maximum=1000, length=px(540), style="Setup.Horizontal.TProgressbar")
    bar.grid(row=6, column=0, sticky="we", pady=(px(4), 0))
    status = tk.Label(frame, text="", font=body, fg=grey, bg=page, anchor="w", justify="left", wraplength=px(400))
    status.grid(row=7, column=0, sticky="we", pady=(px(4), px(8)))
    # wrap long messages (errors) at the window's width, whatever the screen's DPI scaling
    frame.bind("<Configure>", lambda event: status.configure(wraplength=max(200, event.width - 20)))

    problem = tk.Frame(frame, bg=page)     # shown only after a failure
    problem.grid(row=8, column=0, sticky="we", pady=(0, px(8)))
    problem.grid_remove()
    copy_button = ttk.Button(problem, text="Copy details")
    copy_button.pack(side="left")
    log_button = ttk.Button(problem, text="Open the log")
    log_button.pack(side="left", padx=(px(8), 0))
    report_button = ttk.Button(problem, text="Report a problem on GitHub")
    report_button.pack(side="left", padx=(px(8), 0))

    buttons = tk.Frame(frame, bg=page)
    buttons.grid(row=9, column=0, sticky="we")
    update_button = ttk.Button(buttons, text="Check for updates")
    update_button.pack(side="left")
    uninstall_button = ttk.Button(buttons, text="Uninstall...")
    cancel_button = ttk.Button(buttons, text="Cancel install")
    play = ttk.Button(buttons, text="Play now", style="Accent.TButton")
    install_button = ttk.Button(buttons, text="Install", style="Accent.TButton", state="disabled")
    install_button.pack(side="right")
    close_button = ttk.Button(buttons, text="Close", command=root.destroy)
    close_button.pack(side="right", padx=(0, px(8)))
    update_game_button = ttk.Button(buttons, text="Update this install", style="Accent.TButton", state="disabled")

    def refresh_cars_colour(*_):
        cars_label.configure(fg="#000000" if cars_var.get() else grey)

    def set_controls(enabled: bool):
        st = "normal" if enabled else "disabled"
        for widget in (iso_entry, browse, find_button, change, cars_check, improvements_check, shortcuts_check,
                       dlc_entry, dlc_zip, dlc_folder, update_button, uninstall_button):
            widget.configure(state=st)
        if cars:
            cars_details.configure(state=st)

    def refresh_install_state():
        """Install needs a recognised image; Update this install needs a healthy install here and no image.
        Both need a usable folder and nothing else running.  With an update on offer it is the main button."""
        idle = state["dest_ok"] and not state["busy"]
        install_button.configure(state="normal" if idle and state["iso_ok"] else "disabled")
        update_game_button.configure(state="normal" if idle and state["update_ok"] else "disabled")
        layout = (state["update_ok"], state["installed"])
        if layout != state["layout"]:
            state["layout"] = layout
            install_button.pack_forget()
            update_game_button.pack_forget()
            install_button.pack(side="right", before=close_button)
            install_button.configure(text="Install from image" if state["update_ok"]
                                     else "Install again" if state["installed"] else "Install",
                                     style="TButton" if state["update_ok"] else "Accent.TButton")
            if state["update_ok"]:
                update_game_button.pack(side="right", before=install_button)

    def image_hint() -> str:
        return ("Not needed to update this install: its game files are reused. Choose an image only to repair "
                "or reinstall it." if state["update_ok"]
                else "Choose a lawfully obtained image of your own Xbox disc.")

    def refresh_dest(*_):
        """The folder's verdict (space, protected folders) and whether an install is already there."""
        folder = dest_var.get()
        has_files = (Path(folder) / "game_files").exists()
        needed = space_needed(state["iso_bytes"], cars_var.get(), Path(folder)) \
            if state["iso_bytes"] or has_files else 0
        ok, message = check_destination(folder, needed)
        state["dest_ok"] = ok
        saved = saved_options(Path(folder)) if ok else {}
        state["installed"] = bool(saved)
        if ok and saved:
            made = ", ".join(name for name, on in (("extra cars", saved.get("extra_cars")),
                                                   ("improvements", saved.get("game_improvements"))) if on)
            here = f"Setup {saved.get('setup_version', '?')} is installed here{' with ' + made if made else ''}."
            if state["update_ok"]:
                message = (f"{here} Update this install refreshes the game program, extra cars and shortcuts and "
                           "keeps its game files, DLC, saved games and settings; no image is needed.")
            elif state["checking"]:
                message = f"{here} Checking its game files..."
            else:
                message = f"{here} {state['update_message'] or 'Install again to update or repair it.'}"
        dest_note.configure(text=message, fg=grey if ok else red)
        if state["installed"] and not state["busy"]:
            uninstall_button.pack(side="left", padx=(px(8), 0))
        else:
            uninstall_button.pack_forget()
        refresh_install_state()

    def refresh_existing(*_):
        """Is there an install here that can be updated without the image?  default.xbe is hashed, so
        this runs off the window's thread; the answer arrives as an "existing" event."""
        state["existing_id"] += 1
        token, folder = state["existing_id"], Path(dest_var.get())
        state["update_ok"], state["update_message"] = False, ""
        state["checking"] = bool(saved_options(folder))
        refresh_dest()
        if not state["checking"]:
            return

        def work():
            try:
                result = check_existing_install(folder)
            except Exception as error:
                result = InstallCheck(False, f"Could not check the install here ({type(error).__name__}: {error}).")
            events.put(("existing", token, result))
        threading.Thread(target=work, daemon=True).start()

    def search_image(manual: bool):
        def look():
            found = find_game_image()
            events.put(("found", str(found) if found else "", manual))
        threading.Thread(target=look, daemon=True).start()

    def auto_search():
        """Look for the image once by itself; never when the box is filled or an update needs none."""
        if not state["searched"] and not iso_var.get().strip():
            state["searched"] = True
            search_image(False)

    def find_clicked():
        iso_status.configure(text="Looking for your image in Downloads, Desktop and Documents...", fg=grey)
        search_image(True)

    def validate(*_):
        state["check_id"] += 1
        token, path = state["check_id"], iso_var.get()
        state["iso_ok"], state["iso_bytes"] = False, 0
        refresh_install_state()
        if not path.strip():
            iso_status.configure(text=image_hint(), fg=grey)
            return
        iso_status.configure(text="Checking the image...", fg=grey)

        def work():
            info = inspect_image(path)
            events.put(("iso", token, info.ok, info.message, info.total_bytes))
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

    def start_install(update_only: bool = False):
        state["busy"] = True
        state["updating"] = update_only
        state["cancel"] = threading.Event()
        set_controls(False)
        refresh_install_state()
        uninstall_button.pack_forget()
        problem.grid_remove()
        play.pack_forget()
        cancel_button.configure(state="normal")
        cancel_button.pack(side="right", padx=(0, px(8)))
        status.configure(text="Starting...", fg=grey)
        session.clear()
        args = ("" if update_only else iso_var.get(), dest_var.get(), cars_var.get(), improvements_var.get())
        chosen_dlc = dlc_var.get().strip().strip('"')
        use_shortcuts, cancel = shortcuts_var.get(), state["cancel"]

        def work():
            try:
                result = install(*args, log=session.write,
                                 progress=lambda f, text: events.put(("progress", f, text)),
                                 dlc_path=Path(chosen_dlc) if chosen_dlc else None,
                                 shortcuts=use_shortcuts, cancel=cancel)
                events.put(("done", str(result), len(installed_dlc_packs(result / "game_files"))))
            except InstallCancelled as error:
                events.put(("cancelled", str(error)))
            except InstallError as error:
                events.put(("error", str(error)))
            except Exception as error:  # unexpected: show something useful rather than vanish
                events.put(("error", f"Unexpected problem: {type(error).__name__}: {error}"))
        threading.Thread(target=work, daemon=True).start()

    def stop_install():
        """Cancel install: the file being written finishes, then everything stops and can be continued."""
        if state["cancel"] is not None:
            state["cancel"].set()
        cancel_button.configure(state="disabled")
        status.configure(text="Stopping after the current file...", fg=grey)

    def details() -> str:
        return problem_details(state["error"], session.lines, image=iso_var.get(), folder=dest_var.get(),
                               extra_cars=cars_var.get(), improvements=improvements_var.get(),
                               shortcuts=shortcuts_var.get())

    def copy_details():
        root.clipboard_clear()
        root.clipboard_append(details())
        status.configure(text="The details are copied. Paste them into a message to whoever helps you.", fg=grey)

    def open_log():
        try:
            os.startfile(str(session.path))
        except OSError as error:
            messagebox.showerror(TITLE, f"Could not open {session.path}: {error.strerror or error}")

    def report_problem():
        import webbrowser
        webbrowser.open(report_url(details()))

    def start_uninstall():
        folder = Path(dest_var.get())
        size = sum(f.stat().st_size for f in (folder / "game_files").rglob("*") if f.is_file()) \
            if (folder / "game_files").is_dir() else 0
        kept = f" ({size / 1e9:.1f} GB)" if size else ""
        if not messagebox.askyesno(TITLE, f"Remove Midtown Madness 3 from {shorten(str(folder), 60)}?\n\n"
                                   "Removed: the game program, the start script, the shortcuts and the downloaded "
                                   f"Setup.\nKept: game_files, with your saved games and DLC{kept}.", icon="question"):
            return
        wipe = bool(size) and messagebox.askyesno(
            TITLE, f"Also delete game_files ({size / 1e9:.1f} GB), including your saved games?\n\n"
            "This cannot be undone. Choose No to keep them: a later Setup finds them again.",
            icon="warning", default="no")
        try:
            uninstall(folder, session.write, wipe)
        except (InstallError, OSError) as error:
            status.configure(text=str(error), fg=red)
            return
        iso_status.configure(text="Removed. Press Install to put the game back.", fg=grey)
        status.configure(text="Midtown Madness 3 was removed." + ("" if wipe else " Your game_files folder stays."),
                         fg=green)
        play.pack_forget()
        refresh_existing()

    def finish_play():
        try:
            launch_game(Path(state["done_dest"]))
        except (InstallError, OSError) as error:
            messagebox.showerror(TITLE, f"Could not start the game: {error}")

    def check_update():
        """The "Check for updates" button: ask GitHub, and on a yes download and swap in the new Setup."""
        state["busy"] = True
        set_controls(False)
        refresh_install_state()
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
        refresh_install_state()

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
                    _, _, ok, message, total = event
                    state["iso_ok"], state["iso_bytes"] = ok, total if ok else 0
                    found = ok and state["auto_image"] and os.path.normcase(iso_var.get()) == state["auto_image"]
                    iso_status.configure(text=("\u2714  " if ok else "\u2716  ")
                                         + ("Found your game image on this PC. " if found else "") + message,
                                         fg=green if ok else red)
                    refresh_dest()
                    if ok and install_now and not state["auto_started"] and state["dest_ok"]:
                        state["auto_started"] = True
                        start_install()
                elif kind == "found":
                    _, path, manual = event
                    if path and (manual or not iso_var.get().strip()):   # an automatic find never replaces a choice
                        state["auto_image"] = os.path.normcase(path)
                        iso_var.set(path)
                    elif manual and not path:
                        iso_status.configure(text="No Midtown Madness 3 image was found in Downloads, Desktop, "
                                                  "Documents or beside Setup. Use Browse.", fg=red)
                elif kind == "existing" and event[1] == state["existing_id"]:
                    result = event[2]
                    state["checking"], state["update_ok"], state["update_message"] = False, result.ok, result.message
                    if not iso_var.get().strip():
                        iso_status.configure(text=image_hint(), fg=grey)
                    if not result.ok:
                        auto_search()          # the install needs the image to be repaired: look for it
                    refresh_dest()
                elif kind == "progress":
                    bar["value"] = event[1] * 1000
                    status.configure(text=event[2], fg=grey)
                elif kind == "done":
                    state["busy"], state["done_dest"] = False, event[1]
                    bar["value"] = 1000
                    cancel_button.pack_forget()
                    shortcut_note = "Shortcuts: Desktop and Start menu.\n" if shortcuts_var.get() else ""
                    kept = "Game files, DLC, saved games and settings were kept.\n" if state["updating"] else ""
                    status.configure(text=f"Done!  {'Updated' if state['updating'] else 'Installed to'} "
                                     f"{shorten(event[1], 60)}\n"
                                     f"Official DLC: {event[2]}/{len(DLC_PACKS)} complete packs detected.\n"
                                     f"{kept}{shortcut_note}Start it with \u201c{LAUNCHER}\u201d, or press Play now.",
                                     fg=green)
                    set_controls(True)
                    refresh_existing()
                    play.pack(side="right", padx=(0, 8))
                    play.configure(command=finish_play)
                elif kind in ("error", "cancelled"):
                    state["busy"] = False
                    state["error"] = event[1]
                    bar["value"] = 0
                    cancel_button.pack_forget()
                    status.configure(text=event[1], fg=red if kind == "error" else grey)
                    if kind == "error":
                        session.write("ERROR: " + event[1])
                        problem.grid()
                    set_controls(True)
                    refresh_existing()
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
        if state["busy"] and not messagebox.askyesno(
                TITLE, "Setup is still working. Quit anyway?\n\nWhat is extracted so far is kept, and the next "
                       "Install continues from there."):
            return
        root.destroy()

    browse.configure(command=pick_iso)
    dlc_zip.configure(command=pick_dlc_zip)
    dlc_folder.configure(command=pick_dlc_folder)
    change.configure(command=pick_dest)
    update_button.configure(command=check_update)
    install_button.configure(command=start_install)
    update_game_button.configure(command=lambda: start_install(True))
    find_button.configure(command=find_clicked)
    cancel_button.configure(command=stop_install)
    uninstall_button.configure(command=start_uninstall)
    copy_button.configure(command=copy_details)
    log_button.configure(command=open_log)
    report_button.configure(command=report_problem)
    iso_var.trace_add("write", lambda *_: root.after(350, validate))
    dest_var.trace_add("write", refresh_existing)
    cars_var.trace_add("write", refresh_dest)
    cars_var.trace_add("write", refresh_cars_colour)
    refresh_cars_colour()
    refresh_existing()
    root.protocol("WM_DELETE_WINDOW", on_close)
    root.after(100, poll)
    if FROZEN:  # what an earlier update left behind; the old copy may still be closing, so twice
        for delay in (500, 6000):
            root.after(delay, lambda: cleanup_old_versions(Path(sys.executable).resolve()))
    if iso:
        iso_var.set(iso)
    elif not state["checking"]:   # a new install: look where downloads usually land, so most need not browse
        auto_search()             # (over an existing install the check above decides: an update needs no image)
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
    parser.add_argument("--shortcuts", action="store_true",
                        help="with --cli: add Desktop and Start menu shortcuts (the window offers them by default)")
    parser.add_argument("--update-game", action="store_true",
                        help="update the install in --dest without the image: refresh the game program, extra "
                             "cars, launcher script and shortcuts, keeping game_files, DLC, saves and settings")
    parser.add_argument("--uninstall", action="store_true",
                        help="remove what Setup installed in --dest (the game's files and saved games stay), then exit")
    parser.add_argument("--delete-game-files", action="store_true",
                        help="with --uninstall: also delete game_files, including saved games")
    parser.add_argument("--install-now", action="store_true", help="open the window and start at once (needs --iso)")
    parser.add_argument("--log-file", type=Path, help="also write progress to this file (with --cli)")
    parser.add_argument("--check-update", action="store_true",
                        help="say whether a newer Setup is published on GitHub, then exit")
    parser.add_argument("--update", action="store_true",
                        help="download and install the newer Setup if there is one, then exit")
    parser.add_argument("--relaunch", action="store_true", help="with --update: start the new Setup afterwards")
    parser.add_argument("--update-api", default=RELEASES_API, help=argparse.SUPPRESS)  # tests: a local fake release
    args = parser.parse_args(argv)
    headless = args.cli or args.check_update or args.update or args.uninstall or args.update_game
    if not headless:
        # Over an existing install the window starts with what was chosen last time, so installing
        # again (to update the game) does not take the extra cars away by default.
        saved = saved_options(args.dest)
        return run_gui(args.dest, args.iso or "", args.cars or saved.get("extra_cars") is True,
                       not args.no_improvements and saved.get("game_improvements") is not False,
                       args.install_now, str(args.dlc) if args.dlc else "", args.update_api,
                       shortcuts=bool(saved.get("shortcuts")) if "shortcuts" in saved else True)
    if args.cli and not args.iso and not args.uninstall and not args.update_game:
        parser.error("--cli needs --iso (or --update-game for an install that is already there)")
    log_stream = open(args.log_file, "w", encoding="utf-8", buffering=1) if args.log_file else None

    def log(line):
        if log_stream:
            log_stream.write(line + "\n")
        elif sys.stdout:
            print(line, flush=True)

    if args.uninstall:
        try:
            uninstall(args.dest, log, args.delete_game_files)
            return 0
        except (InstallError, OSError) as error:
            log(f"ERROR: {error}")
            return 1
        finally:
            if log_stream:
                log_stream.close()

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
        if args.update_game:   # what the install had, unless the command line says otherwise
            saved = saved_options(args.dest)
            install(None, args.dest, args.cars or saved.get("extra_cars") is True,
                    not args.no_improvements and saved.get("game_improvements") is not False, log, progress,
                    args.dlc, shortcuts=args.shortcuts or bool(saved.get("shortcuts")))
            return 0
        install(args.iso, args.dest, args.cars, not args.no_improvements, log, progress, args.dlc,
                shortcuts=args.shortcuts)
        return 0
    except (InstallError, OSError) as error:
        log(f"ERROR: {error}")
        return 1
    finally:
        if log_stream:
            log_stream.close()


if __name__ == "__main__":
    sys.exit(main())
