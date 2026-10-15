"""Read-only reference data from MM3's original retail car tweaks.

The workshop must remain usable when an install backup is missing or damaged.
This reader has no dependency on the installer, and never creates a backup or
writes game data. Raw section entries keep duplicates (paint colors, skins,
etc.); the typed convenience dictionaries use the last occurrence of a key.
"""
from __future__ import annotations

import math
from pathlib import Path
import re
import zipfile
import zlib


# Same retail order as prepare.ORIGINAL_CAR_IDS and src/mm3_car_order.c. Includes
# the two retail bonus cars, but excludes DLC and user-added model aliases.
ORIGINAL_CAR_IDS = (
    106, 115, 103, 101, 105, 102, 6, 7, 50, 113, 104, 19, 111,
    108, 110, 107, 109, 10, 21, 14, 23, 12, 1, 24, 9, 2, 16,
    3, 8, 25, 18, 11, 15, 666, 667,
)
DLC_CAR_IDS = (201, 202, 203, 204, 205, 206, 207, 208, 210, 211, 212, 213, 214, 215)
# Exact Cars/Tweak/<stem>.txt spelling of every retail car, in ORIGINAL_CAR_IDS
# order. The archive mixes case (DCBus, Mv_Badguy, Pt, cement, mini), so
# manifests may use any case and lookups compare with casefold().
ORIGINAL_ALIASES = {
    106: "ParisBus", 115: "DCBus", 103: "garbage", 101: "firetruck",
    105: "cement", 102: "ArmoredCar", 6: "Freightliner", 7: "HummerH2",
    50: "Microcar", 113: "Mv_Badguy", 104: "Limo", 19: "escalade",
    111: "ambulance", 108: "ParisTaxi", 110: "WashingtonTaxi",
    107: "ParisPolice", 109: "WashingtonPolice", 10: "Pt", 21: "Opel_Astra",
    14: "Seville", 23: "Renault5", 12: "mini", 1: "ChevySSR", 24: "Saab93",
    9: "NewBeetle", 2: "AudiTT", 16: "Crossfire", 3: "Avant", 8: "MFastBack",
    25: "Corvette", 18: "Lotus", 11: "Viper", 15: "Koenigsegg",
    666: "SerpentMk2", 667: "Avalanche",
}
RATING_KEYS = ("TOPSPEED", "ACCELERATION", "MASS", "HANDLING", "DURABILITY")

# Readable names for aliases whose PAINTJOBNAME is abbreviated, absent, or
# "????" in the retail archive. Other aliases can use their own PAINTJOBNAME.
ORIGINAL_NAMES = {
    "ambulance": "Ambulance", "armoredcar": "Armored Car",
    "auditt": "Audi TT", "avalanche": "Chevrolet Avalanche",
    "avant": "Audi RS 6 Avant", "cement": "Cement Truck",
    "chevyssr": "Chevrolet SSR", "corvette": "Chevrolet Corvette",
    "crossfire": "Chrysler Crossfire", "dcbus": "Washington Bus",
    "escalade": "Cadillac Escalade", "firetruck": "Fire Truck",
    "freightliner": "Freightliner", "garbage": "Garbage Truck",
    "hummerh2": "Hummer H2", "koenigsegg": "Koenigsegg",
    "limo": "Limousine", "lotus": "Lotus Esprit V8",
    "mfastback": "Ford Mustang Fastback", "microcar": "Microcar",
    "mini": "Mini Cooper", "mv_badguy": "Bad Guy Van",
    "newbeetle": "Volkswagen New Beetle", "opel_astra": "Opel Astra",
    "parisbus": "Paris Bus", "parispolice": "Paris Police",
    "paristaxi": "Paris Taxi", "pt": "Chrysler PT Cruiser",
    "renault5": "Renault 5", "saab93": "Saab 9-3",
    "serpentmk2": "Serpent Mk II", "seville": "Cadillac Seville",
    "viper": "Dodge Viper", "washingtonpolice": "Washington Police",
    "washingtontaxi": "Washington Taxi",
}
_MARKER = re.compile(r"^(Begin|End)\s*\(\s*([^()]+?)\s*\)\s*$", re.I)
_TWEAK = re.compile(r"^Cars/Tweak/([^/]+)\.txt$", re.I)
_NUMBER = re.compile(r"^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?$")
_HEX = re.compile(r"^[+-]?0[xX][0-9a-fA-F]+$")


class OriginalStatsError(ValueError):
    """An original-car reference could not be read; safe to show in the UI."""


def parse_sections(text: str) -> list[dict]:
    """Return ordered sections with ordered ``(key, raw_value)`` entries.

    Flags have an empty value; duplicate keys and duplicate section names are
    retained. Newline style, comments, and spacing remain intact in raw_text.
    Unknown sections and any values outside sections are also kept.
    """
    sections: list[dict] = []
    current = None
    outside = None
    for line_number, line in enumerate(text.splitlines(), 1):
        stripped = line.strip().lstrip("\ufeff")
        if not stripped or stripped.startswith(("//", "#", ";")):
            continue
        # Inline comments are not part of a section marker. Entry values below
        # stay raw so colors, strings, and comments are not silently rewritten.
        marker = _MARKER.fullmatch(re.split(r"\s+(?://|#|;)", stripped, maxsplit=1)[0])
        if marker:
            operation, name = marker.groups()
            if operation.lower() == "begin":
                if current is not None:
                    raise OriginalStatsError(f"Nested section at line {line_number}: {name}")
                current = {"name": name, "entries": []}
                sections.append(current)
            else:
                if current is None or current["name"].casefold() != name.casefold():
                    raise OriginalStatsError(f"Unmatched End({name}) at line {line_number}")
                current = None
                outside = None
            continue
        parts = stripped.split(None, 1)
        entry = (parts[0], parts[1].strip() if len(parts) > 1 else "")
        if current is not None:
            current["entries"].append(entry)
        else:
            if outside is None:
                outside = {"name": "Global", "entries": []}
                sections.append(outside)
            outside["entries"].append(entry)
    if current is not None:
        raise OriginalStatsError(f"Missing End({current['name']})")
    return sections


def typed_value(raw: str):
    """Convert native numbers, vectors and boolean flags without losing strings.

    Empty flags are True. Raw entries should be used for display/copying;
    convenience values are for donor defaults and comparisons.
    """
    value = re.split(r"\s+(?://|#|;)", raw.strip(), maxsplit=1)[0].strip()
    if not value:
        return True
    if value.casefold() in ("true", "false"):
        return value.casefold() == "true"
    numbers = []
    for token in value.split():
        if _HEX.fullmatch(token):
            numbers.append(int(token, 16))
        elif _NUMBER.fullmatch(token):
            if not any(char in token.lower() for char in ".e"):
                numbers.append(int(token, 10))
            else:
                number = float(token)
                if not math.isfinite(number):
                    return raw
                numbers.append(number)
        else:
            return raw
    return numbers[0] if len(numbers) == 1 else numbers


def _values(sections: list[dict], section_name: str) -> dict:
    return {key: typed_value(value)
            for section in sections if section["name"].casefold() == section_name.casefold()
            for key, value in section["entries"]}


def _decode_text(payload: bytes) -> str:
    if payload.startswith((b"\xff\xfe", b"\xfe\xff")):
        return payload.decode("utf-16")
    return payload.decode("utf-8") if payload.startswith(b"\xef\xbb\xbf") else payload.decode("cp1252")


# The installer and editor decode tweak bytes exactly as this reader does.
decode_text = _decode_text


def _car_from_text(text: str, alias: str, source: str) -> dict | None:
    sections = parse_sections(text)
    identity_values = [typed_value(value)
                       for section in sections if section["name"].casefold() == "carinfo"
                       for key, value in section["entries"] if key.upper() == "ID"]
    if not identity_values:
        return None
    if len(identity_values) != 1 or type(identity_values[0]) is not int:
        raise OriginalStatsError("Expected one integer ID in CarInfo")
    info_values = {key.upper(): value for key, value in _values(sections, "CarInfo").items()}
    paint_name = info_values.get("PAINTJOBNAME", "")
    fallback = paint_name if isinstance(paint_name, str) and paint_name.strip(" ?") else alias.replace("_", " ")
    return {
        "id": identity_values[0], "alias": alias,
        "display_name": ORIGINAL_NAMES.get(alias.casefold(), fallback),
        "sections": sections,
        "ratings": {key: info_values[key] for key in RATING_KEYS if key in info_values},
        "physics": _values(sections, "CarBody"),
        "raw_text": text, "source": source,
    }


def load_original_cars(game_dir: Path | str) -> list[dict]:
    """Load retail references from the pristine backup, or the installed HD ZIP.

    Each car has id, alias, display_name, sections, ratings, physics, raw_text
    and source. A corrupt existing backup raises a clear error rather than
    quietly substituting modified installed data. Partial fixture archives are
    supported; an archive with no retail cars raises OriginalStatsError.
    """
    game_dir = Path(game_dir)
    backup = game_dir / ".mm3-car-mods" / "Data_hd.zip"
    installed = game_dir / "Data" / "Data_hd.zip"
    archive_path = backup if backup.exists() else installed
    if not archive_path.is_file():
        raise OriginalStatsError(
            f"Original car stats are unavailable. Expected {backup} or {installed}. "
            "Choose an extracted MM3 game folder containing Data/Data_hd.zip.")
    cars = {}
    source_member = ""
    try:
        with zipfile.ZipFile(archive_path, "r") as archive:
            for info in archive.infolist():
                match = _TWEAK.fullmatch(info.filename.replace("\\", "/"))
                if not match:
                    continue
                source_member = info.filename
                # Retail tweaks are ASCII. cp1252 preserves every byte if a
                # localized name occurs in an otherwise compatible archive.
                payload = archive.read(info)
                text = _decode_text(payload)
                car = _car_from_text(text, match[1], f"{archive_path} :: {info.filename}")
                if car is None:
                    continue
                ident = car["id"]
                if ident not in ORIGINAL_CAR_IDS:
                    continue
                if ident in cars:
                    raise OriginalStatsError(f"Duplicate original car ID {ident}")
                cars[ident] = car
    except (OSError, zipfile.BadZipFile, RuntimeError, UnicodeError, ValueError, NotImplementedError, EOFError, zlib.error) as error:
        detail = f" ({source_member})" if source_member else ""
        raise OriginalStatsError(f"Could not read original car stats from {archive_path}{detail}: {error}") from error
    if not cars:
        raise OriginalStatsError(f"No original retail car tweaks were found in {archive_path}.")
    return [cars[ident] for ident in ORIGINAL_CAR_IDS if ident in cars]


def _case_child(folder: Path, name: str) -> Path | None:
    """Xbox paths are case-insensitive; fixture hosts may not be."""
    matches = [child for child in folder.iterdir() if child.name.casefold() == name.casefold()]
    if len(matches) > 1:
        raise OriginalStatsError(f"Ambiguous installed path: {folder / name}")
    return matches[0] if matches else None


def _dlc_names(payload: bytes) -> dict[int, str]:
    """English names are the first nonblank line following each language.dat ID.

    Retail DLC uses UTF-16 with seven language pairs per car. The native names
    here are reliable; PAINTJOBNAME can still refer to the retail donor.
    """
    lines = [line.strip().lstrip("\ufeff") for line in _decode_text(payload).splitlines() if line.strip()]
    return {int(line): lines[index + 1] for index, line in enumerate(lines[:-1])
            if line.isdecimal() and not lines[index + 1].isdecimal()}


def load_dlc_cars(game_dir: Path | str) -> list[dict]:
    """Read installed official DLC cars in ID order, without touching game data.

    Installed packs live in TDATA/4d53002a/$C and have contentmeta.xbx. Tweaks
    can be loose Cars/Tweak members or members of a ZIP in the pack root; loose
    files supersede archived copies within that pack. Identical cars copied to
    another pack are returned once; conflicting IDs raise OriginalStatsError
    rather than choosing a misleading reference. No DLC folder means [].
    """
    source = str(Path(game_dir) / "TDATA/4d53002a/$C")
    cars = {}
    try:
        # Resolve just the installed title path, not user download folders,
        # inactive backups, or archives elsewhere in the repository.
        root = Path(game_dir)
        if not root.is_dir():
            return []
        for component in ("TDATA", "4d53002a", "$C"):
            root = _case_child(root, component)
            if root is None or not root.is_dir():
                return []
        for pack in sorted(root.iterdir(), key=lambda path: path.name.casefold()):
            if not pack.is_dir():
                continue
            marker = _case_child(pack, "contentmeta.xbx")
            if marker is None or not marker.is_file():
                continue
            members = {}
            # Package-root archives support extracted installs that keep
            # their car tweaks archived. Subdirectories are intentionally not
            # scanned for ZIPs, since they can hold backups or source assets.
            for archive_path in sorted(pack.iterdir(), key=lambda path: path.name.casefold()):
                if not archive_path.is_file() or archive_path.suffix.casefold() != ".zip":
                    continue
                source = str(archive_path)
                with zipfile.ZipFile(archive_path, "r") as archive:
                    for info in archive.infolist():
                        member = info.filename.replace("\\", "/")
                        if not (_TWEAK.fullmatch(member) or member.casefold() == "cars/tweak/language.dat"):
                            continue
                        key = member.casefold()
                        payload = archive.read(info)
                        if key in members and members[key][0] != payload:
                            raise OriginalStatsError(f"Conflicting archived DLC member {member} in {pack}")
                        members[key] = (payload, f"{archive_path} :: {info.filename}", member)
            cars_dir = _case_child(pack, "Cars")
            tweak_dir = _case_child(cars_dir, "Tweak") if cars_dir is not None and cars_dir.is_dir() else None
            if tweak_dir is not None and tweak_dir.is_dir():
                for path in sorted(tweak_dir.iterdir(), key=lambda path: path.name.casefold()):
                    if path.is_file() and (path.suffix.casefold() == ".txt" or path.name.casefold() == "language.dat"):
                        source = str(path)
                        member = f"Cars/Tweak/{path.name}"
                        members[member.casefold()] = (path.read_bytes(), str(path), member)
            language = members.get("cars/tweak/language.dat")
            names = _dlc_names(language[0]) if language is not None else {}
            for key, (payload, source, member) in sorted(members.items()):
                match = _TWEAK.fullmatch(member)
                if not match:
                    continue
                car = _car_from_text(_decode_text(payload), match[1], source)
                if car is None or car["id"] not in DLC_CAR_IDS:
                    continue
                ident = car["id"]
                # All official DLC aliases are car<ID>. Ignore retail/model
                # copies with a borrowed DLC ID rather than mislabeling them.
                if car["alias"].casefold() != f"car{ident}":
                    continue
                car["display_name"] = names.get(ident, f"DLC car {ident}")
                previous = cars.get(ident)
                if previous is not None:
                    if previous["sections"] != car["sections"] or previous["display_name"] != car["display_name"]:
                        raise OriginalStatsError(f"Conflicting installed DLC car ID {ident}: {previous['source']} and {source}")
                    continue
                cars[ident] = car
    except (OSError, zipfile.BadZipFile, RuntimeError, UnicodeError, ValueError, NotImplementedError, EOFError, zlib.error) as error:
        raise OriginalStatsError(f"Could not read installed DLC car stats from {source}: {error}") from error
    return [cars[ident] for ident in sorted(cars)]
