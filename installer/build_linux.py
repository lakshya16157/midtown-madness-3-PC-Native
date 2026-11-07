"""Build MM3-Linux-x86_64.tar.gz: the Linux counterpart of MM3-Setup.exe.

    python installer/build_linux.py --binary path/to/mm3_recomp [--libs DIR] [--packages DIR] [--downloads DIR]

--binary is the native Linux build (SDL3 GPU renderer, see DEVELOPERS.md, built on Linux or in WSL).
--libs (optional) holds shared libraries the build needs that most systems lack; they are shipped in
lib/ and found through the start script.  The archive holds install.sh, installer.py (the same Setup
as Windows, running on the system's Python 3) and payload/ (game program, extra cars, circuit races).
A Linux user needs Python 3.9+ (python3-tk for the window) and their own game image.
"""
import argparse
import hashlib
import io
import shutil
import sys
import tarfile
from pathlib import Path

import build
from build_car_packages import find_downloads, find_repo

HERE = Path(__file__).resolve().parent
ROOT_NAME = "MM3-Linux"

INSTALL_SH = """#!/bin/sh
# Midtown Madness 3 Setup for Linux: runs installer.py with the system's Python 3.
cd "$(dirname "$0")" || exit 1
if ! command -v python3 >/dev/null 2>&1; then
  echo "Python 3 is needed (Debian/Ubuntu: sudo apt install python3 python3-tk)."
  exit 1
fi
exec python3 installer.py "$@"
"""

README = """Midtown Madness 3 PC Native - Linux
====================================

You need: your own Midtown Madness 3 disc image (.iso, Xbox Europe/Australia or USA),
Python 3.9 or newer, a Vulkan-capable GPU with its driver, and about 10 GB of free space.

Install (a window opens; python3-tk is needed for it):
    sh install.sh

Without a window:
    sh install.sh --cli --iso /path/to/game.iso [--dlc DLC.zip] [--cars] [--shortcuts] [--dest DIR]

It installs to ~/Games/Midtown Madness 3 by default and adds a Desktop and applications-menu entry.
Start the game with the entry or with: sh ~/Games/"Midtown Madness 3"/play-midtown-madness-3.sh

Uninstall (keeps your saves):  sh install.sh --uninstall
Problems: https://github.com/lakshya16157/midtown-madness-3-PC-Native/issues
"""

EXECUTABLE = {"install.sh", "installer.py", "payload/mm3_recomp"}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True, help="the native Linux mm3_recomp")
    parser.add_argument("--libs", type=Path, help="shared libraries to ship in lib/")
    parser.add_argument("--repo", type=Path)
    parser.add_argument("--packages", type=Path, default=HERE / "car-packages")
    parser.add_argument("--downloads", type=Path)
    parser.add_argument("--work", type=Path, default=HERE / "build-work-linux")
    parser.add_argument("--out", type=Path, default=HERE / "MM3-Linux-x86_64.tar.gz")
    args = parser.parse_args()
    repo = (args.repo or find_repo()).resolve()
    work = args.work.resolve()
    if work in (repo, HERE) or work in repo.parents:
        parser.error("--work must be a dedicated scratch directory.")
    if not args.binary.is_file() or args.binary.read_bytes()[:4] != b"\x7fELF":
        parser.error("--binary must be a Linux ELF executable.")

    stage = work / ROOT_NAME
    if work.exists():
        shutil.rmtree(work)
    payload = stage / "payload"
    build.stage_payload(payload, repo, args.packages, find_downloads(args.downloads),
                        linux_binary=args.binary, linux_libs=args.libs)
    # The menu entry's and window's icon is a PNG (made from the game's logo by build.make_icons, the same
    # picture as the Windows icon); the .ico is Windows-only.
    made = payload / "branding" / "mm3.png"
    if made.is_file():
        shutil.move(str(made), payload / "mm3.png")
    else:
        try:
            from PIL import Image
            Image.open(repo / "src" / "mm3.ico").convert("RGBA").save(payload / "mm3.png")
        except (ImportError, OSError) as error:
            print(f"WARNING: no menu icon ({error})")
    (payload / "branding" / "mm3.ico").unlink(missing_ok=True)
    shutil.copy2(HERE / "mm3_settings.py", payload / "mm3_settings.py")   # the Settings window
    shutil.copy2(HERE / "installer.py", stage / "installer.py")
    (stage / "install.sh").write_text(INSTALL_SH, encoding="ascii", newline="\n")
    (stage / "README-Linux.txt").write_text(README, encoding="ascii", newline="\n")

    def mark(info: tarfile.TarInfo) -> tarfile.TarInfo:
        relative = info.name.split("/", 1)[1] if "/" in info.name else ""
        info.uid = info.gid = 0
        info.uname = info.gname = ""
        info.mode = 0o755 if info.isdir() or relative in EXECUTABLE or relative.startswith("payload/lib/") else 0o644
        return info

    args.out.parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(args.out, "w:gz", compresslevel=9) as archive:
        archive.add(stage, arcname=ROOT_NAME, filter=mark)
    digest = hashlib.sha256(args.out.read_bytes()).hexdigest()
    Path(str(args.out) + ".sha256").write_bytes(f"{digest}  {args.out.name}\n".encode("ascii"))   # LF: sha256sum -c
    print(f"built {args.out}  ({args.out.stat().st_size / 1e6:.0f} MB)  sha256 {digest}")
    shutil.rmtree(work, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
