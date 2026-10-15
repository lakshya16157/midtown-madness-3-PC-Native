"""Reference numbers for the Car Workshop: per-car values and game-wide extremes.

This module has no Tk dependency. It reads cars in the shape produced by
original_stats (``sections`` plus convenience ``physics``/``ratings``) or plain
``physics``/``ratings`` dictionaries, so the editor, its tests and the docs all
agree on what "highest in the game" means.
"""
from __future__ import annotations

from collections import Counter
import importlib.util
from pathlib import Path

HERE = Path(__file__).resolve().parent
_ORIGINAL_SPEC = importlib.util.spec_from_file_location("mm3_tuning_original_stats", HERE / "original_stats.py")
original_stats = importlib.util.module_from_spec(_ORIGINAL_SPEC)
_ORIGINAL_SPEC.loader.exec_module(original_stats)

RATING_KEYS = original_stats.RATING_KEYS
RATING_MAX = 9
# Manifest speedometer keys and the native CarCamera entries they set.
DIAL_KEYS = {"max_kmh": "SpeedometerMaxKmh", "max_mph": "SpeedometerMaxMph"}
# Highest printed labels in the retail Menu/Gfx/HUD/Speedometer* textures.
# These are artwork markings, not CarCamera calibration or needle travel limits.
# SPEEDOMETERTYPE selects these pairs in the native HUD texture loader.
HUD_DIAL_MARKINGS = {
    0: (300, 150), 1: (300, 200), 2: (160, 100), 3: (220, 180), 4: (380, 280),
}
PEAK_RPM_KEYS = ("MaxTorqueRPM", "MaxTurboTorqueRPM")
# Donor-mode automatic shifting samples the stock torque table, which ends
# near 8500 RPM; prepare.load_config enforces this cap without a factory profile.
AUTOMATIC_PEAK_LIMIT = 8000
DRIVE_NAMES = {0: "Front", 1: "Rear", 2: "All wheels"}
DRIVE_SHORT = {0: "F", 1: "R", 2: "AWD"}


def is_number(value) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def format_number(value) -> str:
    """Compact display: 6500.0 -> 6500, 0.0100 -> 0.01, 1.125 stays 1.125."""
    if isinstance(value, bool) or not is_number(value):
        return str(value)
    if isinstance(value, float):
        if value.is_integer() and abs(value) < 1e15:
            return str(int(value))
        return format(value, ".6g")
    return str(value)


def _entries(section: dict):
    entries = section.get("entries", [])
    return entries.items() if isinstance(entries, dict) else entries


def section_values(car: dict, name: str) -> dict:
    """Typed values of one native section; the last duplicate key wins."""
    values = {}
    for section in car.get("sections") or []:
        if str(section.get("name", "")).casefold() != name.casefold():
            continue
        for key, raw in _entries(section):
            values[str(key)] = original_stats.typed_value(raw) if isinstance(raw, str) else raw
    return values


def native_hud_markings(car: dict | None) -> dict:
    """Printed HUD limits for a native gauge, independent of interior maxima.

    Unknown or unavailable gauge types stay unknown; never substitute camera
    limits, a driving-base car or a guessed conversion between the two units.
    """
    info = {key.upper(): value for key, value in section_values(car or {}, "CarInfo").items()}
    gauge_type = info.get("SPEEDOMETERTYPE")
    if not is_number(gauge_type) or gauge_type not in HUD_DIAL_MARKINGS:
        return {}
    kmh, mph = HUD_DIAL_MARKINGS[gauge_type]
    return {"type": int(gauge_type), "max_kmh": kmh, "max_mph": mph}


def car_values(car: dict | None) -> dict:
    """Return ``{"physics", "ratings", "dial"}`` for any reference or draft car."""
    if not car:
        return {"physics": {}, "ratings": {}, "dial": {}}
    if car.get("sections"):
        physics = section_values(car, "CarBody")
        info = {key.upper(): value for key, value in section_values(car, "CarInfo").items()}
        camera = section_values(car, "CarCamera")
    else:
        physics = dict(car.get("physics") or {})
        info = dict(car.get("ratings") or {})
        camera = {}
    ratings = {key: info[key] for key in RATING_KEYS if key in info}
    dial = {key: camera[native] for key, native in DIAL_KEYS.items() if native in camera}
    return {"physics": physics, "ratings": ratings, "dial": dial}


def car_name(car: dict) -> str:
    return str(car.get("display_name") or car.get("alias") or car.get("id") or "Car")


class Extreme:
    """Highest and lowest stored value of one setting across reference cars.

    A plain class rather than a dataclass: the workshop loads its modules by
    file path without registering them in sys.modules, which dataclasses need.
    """
    def __init__(self) -> None:
        self.high = None
        self.high_names: list = []
        self.low = None
        self.low_names: list = []
        self.present = 0
        self.total = 0
        self.counts: Counter = Counter()

    def add(self, value, name: str) -> None:
        if not is_number(value):
            return
        self.present += 1
        self.counts[value] += 1
        if self.high is None or value > self.high:
            self.high, self.high_names = value, [name]
        elif value == self.high:
            self.high_names.append(name)
        if self.low is None or value < self.low:
            self.low, self.low_names = value, [name]
        elif value == self.low:
            self.low_names.append(name)


def gear_key(index: int) -> str:
    return "gear.R" if index == 0 else f"gear.{index}"


def game_extremes(cars) -> dict[str, Extreme]:
    """Extremes for CarBody keys, gears, gear count, ratings and interior dial.

    Keys: native CarBody names (``Mass``), ``gear.R``/``gear.1``..``gear.7``
    (reverse by magnitude), ``gear.count``, ``rating.TOPSPEED`` and so on, and
    ``dial.max_kmh``/``dial.max_mph``. Names follow the given car order.
    """
    cars = [car for car in (cars or []) if isinstance(car, dict)]
    table: dict[str, Extreme] = {}

    def track(key, value, name):
        table.setdefault(key, Extreme()).add(value, name)

    for car in cars:
        name = car_name(car)
        values = car_values(car)
        for key, value in values["physics"].items():
            if key == "GearRatioVector":
                ratios = value if isinstance(value, list) else [value]
                if all(is_number(ratio) for ratio in ratios) and ratios:
                    for index, ratio in enumerate(ratios):
                        track(gear_key(index), abs(ratio) if index == 0 else ratio, name)
                    track("gear.count", len(ratios) - 1, name)
            else:
                track(key, value, name)
        for key, value in values["ratings"].items():
            track(f"rating.{key}", value, name)
        for key, value in values["dial"].items():
            track(f"dial.{key}", value, name)
    for extreme in table.values():
        extreme.total = len(cars)
    return table


def _shorten(name: str, limit: int = 22) -> str:
    return name if len(name) <= limit else name[:limit - 1].rstrip() + "…"


def name_list(names: list, limit: int = 2) -> str:
    shown = ", ".join(names[:limit])
    extra = len(names) - limit
    return shown + (f" and {extra} more" if extra > 0 else "")


def short_high(extreme: Extreme | None, key: str = "") -> str:
    """Table cell text: '15000 · Paris Bus +2', '6500 · all 49 cars' or '—'."""
    if extreme is None or extreme.high is None:
        return "—"
    if key == "WheelDrive":
        return " · ".join(f"{DRIVE_SHORT.get(value, value)} {count}"
                          for value, count in sorted(extreme.counts.items()))
    value = -extreme.high if key == "gear.R" else extreme.high
    if extreme.total and len(extreme.high_names) == extreme.total:
        return f"{format_number(value)} · all {extreme.total} cars"
    extra = f" +{len(extreme.high_names) - 1}" if len(extreme.high_names) > 1 else ""
    return f"{format_number(value)} · {_shorten(extreme.high_names[0])}{extra}"


def long_range(extreme: Extreme | None, key: str = "") -> str:
    """Guide/tooltip sentence describing the full game range."""
    if extreme is None or extreme.high is None:
        return "No game car stores this value."
    sign = -1 if key == "gear.R" else 1
    if key == "WheelDrive":
        text = "Game cars: " + ", ".join(
            f"{count} {DRIVE_NAMES.get(value, value).lower()}" for value, count in sorted(extreme.counts.items())) + "."
    elif extreme.high == extreme.low:
        text = f"Every game car that stores it uses {format_number(sign * extreme.high)}."
    else:
        text = (f"Across {extreme.total} game cars: lowest {format_number(sign * extreme.low)} "
                f"({name_list(extreme.low_names)}), highest {format_number(sign * extreme.high)} "
                f"({name_list(extreme.high_names)}).")
    missing = extreme.total - extreme.present
    if missing > 0:
        text += f" {missing} {'car leaves' if missing == 1 else 'cars leave'} it at the game default."
    return text


def outside_flag(value, extreme: Extreme | None) -> str:
    """' ▲' above the game's highest value, ' ▼' below its lowest, else ''."""
    if extreme is None or extreme.high is None or not is_number(value):
        return ""
    if value > extreme.high:
        return " ▲"
    if value < extreme.low:
        return " ▼"
    return ""


# Arrow-key step and display format for each editable value.
STEPS = {
    "Mass": (10, "%.0f"), "TorqueScale": (10, "%.0f"), "TorqueScaleFactor": (0.025, "%.3f"),
    "TurboSize": (0.1, "%.2f"), "BrakeTorque": (250, "%.0f"), "HandBrakeTorque": (250, "%.0f"),
    "LatFrictionFactor": (0.05, "%.2f"), "AirKZZ": (0.005, "%.3f"), "AirKZY": (0.05, "%.2f"),
    "MaxTorqueRPM": (100, "%.0f"), "MaxTurboTorqueRPM": (100, "%.0f"), "MaxRPM": (100, "%.0f"),
    "CutRPM": (100, "%.0f"), "MinRPM": (50, "%.0f"), "MinRPMGearChange": (100, "%.0f"),
    "TimeChangeGear": (0.05, "%.2f"), "TimeChangeGearFR": (0.05, "%.2f"), "WheelRatio": (0.1, "%.2f"),
    "GearStatusDownGearFactor": (0.05, "%.2f"), "SteeringEndSpeed": (10, "%.0f"),
    "WheelRadius": (0.005, "%.3f"),
}


def spin_value(text, inherited, direction: int, step: float, low: float, high: float, fmt: str = "%g") -> str:
    """Next arrow-key value: a blank or unreadable field starts from the inherited value."""
    start = float(low)
    for candidate in (text, inherited):
        try:
            start = float(candidate)
            break
        except (TypeError, ValueError):
            continue
    return fmt % min(high, max(low, start + direction * step))


def gear_tokens(text: str) -> list:
    """Ratio tokens from '-3.3, 2.9, 1.8' or '[-3.3 2.9]'; blank means inherit."""
    text = str(text).strip().strip("[]").strip()
    return [token for token in text.replace(",", " ").split()] if text else []


def gear_text(cells) -> str:
    """Join per-gear cells; a blank gap becomes '?' so validation reports it."""
    values = [str(cell).strip() for cell in cells]
    while values and not values[-1]:
        values.pop()
    return ", ".join(value or "?" for value in values)


def allowed_range(limits: dict, key: str, factory: bool = False) -> tuple:
    """Installer limits for an override; peak RPMs are capped in donor mode."""
    low, high = limits[key]
    if key in PEAK_RPM_KEYS and not factory:
        high = min(high, AUTOMATIC_PEAK_LIMIT)
    return low, high


def _body_entries(car: dict) -> list:
    return [(str(key), " ".join(str(value).split()))
            for section in car.get("sections") or []
            if str(section.get("name", "")).casefold() == "carbody"
            for key, value in _entries(section)]


def dlc_twins(originals, dlc) -> dict:
    """Original alias -> names of DLC cars whose driving values are identical.

    Official DLC cars name a retail car in CarInfo PHYSICSID; a DLC car counts
    as a twin only when its whole CarBody matches that car entry for entry.
    """
    by_id = {car.get("id"): car for car in originals or [] if isinstance(car, dict)}
    twins: dict = {}
    for car in dlc or []:
        if not isinstance(car, dict):
            continue
        twin = by_id.get(section_values(car, "CarInfo").get("PHYSICSID"))
        if twin is not None and _body_entries(car) and _body_entries(car) == _body_entries(twin):
            twins.setdefault(twin["alias"], []).append(car_name(car))
    return twins


def native_extremes(cars, section: str, key: str) -> Extreme | None:
    """Extremes of any numeric native setting, for read-only stat browsing."""
    extreme = Extreme()
    total = 0
    for car in cars or []:
        if not isinstance(car, dict):
            continue
        total += 1
        value = section_values(car, section).get(key)
        extreme.add(value, car_name(car))
    extreme.total = total
    return extreme if extreme.present else None
