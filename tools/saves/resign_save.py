"""Make a Midtown Madness 3 save from a real Xbox load in this port.

The game signs every save file (profile, score) with a 20-byte HMAC-SHA1 at its
end and reports the save as damaged when it does not match. A console keys it
with its kernel's XboxSignatureKey; this port's kernel exports zero keys, so its
saves end in HMAC-SHA1(16 zero bytes, everything before the signature). Saves
shared from consoles (often with a Datel datelinfo.xbx beside them) carry the
console's signature and load as damaged until re-signed.

The save folder name is no key: the game names it after the save's name,
h = (h * 65536 + UTF-16 code unit) mod 0xFFFFFFFFFFC5 as 12 hex digits
(XAPI sub_000808F2), so a folder from a console is already right.

    python tools/saves/resign_save.py <UDATA or a save folder> <output folder>

The input is never changed. The output gets a copy with every signed file
re-signed; each folder name is checked against the name in its SaveMeta.xbx.
"""
import hashlib
import hmac
import shutil
import sys
from pathlib import Path

PORT_KEY = bytes(16)
SIGNATURE = 20
TITLE_ID = "4d53002a"


def folder_name(save_name):
    h = 0
    data = save_name.encode("utf-16-le")
    for i in range(0, len(data), 2):
        h = (h * 65536 + int.from_bytes(data[i:i + 2], "little")) % 0xFFFFFFFFFFC5
    return f"{h:012X}"


def save_name(meta):
    text = meta.read_bytes().decode("utf-16")
    for line in text.splitlines():
        if line.startswith("Name="):
            return line[5:]
    return None


def signed(data):
    return hmac.new(PORT_KEY, data[:-SIGNATURE], hashlib.sha1).digest()


def resign_folder(folder):
    """Re-sign every signed file in one save folder; returns a report line."""
    meta = folder / "SaveMeta.xbx"
    name = save_name(meta) if meta.is_file() else None
    notes = []
    if name is not None and folder_name(name) != folder.name.upper():
        notes.append(f"folder should be {folder_name(name)} for '{name}'")
    for path in sorted(folder.iterdir()):
        if not path.is_file() or path.suffix.lower() == ".xbx" or path.stat().st_size <= SIGNATURE:
            continue
        data = path.read_bytes()
        if data[-SIGNATURE:] == signed(data):
            notes.append(f"{path.name} already signed for the port")
            continue
        path.write_bytes(data[:-SIGNATURE] + signed(data))
        notes.append(f"{path.name} re-signed")
    return f"{folder.name} ({name or 'no SaveMeta'}): " + ", ".join(notes or ["nothing to sign"])


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    source, target = Path(sys.argv[1]), Path(sys.argv[2])
    if target.exists():
        raise SystemExit(f"{target} exists; pick a new output folder")
    shutil.copytree(source, target)
    # A UDATA folder, the title folder inside it, or one save folder.
    title = target / TITLE_ID if (target / TITLE_ID).is_dir() else target
    folders = [f for f in title.iterdir() if f.is_dir()] if title.name.lower() == TITLE_ID else [target]
    for folder in sorted(folders):
        print(resign_folder(folder))


if __name__ == "__main__":
    main()
