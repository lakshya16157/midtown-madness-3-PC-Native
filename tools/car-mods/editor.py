#!/usr/bin/env python3
"""Desktop car workshop with editable extras and read-only retail references."""

from __future__ import annotations

import argparse
import base64
import copy
import csv
import importlib.util
import io
import json
import math
import os
from pathlib import Path
import queue
import re
import subprocess
import sys
import tempfile
import threading
import tkinter as tk
import webbrowser
from tkinter import colorchooser, filedialog, messagebox, ttk

HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("mm3_car_prepare", HERE / "prepare.py")
prepare = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(prepare)
FACTORY_SPEC = importlib.util.spec_from_file_location("mm3_factory_specs", HERE / "factory_specs.py")
factory = importlib.util.module_from_spec(FACTORY_SPEC)
FACTORY_SPEC.loader.exec_module(factory)
DEFAULT_SPEC = importlib.util.spec_from_file_location("mm3_driving_defaults", HERE / "driving_defaults.py")
defaults = importlib.util.module_from_spec(DEFAULT_SPEC)
DEFAULT_SPEC.loader.exec_module(defaults)
ORIGINAL_SPEC = importlib.util.spec_from_file_location("mm3_original_stats", HERE / "original_stats.py")
original_stats = importlib.util.module_from_spec(ORIGINAL_SPEC)
ORIGINAL_SPEC.loader.exec_module(original_stats)
HELP_SPEC = importlib.util.spec_from_file_location("mm3_field_help", HERE / "field_help.py")
field_help = importlib.util.module_from_spec(HELP_SPEC)
HELP_SPEC.loader.exec_module(field_help)
BROWSER_SPEC = importlib.util.spec_from_file_location("mm3_stats_browser", HERE / "stats_browser.py")
stats_browser = importlib.util.module_from_spec(BROWSER_SPEC)
BROWSER_SPEC.loader.exec_module(stats_browser)
WIDGETS_SPEC = importlib.util.spec_from_file_location("mm3_workshop_widgets", HERE / "workshop_widgets.py")
widgets = importlib.util.module_from_spec(WIDGETS_SPEC)
WIDGETS_SPEC.loader.exec_module(widgets)
REFERENCE_SPEC = importlib.util.spec_from_file_location("mm3_tuning_reference", HERE / "tuning_reference.py")
reference = importlib.util.module_from_spec(REFERENCE_SPEC)
REFERENCE_SPEC.loader.exec_module(reference)
HUD_SPEC = importlib.util.spec_from_file_location("mm3_hud_preview", HERE / "hud_preview.py")
hud_preview = importlib.util.module_from_spec(HUD_SPEC)
HUD_SPEC.loader.exec_module(hud_preview)
# Prepared model packages made by tools/car-models, offered on the Appearance tab.
PACKAGES_DIR = prepare.ROOT / "out" / "car-models" / "packages"

RATING_ORDER = ("TOPSPEED", "ACCELERATION", "MASS", "HANDLING", "DURABILITY")
DRIVING = ("TorqueScale", "Mass", "BrakeTorque", "LatFrictionFactor", "AirKZZ", "WheelDrive")
TRANSMISSION = ("WheelRatio", "TimeChangeGear", "TimeChangeGearFR", "MinRPM", "MinRPMGearChange", "CutRPM", "MaxRPM", "MaxTorqueRPM", "MaxTurboTorqueRPM")
LABELS = {
    "TorqueScale": "Engine torque scale", "Mass": "Mass (kg)", "BrakeTorque": "Braking strength",
    "LatFrictionFactor": "Cornering grip", "AirKZZ": "Air drag", "WheelDrive": "Driven wheels",
    "WheelRatio": "Final drive", "TimeChangeGear": "Shift time (seconds)",
    "TimeChangeGearFR": "Reverse shift time", "MinRPM": "Idle RPM",
    "MinRPMGearChange": "Minimum shift RPM", "CutRPM": "Cutoff RPM", "MaxRPM": "Redline RPM",
    "MaxTorqueRPM": "Peak torque RPM", "MaxTurboTorqueRPM": "Turbo peak torque RPM",
    "TorqueScaleFactor": "Torque multiplier", "HandBrakeTorque": "Handbrake strength",
    "AirKZY": "Downforce scale", "TurboSize": "Donor turbo boost", "GearStatusDownGearFactor": "Downshift factor",
    "SteeringEndSpeed": "Steering end speed", "WheelRadius": "Wheel radius",
}
DRIVE_OPTIONS = ("", "0 - Front", "1 - Rear", "2 - All wheels")
LABELS.update(field_help.LABELS)


def display_value(value) -> str:
    if isinstance(value, (list, tuple)):
        return ", ".join(display_value(item) for item in value)
    if isinstance(value, float):
        return format(value, ".9g")
    return str(value)


def effective_stats(car: dict, donor: dict, base: dict | None = None, hud: dict | None = None) -> dict:
    """Use the installer's exact overlay rules for the read-only stat preview.

    ``base`` is the loaded original car named by the car's driving_base. A car
    with a base never falls back to donor values silently: without the base
    car's data the preview raises ValueError.
    """
    text = donor["raw_text"].replace("\r\r\n", "\n").replace("\r\n", "\n")
    base_text = None
    if "driving_base" in car:
        if base is None:
            raise ValueError(f"Base stats car {car['driving_base']} is not available in the loaded game data")
        base_text = base["raw_text"]
    hud_text = None
    if "hud_style" in car:
        if hud is None:
            raise ValueError(f"HUD style car {car['hud_style']} is not available in the loaded game data")
        hud_text = hud["raw_text"]
    text = prepare.tweak_variant(text.encode("ascii"), car, base_text, hud_text).decode("ascii")
    source = f"Draft settings + {donor['alias']} MM3 donor. Geometry may change wheel radius."
    if base_text is not None:
        source = (f"Draft settings + {base['display_name']} base stats + {donor['alias']} MM3 donor. "
                  "Geometry may change wheel radius.")
    return {"id": car["id"], "alias": car["alias"], "display_name": car["display_name"],
            "sections": original_stats.parse_sections(text), "raw_text": text, "source": source}


# Moved to workshop_widgets.py; these names stay for existing callers and tests.
ToolTip = widgets.ToolTip
ScrollableFrame = widgets.ScrollableFrame


def validated(config: dict) -> dict:
    """Use exactly the same validation as the installer, without saving edits.

    The JSON round trip gives an independent copy with the same types a saved
    and reloaded manifest has; nothing is written to disk.
    """
    return prepare.validate_config(json.loads(json.dumps(config)))


def number(text: str, label: str, integer: bool = False) -> int | float:
    try:
        result = float(text)
    except ValueError as error:
        raise ValueError(f"{label} must be a number") from error
    if not math.isfinite(result) or (integer and result != int(result)):
        raise ValueError(f"{label} must be a finite {'integer' if integer else 'number'}")
    return int(result) if integer or result.is_integer() else result


def edited_car(car: dict, ratings: dict[str, str], physics: dict[str, str],
               gears: str, colors: list[str], speedometer: dict[str, str],
               model_package: str | None = None, factory_engine: bool | None = None,
               driving_base: str | None = None, hud_style: str | None = None) -> dict:
    """Translate the form into config data; blank physics fields inherit the base.

    ``driving_base``: None keeps the car's current base stats car, "" removes
    it (the MM3 donor supplies the values) and any other text selects it.
    """
    result = copy.deepcopy(car)
    if factory_engine is not None:
        result.pop("engine_profile", None)
        if factory_engine:
            result["engine_profile"] = "factory"
    if driving_base is not None:
        result.pop("driving_base", None)
        if driving_base.strip():
            result["driving_base"] = driving_base.strip()
    if hud_style is not None:
        result.pop("hud_style", None)
        if hud_style.strip():
            result["hud_style"] = hud_style.strip()
    result["ratings"] = {key: number(ratings[key], key.title(), True) for key in RATING_ORDER}
    result["physics"] = {}
    for key, text in physics.items():
        if text.strip():
            result["physics"][key] = number(text.split(" - ", 1)[0].strip(), LABELS.get(key, key), key == "WheelDrive")
    if gears.strip():
        tokens = re.split(r"[\s,]+", gears.strip().strip("[]").strip())
        result["physics"]["GearRatioVector"] = [number(token, "Gear ratio") for token in tokens]
    result["colors"] = [color.strip().lower() for color in colors]
    result.pop("speedometer", None)
    limits = edited_speedometer(speedometer)
    if limits is not None:
        result["speedometer"] = limits
    if model_package is not None:
        result.pop("model_package", None)
        if model_package.strip():
            result["model_package"] = model_package.strip()
    return result


def edited_speedometer(fields: dict[str, str]) -> dict | None:
    if not any(text.strip() for text in fields.values()):
        return None
    if not all(text.strip() for text in fields.values()):
        raise ValueError("Enter both speedometer limits, or leave both blank to use the original range")
    limits = {key: number(text, "Speedometer limit") for key, text in fields.items()}
    prepare.validate_speedometer(limits)
    return limits


class EditorModel:
    def __init__(self, path: Path):
        self.path = path.resolve()
        self.reload()

    def reload(self) -> None:
        self.config = prepare.load_config(self.path)
        self.baseline = self.path.read_bytes()
        self.saved = copy.deepcopy(self.config)

    def replace_car(self, index: int, car: dict) -> None:
        candidate = copy.deepcopy(self.config)
        candidate["cars"][index] = car
        self.config = validated(candidate)

    def restore_default_driving(self, index: int) -> None:
        self.replace_car(index, defaults.apply_preset(self.config["cars"][index]))

    def replace_original_speedometers(self, settings: dict) -> None:
        candidate = copy.deepcopy(self.config)
        if settings:
            candidate["original_speedometers"] = copy.deepcopy(settings)
        else:
            candidate.pop("original_speedometers", None)
        self.config = validated(candidate)

    def save(self) -> None:
        config = validated(self.config)
        if self.path.read_bytes() != self.baseline:
            raise ValueError(f"{self.path.name} changed outside the editor. Reload it before saving to keep those changes.")
        payload = (json.dumps(config, indent=2) + "\n").encode("utf-8")
        prepare.atomic_write(self.path, payload)
        self.baseline = payload
        self.saved = copy.deepcopy(config)

    def apply_command(self, game_dir: Path) -> list[str]:
        # pythonw has no console streams; use its adjacent console interpreter.
        executable = Path(sys.executable)
        if executable.name.lower() == "pythonw.exe":
            executable = executable.with_name("python.exe")
        return [str(executable), str(HERE / "prepare.py"), "--config", str(self.path),
                "--game-dir", str(game_dir.resolve()), "--apply"]

    def apply_snapshot_command(self, game_dir: Path) -> tuple[list[str], Path]:
        """Freeze the saved settings for one apply, preserving relative models.

        The installer runs later in another process. An external edit to the
        live manifest must not change the settings this window just saved.
        The caller removes the snapshot after the installer exits.
        """
        descriptor, name = tempfile.mkstemp(prefix=".mm3-workshop-apply-", suffix=".json", dir=self.path.parent)
        snapshot = Path(name)
        try:
            with os.fdopen(descriptor, "wb") as stream:
                stream.write(self.baseline)
            command = self.apply_command(game_dir)
            command[3] = str(snapshot)
            return command, snapshot
        except BaseException:
            snapshot.unlink(missing_ok=True)
            raise


def game_is_running() -> bool:
    if sys.platform != "win32":
        return False
    result = subprocess.run(["tasklist", "/FI", "IMAGENAME eq mm3_recomp.exe", "/FO", "CSV", "/NH"],
                            capture_output=True, text=True, check=True,
                            creationflags=subprocess.CREATE_NO_WINDOW, timeout=10)
    return any(row and row[0].lower() == "mm3_recomp.exe" for row in csv.reader(io.StringIO(result.stdout)))


def last_applied_settings(game_dir: Path) -> dict | None:
    """Read the installer's recorded settings without claiming file integrity."""
    try:
        state = json.loads((game_dir / prepare.STATE_DIR / "state.json").read_text(encoding="utf-8"))
        config = state.get("config")
        return config if isinstance(config, dict) else None
    except (OSError, ValueError, AttributeError):
        return None


class OriginalSpeedometerDialog:
    """Edit retail-car HUD ranges in memory; the main window saves/applies them."""
    def __init__(self, parent: tk.Tk, model: EditorModel, changed, show: bool = True) -> None:
        self.model, self.changed = model, changed
        self.settings = copy.deepcopy(model.config.get("original_speedometers", {}))
        self.aliases = sorted(prepare.ORIGINAL_SPEEDOMETERS, key=lambda alias: prepare.ORIGINAL_SPEEDOMETERS[alias])
        self.current = "Koenigsegg"
        self.window = tk.Toplevel(parent)
        self.window.withdraw()
        self.window.title("Original car speedometers")
        self.window.resizable(False, False)
        self.window.transient(parent)
        parent.update_idletasks()
        width, height = 455, 345
        x = parent.winfo_rootx() + (parent.winfo_width() - width) // 2
        y = parent.winfo_rooty() + (parent.winfo_height() - height) // 2
        x = min(max(0, x), max(0, parent.winfo_screenwidth() - width))
        y = min(max(0, y), max(0, parent.winfo_screenheight() - height - 40))
        self.window.geometry(f"{width}x{height}+{x}+{y}")
        frame = ttk.Frame(self.window, padding=20)
        frame.pack(fill="both", expand=True)
        ttk.Label(frame, text="Original car speedometers", font=("Segoe UI", 15, "bold")).pack(anchor="w")
        ttk.Label(frame, text="Set the on-screen dial maximum for each original sports car.",
                  style="Sub.TLabel", wraplength=410).pack(anchor="w", pady=(5, 12))
        self.selector = ttk.Combobox(frame, state="readonly", width=36,
                                    values=[prepare.ORIGINAL_SPEEDOMETERS[alias] for alias in self.aliases])
        self.selector.pack(fill="x", pady=(0, 12))
        self.selector.bind("<<ComboboxSelected>>", self.select)
        self.speed_vars = {key: tk.StringVar() for key in prepare.SPEEDOMETER}
        for key, unit in (("max_kmh", "km/h"), ("max_mph", "mph")):
            row = ttk.Frame(frame)
            row.pack(fill="x", pady=4)
            low, high = prepare.SPEEDOMETER[key]
            ttk.Label(row, text=f"Maximum {unit}", width=16).pack(side="left")
            field = ttk.Spinbox(row, from_=low, to=high, increment=10, textvariable=self.speed_vars[key], width=10)
            field.pack(side="left")
            ToolTip(field, f"{field_help.OTHER_HELP[key]} Allowed range: {low} to {high}.")
            ttk.Label(row, text=f"{low}–{high}", style="Sub.TLabel").pack(side="left", padx=12)
        ttk.Label(frame, text="Both blank = original HUD range. This changes the dial scale, not the car's speed or physics.\nUse Save & Apply in the main window after keeping these changes.",
                  style="Sub.TLabel", wraplength=410).pack(anchor="w", pady=(9, 12))
        buttons = ttk.Frame(frame)
        buttons.pack(fill="x", side="bottom")
        ttk.Button(buttons, text="Keep changes", command=self.keep).pack(side="left")
        ttk.Button(buttons, text="Cancel", command=self.window.destroy).pack(side="right")
        self.load(self.current)
        if show:
            self.window.deiconify()
            self.window.grab_set()

    def load(self, alias: str) -> None:
        self.current = alias
        self.selector.current(self.aliases.index(alias))
        for key, variable in self.speed_vars.items():
            variable.set(str(self.settings.get(alias, {}).get(key, "")))

    def collect(self) -> None:
        limits = edited_speedometer({key: variable.get() for key, variable in self.speed_vars.items()})
        if limits is None:
            self.settings.pop(self.current, None)
        else:
            self.settings[self.current] = limits

    def select(self, _event=None) -> None:
        alias = self.aliases[self.selector.current()]
        try:
            self.collect()
        except ValueError as error:
            self.selector.current(self.aliases.index(self.current))
            messagebox.showerror("Check speedometer settings", str(error), parent=self.window)
            return
        self.load(alias)

    def keep(self) -> None:
        try:
            self.collect()
            self.model.replace_original_speedometers(self.settings)
        except (ValueError, OSError) as error:
            messagebox.showerror("Check speedometer settings", str(error), parent=self.window)
            return
        self.changed()
        self.window.destroy()


class CarEditor:
    def __init__(self, root: tk.Tk, config: Path | None = None, game_dir: Path = prepare.ROOT / "game_files"):
        self.root, self.game_dir = root, game_dir
        self.model = EditorModel(config if config is not None else prepare.default_config_path())
        self.applied_settings = last_applied_settings(game_dir)
        self.applied_in_session = False
        self.current = 0
        self.original_current = None
        self.original_error = ""
        try:
            self.original_cars = original_stats.load_original_cars(game_dir)
        except (ValueError, OSError) as error:
            self.original_cars = []
            self.original_error = str(error)
        self.retail_count = len(self.original_cars)
        try:
            self.dlc_cars = original_stats.load_dlc_cars(game_dir)
        except (ValueError, OSError) as error:
            self.dlc_cars = []
            self.original_error = "\n".join(filter(None, (self.original_error, str(error))))
        self.original_cars.extend(self.dlc_cars)
        self.original_by_alias = {car["alias"]: car for car in self.original_cars}
        # Highest/lowest stored values across the original and DLC cars.
        self.extremes = reference.game_extremes(self.original_cars)
        self._native_ranges = {}
        # Base stats cars are retail originals, matched in any letter case. DLC
        # cars that drive identically are named beside them in the picker.
        self.original_by_casefold = {car["alias"].casefold(): car for car in self.original_cars[:self.retail_count]}
        self.twins = reference.dlc_twins(self.original_cars[:self.retail_count], self.dlc_cars)
        self.base_var = tk.StringVar()
        self.base_choices: dict[str, str] = {}
        self._loaded_base = None
        self.hud_var = tk.StringVar()
        self.hud_choices: dict[str, str] = {}
        self._loaded_hud = None
        self.hud_var.trace_add("write", self._hud_changed)
        self.loading = False
        self.applying = False
        self.events = queue.Queue()
        self.rating_vars, self.physics_vars = {}, {}
        self.physics_fields = {}
        self.inherited_labels = {}
        self.setting_rows, self.rating_rows, self.tables = {}, {}, []
        self.scroll_forms = {}
        self.base_values = reference.car_values(None)
        self.compare_values = reference.car_values(None)
        self.compare_var = tk.StringVar()
        self._compare_pending = False
        # Live validation and unsaved-change tracking.
        self.problems: dict[str, str] = {}
        self._validation_job = None
        self.gear_problem = tk.StringVar()
        self.dial_problem = tk.StringVar()
        self.hud_photos: dict = {}
        self.rpm_line = tk.StringVar()
        self.placeholders: dict = {}
        self.rating_bars: dict = {}
        self.dirty_cars: set[int] = set()
        self.tab_titles: dict[str, str] = {}
        self.search_var = tk.StringVar()
        self.search_var.trace_add("write", self._filter_catalog)
        self.setting_help = tk.StringVar(value="Select a value or hover over its label for a tuning tip.")
        self.gear_var = tk.StringVar()
        self.gear_var.trace_add("write", self._form_changed)
        self.factory_engine = tk.BooleanVar()
        self.factory_engine.trace_add("write", self._factory_changed)
        self.factory_specs = factory.load_specs()
        self.speed_vars = {key: tk.StringVar() for key in ("max_kmh", "max_mph")}
        self.model_var = tk.StringVar()
        self.model_status = tk.StringVar()
        self.model_var.trace_add("write", self._model_changed)
        self.color_vars = []
        self.preview_color = 0
        self.status = tk.StringVar(value="Choose a car, edit its settings, then save. Apply with the game closed.")
        self.install_status = tk.StringVar()
        root.title(f"MM3 Car Workshop — {self.model.path.name}")
        icon = prepare.ROOT / "src" / "mm3.ico"
        if icon.is_file():
            try:
                root.iconbitmap(str(icon))
            except tk.TclError:
                pass
        screen_width, screen_height = root.winfo_screenwidth(), root.winfo_screenheight()
        width, height = min(1180, max(720, screen_width - 80)), min(850, max(480, screen_height - 120))
        x, y = max(0, (screen_width - width) // 2), max(0, (screen_height - height) // 2 - 25)
        root.geometry(f"{width}x{height}+{x}+{y}")
        # Narrowest width that shows every comparison column without clipping.
        root.minsize(min(1140, width), min(680, height))
        root.configure(background="#f3f5f8")
        root.option_add("*Font", "{Segoe UI} 10")
        style = ttk.Style(root)
        style.theme_use("clam")
        style.configure("TFrame", background="#f3f5f8")
        style.configure("TLabel", background="#f3f5f8")
        style.configure("Title.TLabel", font=("Segoe UI", 19, "bold"))
        style.configure("Sub.TLabel", foreground="#526174")
        style.configure("Hint.TLabel", foreground="#526174", font=("Segoe UI", 9))
        style.configure("Accent.TButton", background="#2066ba", foreground="white", padding=(14, 8))
        style.map("Accent.TButton", background=[("active", "#174f94"), ("disabled", "#cbd5e1")])
        style.configure("Treeview", rowheight=29, font=("Segoe UI", 10))
        style.configure("Treeview.Heading", font=("Segoe UI", 10, "bold"), padding=(8, 6))
        style.configure("TNotebook.Tab", padding=(12, 9))
        style.map("TNotebook.Tab", background=[("selected", "white"), ("!selected", "#e7ecf2")])
        style.configure("Catalog.TNotebook.Tab", padding=(5, 7), font=("Segoe UI", 9))
        style.configure("TLabelframe", background="#f3f5f8")
        style.configure("TLabelframe.Label", background="#f3f5f8", foreground="#28384b", font=("Segoe UI", 10, "bold"))
        style.configure("TButton", padding=(10, 7))
        style.configure("Small.TButton", padding=(6, 2))
        style.configure("Section.TLabel", font=("Segoe UI", 12, "bold"), foreground="#1d2b3c")
        style.configure("Column.TLabel", font=("Segoe UI", 9, "bold"), foreground="#526174")
        style.configure("Setting.TLabel", foreground="#1d2b3c")
        style.configure("Overridden.TLabel", foreground="#2066ba", font=("Segoe UI", 10, "bold"))
        style.configure("Value.TLabel", foreground="#28384b")
        style.configure("Compare.TLabel", foreground="#a1500f")
        # Long car names fit in the dropdown without widening the field.
        style.configure("Wide.TCombobox", postoffset=(0, 0, 170, 0))
        style.configure("Badge.TLabel", foreground="#b45309", font=("Segoe UI", 10, "bold"))
        style.configure("Applied.TLabel", foreground="#237146", font=("Segoe UI", 9, "bold"))
        style.configure("Problem.TLabel", foreground="#b42318")
        # Invalid values turn their field red; keep the theme's other states.
        for widget_style in ("TEntry", "TSpinbox", "TCombobox"):
            for option, color in (("fieldbackground", "#fdecec"), ("bordercolor", "#d64545")):
                current = [tuple(item) for item in style.map(widget_style, option)]
                style.map(widget_style, **{option: [("invalid", color), *current]})
        self._build()
        self._load_car(0)
        root.protocol("WM_DELETE_WINDOW", self.close)
        root.after(100, self._poll_apply)

    def _build(self) -> None:
        outer = ttk.Frame(self.root, padding=(14, 12))
        outer.pack(fill="both", expand=True)
        footer = ttk.Frame(outer)
        footer.pack(side="bottom", fill="x")
        body = ttk.Frame(outer)
        body.pack(fill="both", expand=True)
        catalog = ttk.Frame(body, width=270)
        catalog.pack(side="left", fill="y", padx=(0, 14))
        catalog.pack_propagate(False)
        ttk.Label(catalog, text="Find a car", font=("Segoe UI", 11, "bold")).pack(anchor="w", pady=(0, 6))
        search = ttk.Entry(catalog, textvariable=self.search_var)
        search.pack(fill="x", pady=(0, 10))
        ToolTip(search, "Search car names, aliases or IDs in all three lists. Clear the search to show every car.", self.setting_help.set)
        self.catalog_tabs = ttk.Notebook(catalog, style="Catalog.TNotebook")
        self.catalog_tabs.pack(fill="both", expand=True)
        added_list = ttk.Frame(self.catalog_tabs)
        original_list = ttk.Frame(self.catalog_tabs)
        dlc_list = ttk.Frame(self.catalog_tabs)
        self.catalog_tabs.add(added_list, text=f"Added ({len(self.model.config['cars'])})")
        self.catalog_tabs.add(original_list, text=f"Original ({self.retail_count})")
        self.catalog_tabs.add(dlc_list, text=f"DLC ({len(self.dlc_cars)})")
        options = dict(exportselection=False, borderwidth=0, highlightthickness=0, background="white",
                       selectbackground="#2066ba", selectforeground="white", activestyle="none", font=("Segoe UI", 10))
        self.listbox = tk.Listbox(added_list, **options)
        self.original_listbox = tk.Listbox(original_list, **options)
        self.dlc_listbox = tk.Listbox(dlc_list, **options)
        for frame, listing in ((added_list, self.listbox), (original_list, self.original_listbox), (dlc_list, self.dlc_listbox)):
            scroll = ttk.Scrollbar(frame, orient="vertical", command=listing.yview)
            scroll.pack(side="right", fill="y")
            listing.configure(yscrollcommand=scroll.set)
            listing.pack(side="left", fill="both", expand=True)
        self.listbox.bind("<<ListboxSelect>>", self.select_car)
        self.original_listbox.bind("<<ListboxSelect>>", self.select_original)
        self.dlc_listbox.bind("<<ListboxSelect>>", self.select_dlc)
        ttk.Label(catalog, text="Added: editable settings.\nOriginal & DLC: full stat reference.",
                  style="Sub.TLabel", wraplength=220).pack(anchor="w", pady=(12, 6))
        self.catalog_notice = ttk.Label(catalog, text=self.original_error or "Blank values use each car's Base stats car. "
                                        "A dot (•) marks unsaved changes.",
                                       style="Hint.TLabel", wraplength=220)
        self.catalog_notice.pack(anchor="w")
        self._filter_catalog()
        main = ttk.Frame(body)
        main.pack(side="left", fill="both", expand=True)
        title_row = ttk.Frame(main)
        title_row.pack(fill="x")
        self.name_label = ttk.Label(title_row, font=("Segoe UI", 17, "bold"))
        self.name_label.pack(side="left", anchor="w")
        self.unsaved_badge = ttk.Label(title_row, text="●  Unsaved changes", style="Badge.TLabel",
                                       font=("Segoe UI", 10, "bold"))
        ToolTip(self.unsaved_badge, "This car has changes that are not saved yet. Ctrl+S saves every car.")
        self.identity_label = ttk.Label(main, style="Sub.TLabel", wraplength=800, justify="left")
        self.identity_label.pack(anchor="w", fill="x", pady=(2, 8))
        self.identity_label.bind("<Configure>", lambda e: self.identity_label.configure(wraplength=max(300, e.width - 10)))
        ToolTip(self.identity_label, "The MM3 donor supplies the interior, sound, collision and native setup. "
                                    "A model package can replace exterior body, wheels and textures. "
                                    "Exterior shows your current selection; Save & Apply installs it.")
        pickers = ttk.Frame(main)
        pickers.pack(fill="x", pady=(0, 4))
        self.base_label = ttk.Label(pickers, text="Base stats")
        self.base_label.grid(row=0, column=0, sticky="w")
        self.base_combo = ttk.Combobox(pickers, textvariable=self.base_var, state="readonly",
                                       width=32, height=20, style="Wide.TCombobox")
        self.base_combo.grid(row=0, column=1, sticky="w", padx=(8, 18))
        widgets.block_wheel(self.base_combo)
        ToolTip(self.base_label, field_help.OTHER_HELP["driving_base"])
        ToolTip(self.base_combo, lambda: field_help.OTHER_HELP["driving_base"] + "\n" + self._base_details_text())
        compare_label = ttk.Label(pickers, text="Compare with")
        compare_label.grid(row=0, column=2, sticky="w")
        self.compare_combo = ttk.Combobox(pickers, textvariable=self.compare_var, state="readonly",
                                          width=26, height=20, style="Wide.TCombobox")
        self.compare_combo.grid(row=0, column=3, sticky="w", padx=(8, 0))
        widgets.block_wheel(self.compare_combo)
        compare_tip = ("Pick any added, original or DLC car. Its values fill the Compare column on every "
                       "tab and the Game stats table. Click a Compare value to copy it into this car.")
        ToolTip(compare_label, compare_tip)
        ToolTip(self.compare_combo, compare_tip)
        # Packed below the picker row so its length never spreads the pickers apart.
        self.picker_hint = ttk.Label(main, style="Hint.TLabel", wraplength=800, justify="left")
        self.picker_hint.pack(anchor="w", fill="x", pady=(0, 8))
        self.picker_hint.bind("<Configure>", lambda e: self.picker_hint.configure(wraplength=max(300, e.width - 10)))
        self.base_var.trace_add("write", self._base_changed)
        self.notebook = ttk.Notebook(main)
        self.notebook.pack(fill="both", expand=True)
        self.tabs = {}
        for key, title in (("stats", "Game stats"), ("specs", "Car specs"), ("driving", "Driving"),
                           ("gears", "Gears & RPM"), ("advanced", "More settings"), ("model", "Appearance")):
            tab = ttk.Frame(self.notebook)
            self.tabs[key] = tab
            self.tab_titles[key] = title
            self.notebook.add(tab, text=title)
        self.notebook.enable_traversal()  # Ctrl+Tab and Ctrl+Shift+Tab switch tabs.
        # The header picker is the single compare control; Game stats shares it.
        self.stats_view = stats_browser.StatsBrowser(
            self.tabs["stats"], self.original_cars, field_help.tip_for, show_heading=False, padding=14,
            reference_var=self.compare_var, show_reference_picker=False, range_for=self._range_for)
        self.stats_view.pack(fill="both", expand=True)
        self.compare_combo.configure(values=self.stats_view.reference_values())
        self.compare_var.trace_add("write", self._compare_changed)
        self.notebook.bind("<<NotebookTabChanged>>", self._tab_changed)
        specs = ttk.Frame(self.tabs["specs"], padding=18)
        specs.pack(fill="both", expand=True)
        self.spec_title = ttk.Label(specs, font=("Segoe UI", 12, "bold"), wraplength=700)
        self.spec_title.pack(anchor="w", pady=(0, 10))
        spec_body = ttk.Frame(specs)
        spec_body.pack(fill="both", expand=True)
        self.spec_text = tk.Text(spec_body, wrap="word", height=16, borderwidth=0,
                                 background="#f3f5f8", foreground="#28384b", font=("Segoe UI", 10), cursor="arrow")
        spec_scroll = ttk.Scrollbar(spec_body, orient="vertical", command=self.spec_text.yview)
        spec_scroll.pack(side="right", fill="y")
        self.spec_text.configure(yscrollcommand=spec_scroll.set)
        self.spec_text.pack(side="left", fill="both", expand=True)
        self.spec_text.tag_configure("source", foreground="#2066ba", underline=True)
        ttk.Label(specs, text="Restore shipped tuning on the Driving tab returns this car to its shipped weight, "
                              "power and MM3 donor driving values.", style="Hint.TLabel", wraplength=700).pack(anchor="w", pady=(10, 0))
        for key in ("driving", "gears", "advanced", "model"):
            scroll_form = ScrollableFrame(self.tabs[key])
            scroll_form.pack(fill="both", expand=True)
            self.scroll_forms[key] = scroll_form
            self.tabs[key + "_form"] = scroll_form.content
        driving, transmission = self.tabs["driving_form"], self.tabs["gears_form"]
        advanced, models = self.tabs["advanced_form"], self.tabs["model_form"]
        table_hint = ("Blank values use the Base column (shown in grey in the box). Max allowed is the highest "
                      "value the installer accepts; Game max is the highest among the original and DLC cars. "
                      "Hover over any setting for details; the arrow keys step from the inherited value.")
        toolbar = ttk.Frame(driving)
        toolbar.pack(fill="x", pady=(0, 10))
        self.override_summary = ttk.Label(toolbar, style="Sub.TLabel")
        self.override_summary.pack(side="left")
        self.restore_button = ttk.Button(toolbar, text="Restore shipped tuning", command=self.restore_default_driving)
        self.restore_button.pack(side="right")
        ToolTip(self.restore_button, "Returns this car to its shipped weight and power on its MM3 donor's driving "
                                     "values. Keeps your model, paint, menu ratings and dial limits.")
        self.clear_button = ttk.Button(toolbar, text="Clear all overrides", command=self.clear_overrides)
        self.clear_button.pack(side="right", padx=(0, 8))
        ToolTip(self.clear_button, "Blanks every physics value and the gearbox so the car uses its Base stats car. "
                                   "Also turns off the factory engine profile. Keeps ratings, paint and model.")
        self._section_heading(driving, "Driving physics", table_hint)
        self._physics_table(driving, DRIVING).pack(anchor="w", fill="x")
        ttk.Separator(driving).pack(fill="x", pady=16)
        ratings_header = ttk.Frame(driving)
        ratings_header.pack(fill="x")
        ttk.Label(ratings_header, text="Menu ratings", style="Section.TLabel",
                  font=("Segoe UI", 12, "bold")).pack(side="left")
        self.copy_ratings_button = ttk.Button(ratings_header, text="Copy base ratings", style="Small.TButton",
                                              command=self.copy_base_ratings)
        self.copy_ratings_button.pack(side="right")
        ttk.Label(driving, text="The 0 to 9 bars in the vehicle menu. They describe the car; they do not change how "
                                "it drives. Click the bar to set a rating; ticks mark the Base and Compare cars.",
                  style="Hint.TLabel", wraplength=760, justify="left").pack(anchor="w", pady=(2, 8))
        self._ratings_table(driving).pack(anchor="w", fill="x")
        self._section_heading(transmission, "Engine and shifting", table_hint)
        self._physics_table(transmission, TRANSMISSION).pack(anchor="w", fill="x")
        self.rpm_label = ttk.Label(transmission, textvariable=self.rpm_line, style="Sub.TLabel",
                                   wraplength=760, justify="left")
        self.rpm_label.pack(anchor="w", pady=(8, 0))
        ttk.Separator(transmission).pack(fill="x", pady=16)
        gear_tip = field_help.PHYSICS_HELP["GearRatioVector"]
        self._section_heading(transmission, "Gear ratios", gear_tip)
        self.gear_grid = widgets.GearGrid(transmission, self.gear_var, reference.gear_tokens, reference.gear_text)
        self.gear_grid.pack(anchor="w")
        for entry in self.gear_grid.entries:
            self._help(entry, entry, gear_tip)
        text_row = ttk.Frame(transmission)
        text_row.pack(fill="x", pady=(10, 4))
        ttk.Label(text_row, text="Ratios as text").pack(side="left")
        gear_entry = ttk.Entry(text_row, textvariable=self.gear_var)
        gear_entry.pack(side="left", fill="x", expand=True, padx=(8, 0))
        self._help(gear_entry, gear_entry, gear_tip + " Paste or type all ratios here, separated by commas.")
        ttk.Label(transmission, textvariable=self.gear_problem, style="Problem.TLabel",
                  wraplength=760, justify="left").pack(anchor="w")
        self.gear_donor_label = ttk.Label(transmission, style="Value.TLabel", wraplength=760, justify="left")
        self.gear_donor_label.pack(anchor="w")
        self.gear_compare_label = ttk.Label(transmission, style="Compare.TLabel", wraplength=760, justify="left", cursor="hand2")
        self.gear_compare_label.pack(anchor="w", pady=(2, 0))
        self.gear_compare_label.bind("<Button-1>", lambda _event: self.use_compare_gears())
        self.gear_limits_label = ttk.Label(transmission, text=self._gear_limits_text(), style="Hint.TLabel",
                                           wraplength=760, justify="left")
        self.gear_limits_label.pack(anchor="w", pady=(4, 0))
        factory_check = ttk.Checkbutton(transmission, text="Use factory engine profile", variable=self.factory_engine)
        factory_check.pack(anchor="w", pady=(16, 4))
        self.factory_check = factory_check
        self._help(factory_check, factory_check, field_help.OTHER_HELP["engine_profile"])
        ttk.Label(transmission, text=field_help.OTHER_HELP["engine_profile"], style="Hint.TLabel",
                  wraplength=760, justify="left").pack(anchor="w")
        remaining = [key for key in prepare.PHYSICS if key not in DRIVING + TRANSMISSION]
        self._section_heading(advanced, "More driving settings", table_hint)
        self._physics_table(advanced, remaining).pack(anchor="w", fill="x")
        ttk.Label(advanced, text="Base stats does not change the MM3 donor's nominal wheel radius. The loaded wheel "
                                 "model sets the actual rolling radius; imported wheels can change it.",
                  style="Hint.TLabel", wraplength=760, justify="left").pack(anchor="w", pady=(8, 0))
        hud = ttk.LabelFrame(models, text="On-screen speedometer (HUD)", padding=14)
        hud.pack(fill="x", pady=(0, 16))
        hud_row = ttk.Frame(hud)
        hud_row.pack(fill="x", pady=(0, 8))
        hud_label = ttk.Label(hud_row, text="HUD style", width=12)
        hud_label.pack(side="left")
        self.hud_combo = ttk.Combobox(hud_row, textvariable=self.hud_var, state="readonly", width=48)
        self.hud_combo.pack(side="left")
        self._help(hud_label, self.hud_combo, field_help.OTHER_HELP["hud_style"])
        limits = ttk.Frame(hud)
        limits.pack(fill="x", pady=(0, 8))
        self.dial_fields = {}
        for key, unit in (("max_kmh", "km/h"), ("max_mph", "mph")):
            label = ttk.Label(limits, text=f"Dial max {unit}")
            label.pack(side="left", padx=(0, 6))
            low, high = prepare.SPEEDOMETER[key]
            field = ttk.Spinbox(limits, from_=low, to=high, increment=10, textvariable=self.speed_vars[key], width=8,
                                format="%.0f")
            field.pack(side="left", padx=(0, 20))
            self.dial_fields[key] = field
            self._help(label, field, field_help.OTHER_HELP[key])
            self.speed_vars[key].trace_add("write", self._dial_changed)
            self.placeholders[key] = widgets.Placeholder(field, self.speed_vars[key])
            self._spin_from_inherited(field, self.speed_vars[key], lambda key=key: self._native_hud_limits().get(key),
                                      10, low, high, "%.0f")
        ttk.Label(hud, textvariable=self.dial_problem, style="Problem.TLabel", wraplength=760,
                  justify="left").pack(anchor="w")
        self.hud_info = ttk.Label(hud, style="Value.TLabel", wraplength=760, justify="left")
        self.hud_info.pack(anchor="w", pady=(0, 8))
        hud_images = ttk.Frame(hud)
        hud_images.pack(anchor="w")
        self.hud_canvases, self.hud_captions = {}, {}
        for metric, unit in ((True, "km/h"), (False, "mph")):
            panel = ttk.Frame(hud_images)
            panel.pack(side="left", anchor="n", padx=(0, 16))
            canvas = tk.Canvas(panel, width=180, height=170, background="#172638", highlightthickness=0)
            canvas.pack()
            caption = ttk.Label(panel, style="Value.TLabel")
            caption.pack(anchor="w", pady=(4, 0))
            self.hud_canvases[metric], self.hud_captions[metric] = canvas, caption
        ttk.Label(hud, text="Dial maxima update the HUD markings and needle scale, plus the interior needle calibration. "
                            "Leave both blank for the selected style's native HUD. A custom range creates a new dial; "
                            "the preview needle rests at zero. Save & Apply with MM3 closed, then restart.",
                  style="Hint.TLabel", wraplength=760, justify="left").pack(anchor="w", pady=(8, 0))
        self.original_speed_button = ttk.Button(hud, text="Original HUD speedometers...", command=self.original_speedometers, style="Small.TButton")
        self.original_speed_button.pack(anchor="w", pady=(8, 0))
        package = ttk.LabelFrame(models, text="Model package", padding=14)
        package.pack(fill="x", pady=(0, 16))
        ttk.Label(package, text="Choose a converted model folder containing package.json.", style="Sub.TLabel").pack(anchor="w", pady=(0, 8))
        self.model_entry = ttk.Entry(package, textvariable=self.model_var)
        self.model_entry.pack(fill="x")
        self._help(self.model_entry, self.model_entry, field_help.OTHER_HELP["model_package"])
        model_buttons = ttk.Frame(package)
        model_buttons.pack(fill="x", pady=8)
        self.model_browse_button = ttk.Button(model_buttons, text="Choose folder...", command=self.browse_model)
        self.model_browse_button.pack(side="left")
        self.model_clear_button = ttk.Button(model_buttons, text="Use MM3 donor model", command=lambda: self.model_var.set(""))
        self.model_clear_button.pack(side="left", padx=8)
        ttk.Label(package, textvariable=self.model_status, style="Sub.TLabel", wraplength=700).pack(anchor="w")
        # Offer a prepared package from out/car-models/packages when none is chosen.
        self.package_offer = ttk.Frame(package)
        self.package_offer_label = ttk.Label(self.package_offer, style="Value.TLabel")
        self.package_offer_label.pack(side="left")
        self.package_offer_button = ttk.Button(self.package_offer, text="Use it", style="Small.TButton",
                                               command=self.use_detected_package)
        self.package_offer_button.pack(side="left", padx=(8, 0))
        appearance = ttk.Frame(models)
        appearance.pack(fill="x")
        paint = ttk.LabelFrame(appearance, text="Paint", padding=12)
        paint.pack(side="left", anchor="n", padx=(0, 18))
        self.canvas = tk.Canvas(paint, width=245, height=145, background="#172638", highlightthickness=0)
        self.canvas.pack()
        ttk.Label(paint, text="Color schematic", style="Hint.TLabel").pack(anchor="w", pady=6)
        self.paint_frame = ttk.Frame(paint)
        self.paint_frame.pack(fill="x")
        palette_buttons = ttk.Frame(paint)
        palette_buttons.pack(fill="x", pady=6)
        self.add_button = ttk.Button(palette_buttons, text="+ Paint", command=self.add_color, style="Small.TButton")
        self.add_button.pack(side="left")
        self.remove_button = ttk.Button(palette_buttons, text="- Paint", command=self.remove_color, style="Small.TButton")
        self.remove_button.pack(side="right")
        ttk.Label(paint, text=field_help.OTHER_HELP["colors"], style="Hint.TLabel", wraplength=245).pack(anchor="w", pady=6)
        preview = ttk.LabelFrame(appearance, text="Interior reference", padding=12)
        preview.pack(side="left", anchor="n", fill="x", expand=True)
        self.dial_info = ttk.Label(preview, style="Value.TLabel", wraplength=320, justify="left")
        self.dial_info.pack(anchor="w", pady=(6, 0))
        ttk.Label(preview, text="The HUD dial maxima above also calibrate the interior model's needle. "
                               "Blank values keep the donor's interior calibration. Dial maxima do not change engine power or actual top speed.",
                  style="Hint.TLabel", wraplength=245).pack(anchor="w", pady=8)
        self.help_box = ttk.LabelFrame(main, text="Setting guide", padding=(12, 8))
        guide = ttk.Label(self.help_box, textvariable=self.setting_help, style="Sub.TLabel", wraplength=780)
        guide.pack(anchor="w")
        self.help_box.bind("<Configure>", lambda e: guide.configure(wraplength=max(240, e.width - 30)))
        ttk.Separator(footer).pack(fill="x", pady=(14, 10))
        buttons = ttk.Frame(footer)
        buttons.pack(fill="x")
        self.save_button = ttk.Button(buttons, text="Save settings", command=self.save)
        self.save_button.pack(side="left")
        self.apply_button = ttk.Button(buttons, text="Save & Apply to game", command=self.apply, style="Accent.TButton")
        self.apply_button.pack(side="left", padx=8)
        self.reload_button = ttk.Button(buttons, text="Reload", command=self.reload)
        self.reload_button.pack(side="left")
        saved_info = ttk.Frame(buttons)
        saved_info.pack(side="left", padx=14)
        ttk.Label(saved_info, text=f"Settings: {self.model.path.name}", style="Hint.TLabel").pack(anchor="w")
        self.install_badge = ttk.Label(saved_info, textvariable=self.install_status, style="Hint.TLabel")
        self.install_badge.pack(anchor="w")
        ToolTip(self.install_badge, "Compares the saved file with the settings recorded by the last installation. "
                                    "Unsaved edits and saved settings that still need Apply are shown separately. "
                                    "The installer checks game-file integrity before applying.")
        ttk.Button(buttons, text="Close", command=self.close).pack(side="right")
        self.apply_progress = ttk.Progressbar(buttons, mode="indeterminate", length=160)
        ttk.Label(footer, textvariable=self.status, style="Sub.TLabel", wraplength=1090).pack(anchor="w", pady=(8, 0))
        self.root.bind("<Control-s>", lambda _e: self.save())
        self.root.bind("<Control-f>", self._focus_search)
        for scroll_form in self.scroll_forms.values():
            scroll_form.capture_value_widgets()
        self.search_entry = search
        self._refresh_static_cells()
        self.notebook.select(self.tabs["specs"])

    def _focus_search(self, _event=None):
        """Ctrl+F: search Game stats on that tab, otherwise the car list."""
        if self.notebook.select() == str(self.tabs["stats"]):
            self.stats_view.search_entry.focus_set()
            self.stats_view.search_entry.selection_range(0, "end")
        else:
            self.search_entry.focus_set()
            self.search_entry.selection_range(0, "end")
        return "break"

    def _help(self, label, field, tip: str) -> None:
        focus = lambda text: self._focus_help(field, text)
        ToolTip(label, tip, focus)
        if field is not label:
            ToolTip(field, tip, focus)

    def _focus_help(self, widget, tip: str) -> None:
        self.setting_help.set(tip)
        parent = widget.master
        while parent is not None:
            if isinstance(parent, ScrollableFrame):
                parent.reveal(widget)
                break
            parent = getattr(parent, "master", None)

    def _section_heading(self, parent, title: str, hint: str = "") -> None:
        ttk.Label(parent, text=title, style="Section.TLabel", font=("Segoe UI", 12, "bold")).pack(anchor="w")
        if hint:
            ttk.Label(parent, text=hint, style="Hint.TLabel", wraplength=760,
                      justify="left").pack(anchor="w", pady=(2, 8))

    @staticmethod
    def _short_name(name: str, limit: int = 18) -> str:
        return name if len(name) <= limit else name[:limit - 1].rstrip() + "…"

    def _donor_name(self) -> str:
        """Display name of the current added car's original MM3 donor."""
        cars = self.model.config["cars"]
        if not cars:
            return "—"
        car = cars[min(self.current, len(cars) - 1)]
        donor = self.original_by_alias.get(car["donor"])
        return donor["display_name"] if donor else car["donor"]

    def _base_name(self) -> str:
        """Name of the car whose driving values blank fields inherit."""
        if self.original_current is not None or not self.model.config["cars"]:
            return "—"
        alias = self._selected_base_alias()
        if alias:
            base = self.original_by_casefold.get(alias.casefold())
            return base["display_name"] if base else alias
        return self._donor_name()

    def _selected_base_alias(self) -> str:
        """Alias chosen in the Base stats picker; "" means the MM3 donor."""
        return self.base_choices.get(self.base_var.get(), "")

    def _driving_base_value(self) -> str | None:
        """Picker state for edited_car: None keeps the saved spelling, "" removes."""
        if self.original_current is not None or self.base_var.get() not in self.base_choices:
            return None
        alias = self._selected_base_alias()
        loaded = self._loaded_base
        if (loaded is None and not alias) or (loaded is not None and alias.casefold() == loaded.casefold()):
            return None
        return alias

    def _base_reference(self, car: dict) -> dict | None:
        """Loaded original car named by ``car``'s driving_base, if any."""
        name = car.get("driving_base")
        return self.original_by_casefold.get(name.casefold()) if isinstance(name, str) else None

    def _hud_reference(self, car: dict) -> dict | None:
        name = car.get("hud_style")
        return self.original_by_casefold.get(name.casefold()) if isinstance(name, str) else None

    def _hud_style_value(self) -> str | None:
        """None preserves a loaded alias's spelling; blank restores donor HUD."""
        if self.original_current is not None or self.hud_var.get() not in self.hud_choices:
            return None
        alias = self.hud_choices[self.hud_var.get()]
        loaded = self._loaded_hud
        if (loaded is None and not alias) or (loaded is not None and alias.casefold() == loaded.casefold()):
            return None
        return alias

    def _set_hud_choices(self, car: dict) -> None:
        default = f"MM3 donor ({self._donor_name()})"
        choices = {default: ""}
        for original in sorted(self.original_cars[:self.retail_count], key=lambda item: item["display_name"].casefold()):
            markings = reference.native_hud_markings(original)
            suffix = f" — {markings['max_kmh']} km/h, {markings['max_mph']} mph" if markings else ""
            choices[original["display_name"] + suffix] = original["alias"]
        loaded = car.get("hud_style")
        selected = default
        if loaded:
            selected = next((text for text, alias in choices.items() if alias.casefold() == loaded.casefold()), None)
            if selected is None:
                selected = f"{loaded} (not in the loaded game data)"
                choices[selected] = loaded
        self.hud_choices = choices
        self._loaded_hud = loaded
        self.hud_combo.configure(values=list(choices))
        self.hud_var.set(selected)

    def _hud_changed(self, *_args) -> None:
        if self.loading or self.original_current is not None or not hasattr(self, "hud_info"):
            return
        self._refresh_dial_info()
        self._form_changed()

    def _dial_changed(self, *_args) -> None:
        if self.loading or self.original_current is not None or not hasattr(self, "hud_info"):
            return
        self._refresh_dial_info()
        self._form_changed()

    def _native_hud_limits(self) -> dict:
        if self.original_current is not None or not self.model.config["cars"]:
            return {}
        car = self.model.config["cars"][self.current]
        alias = self.hud_choices.get(self.hud_var.get(), car.get("hud_style", "")) or car["donor"]
        return reference.native_hud_markings(self.original_by_casefold.get(alias.casefold()))

    def _set_base_choices(self, car: dict) -> None:
        """MM3 donor first, then every other original car with its DLC twins."""
        def label(original: dict, note: str = "") -> str:
            twins = self.twins.get(original["alias"], [])
            parts = [note] if note else []
            if twins:
                parts.append("also DLC: " + ", ".join(twins))
            return original["display_name"] + (f" ({'; '.join(parts)})" if parts else "")

        donor = self.original_by_alias.get(car["donor"])
        default = label(donor, "MM3 donor") if donor else f"{car['donor']} (MM3 donor)"
        choices = {default: ""}
        for original in sorted(self.original_cars[:self.retail_count], key=lambda item: item["display_name"].casefold()):
            if original["alias"].casefold() != car["donor"].casefold():
                choices[label(original)] = original["alias"]
        loaded = car.get("driving_base")
        selected = default
        if loaded:
            selected = next((text for text, alias in choices.items() if alias and alias.casefold() == loaded.casefold()), None)
            if selected is None:
                # Keep a saved base the loaded game data cannot show.
                selected = f"{loaded} (not in the loaded game data)"
                choices[selected] = loaded
        self.base_choices = choices
        self._loaded_base = loaded
        self.base_combo.configure(values=list(choices))
        self.base_var.set(selected)

    def _base_details_text(self) -> str:
        if self.original_current is not None:
            return "Read-only reference. Compare it with any car; added cars show their current drafts."
        donor = self._donor_name()
        if self.factory_engine.get():
            return (f"The factory engine profile sets this car's engine, so base stats stay with the {donor} "
                    "MM3 donor. Appearance selects exterior visuals. Click a Compare value to copy it.")
        alias = self._selected_base_alias() or self.model.config["cars"][self.current]["donor"]
        twins = self.twins.get(alias) or self.twins.get(next(
            (name for name in self.twins if name.casefold() == alias.casefold()), ""), [])
        same = f" {self._base_name()} drives the same as DLC {', '.join(twins)}." if twins else ""
        if self._selected_base_alias():
            return (f"Engine, gearbox, weight, brakes, grip and drag come from {self._base_name()}; interior, sound "
                    f"and collision stay with the {donor} MM3 donor. Appearance selects exterior visuals. Your values always win.{same} "
                    "Click a Compare value to copy it.")
        return (f"Blank values use the {donor} MM3 donor.{same} Choose another original car as Base stats to use "
                "its engine, gearbox, weight, brakes, grip and drag. Appearance selects exterior visuals. Click a Compare value to copy it.")

    def _picker_hint_text(self) -> str:
        if self.original_current is not None:
            return "Read-only reference. Compare it with any car; added cars show their current drafts."
        if self.factory_engine.get():
            return "The factory engine profile controls the engine. Appearance selects the exterior; Compare values can be copied."
        return (f"Blank driving values come from {self._base_name()}; your entered values override them. "
                "Click a Compare value to copy it.")

    def _update_base_controls(self) -> None:
        """Show the base picker for added cars; it excludes the factory engine."""
        if self.original_current is None:
            self.base_label.grid()
            self.base_combo.grid()
            self.base_combo.configure(state="disabled" if self.factory_engine.get() else "readonly")
            self.factory_check.configure(state="disabled" if self._selected_base_alias() else "normal")
        else:
            self.base_label.grid_remove()
            self.base_combo.grid_remove()
        self.picker_hint.configure(text=self._picker_hint_text())

    def _base_changed(self, *_args) -> None:
        if self.loading or self.original_current is not None or not self.setting_rows:
            return
        self._refresh_base_cells()
        self._update_base_controls()
        self._show_game_stats()
        self._form_changed()
        overrides = sum(1 for variable in self.physics_vars.values() if variable.get().strip())
        overrides += 1 if self.gear_var.get().strip() else 0
        plural = "s still apply" if overrides != 1 else " still applies"
        self.status.set(f"Base stats now from {self._base_name()}. {overrides} value{plural}; "
                        "clear a value to use the base. Save to keep the change.")

    def _compare_name(self) -> str:
        if not hasattr(self, "stats_view") or self.stats_view.reference_car() is None:
            return "None"
        return self.compare_var.get()

    def _table_headings(self) -> dict:
        count = len(self.original_cars)
        return {"setting": "Setting", "value": "Your value",
                "base": "Base\n" + self._short_name(self._base_name()),
                "compare": "Compare\n" + self._short_name(self._compare_name()),
                "allowed": "Max\nallowed", "game": f"Game max\nof {count} cars" if count else "Game max"}

    def _physics_table(self, parent, keys) -> "widgets.SettingTable":
        """Editable physics rows with base, compare, limit and game-maximum cells."""
        table = widgets.SettingTable(parent, self._table_headings())
        self.tables.append(table)
        for key in keys:
            variable = tk.StringVar()
            self.physics_vars[key] = variable

            def make_field(owner, key=key, variable=variable):
                if key == "WheelDrive":
                    return ttk.Combobox(owner, textvariable=variable, values=DRIVE_OPTIONS, state="readonly", width=13)
                low, high = reference.allowed_range(prepare.PHYSICS, key)
                step, fmt = reference.STEPS[key]
                return ttk.Spinbox(owner, textvariable=variable, width=10, from_=low, to=high,
                                   increment=step, format=fmt)

            row = table.add_row(key, LABELS.get(key, key), make_field, self.use_compare_value, self.reset_value)
            self.physics_fields[key] = row.field
            self.inherited_labels[key] = row.base_label
            self.setting_rows[key] = row
            placeholder = widgets.Placeholder(row.field, variable)
            self.placeholders[key] = placeholder
            if key == "WheelDrive":
                # A readonly combobox still shows the inherited choice in grey.
                placeholder.update = self._wheel_drive_placeholder(placeholder)
                placeholder.label.bind("<Button-1>", lambda _event, field=row.field: self._post_combobox(field))
                placeholder.update()
            else:
                low, high = prepare.PHYSICS[key]
                step, fmt = reference.STEPS[key]
                self._spin_from_inherited(row.field, variable, lambda key=key: self.base_values["physics"].get(key),
                                          step, low, high, fmt, key)
            self._help(row.label, row.field, lambda key=key: self._field_tip(key))
            ToolTip(row.game_label, lambda key=key: reference.long_range(self.extremes.get(key), key))
            variable.trace_add("write", lambda *_args, key=key: self._value_changed(key))
        return table

    def _wheel_drive_placeholder(self, placeholder):
        def update():
            if placeholder.text and not placeholder.variable.get():
                placeholder.label.place(in_=placeholder.entry, x=5, rely=0.5, anchor="w")
                placeholder.label.lift()
            else:
                placeholder.label.place_forget()
        return update

    @staticmethod
    def _post_combobox(field) -> None:
        field.focus_set()
        try:
            field.tk.call("ttk::combobox::Post", field)
        except tk.TclError:
            pass

    def _spin_from_inherited(self, field, variable, inherited, step, low, high, fmt, key=None) -> None:
        """Arrow keys and buttons step from the inherited value when blank."""
        def spin(direction):
            if "readonly" in field.state() or "disabled" in field.state():
                return "break"
            upper = high
            if key is not None:
                upper = reference.allowed_range(prepare.PHYSICS, key, self.factory_engine.get())[1]
            if variable.get().strip():
                return None  # Tk's own increment handles typed values.
            variable.set(reference.spin_value("", inherited(), direction, step, low, upper, fmt))
            field.icursor("end")
            return "break"
        field.bind("<<Increment>>", lambda _event: spin(1))
        field.bind("<<Decrement>>", lambda _event: spin(-1))

    def reset_value(self, key: str) -> None:
        """Reset button: blank the override so the Base value applies again."""
        field = self.physics_fields.get(key)
        if field is None or self.original_current is not None:
            return
        label = LABELS.get(key, key)
        # WheelDrive is a read-only combobox by design; factory mode locks others.
        if key != "WheelDrive" and "readonly" in field.state():
            self.status.set(f"{label} is set by the factory engine profile while it is switched on.")
            return
        self.physics_vars[key].set("")
        self.status.set(f"{label} now uses the Base value. Save to keep the change.")

    def _draft_car(self) -> dict:
        """The current added car as the form describes it (may raise ValueError)."""
        return edited_car(self.model.config["cars"][self.current],
                          {key: variable.get() for key, variable in self.rating_vars.items()},
                          {key: variable.get() for key, variable in self.physics_vars.items()},
                          self.gear_var.get(), [variable.get() for variable in self.color_vars],
                          {key: variable.get() for key, variable in self.speed_vars.items()}, self.model_var.get(),
                          factory_engine=self.factory_engine.get(), driving_base=self._driving_base_value(),
                          hud_style=self._hud_style_value())

    def _form_changed(self, *_args) -> None:
        """Any edit: refresh quick summaries now, validate after a short pause."""
        if self.loading or self.original_current is not None or not self.setting_rows:
            return
        self._update_override_summary()
        self._update_rpm_line()
        self._schedule_validation()

    def _schedule_validation(self) -> None:
        if self._validation_job is not None:
            self.root.after_cancel(self._validation_job)
        self._validation_job = self.root.after(400, self.flush_validation)

    def flush_validation(self) -> None:
        """Validate the draft now (tests and car switches skip the pause)."""
        if self._validation_job is not None:
            try:
                self.root.after_cancel(self._validation_job)
            except tk.TclError:
                pass
            self._validation_job = None
        if self.original_current is None and self.setting_rows:
            self._evaluate_draft()

    def _field_problem(self, key: str) -> str:
        text = self.physics_vars[key].get().strip()
        if not text:
            return ""
        label = LABELS.get(key, key)
        try:
            value = float(text.split(" - ", 1)[0])
        except ValueError:
            return f"{label} must be a number"
        if not math.isfinite(value):
            return f"{label} must be a finite number"
        if key == "WheelDrive":
            return "" if value in (0, 1, 2) else "Driven wheels must be front, rear or all wheels"
        low, high = reference.allowed_range(prepare.PHYSICS, key, self.factory_engine.get())
        if not low <= value <= high:
            return f"{label} must be from {reference.format_number(low)} to {reference.format_number(high)}"
        return ""

    def _rating_problem(self, key: str) -> str:
        text = self.rating_vars[key].get().strip()
        name = "Top speed" if key == "TOPSPEED" else key.title()
        try:
            value = number(text, f"{name} rating", True)
        except ValueError:
            return f"{name} rating must be a whole number from 0 to 9"
        return "" if 0 <= value <= 9 else f"{name} rating must be from 0 to 9"

    def _evaluate_draft(self) -> None:
        """Mark invalid fields and group problems without dialogs or saving."""
        problems: dict[str, str] = {}
        for key, row in self.setting_rows.items():
            message = self._field_problem(key)
            row.set_problem(message)
            if message:
                problems[key] = message
        for key, row in self.rating_rows.items():
            message = self._rating_problem(key)
            row.set_problem(message)
            if message:
                problems[f"rating.{key}"] = message
        gear_message = dial_message = ""
        try:
            candidate = copy.deepcopy(self.model.config)
            candidate["cars"][self.current] = self._draft_car()
            validated(candidate)
        except (ValueError, OSError) as error:
            message = str(error)
            lowered = message.casefold()
            if "speedometer" in lowered:
                dial_message = problems["dial"] = message
            elif "rpm" in lowered:
                problems["rpm"] = message
            elif "gear" in lowered or "ratio" in lowered:
                gear_message = problems["gears"] = message
            elif not problems:
                problems["car"] = message
        for key in ("MinRPM", "MinRPMGearChange", "CutRPM", "MaxRPM", "MaxTorqueRPM", "MaxTurboTorqueRPM"):
            row = self.setting_rows[key]
            if "rpm" in problems and self.physics_vars[key].get().strip() and not row.problem:
                row.set_problem(problems["rpm"])
        self.gear_problem.set(gear_message)
        self.dial_problem.set(dial_message)
        previous = self.problems
        self.problems = problems
        self._update_rpm_line()
        self._mark_tabs()
        self._update_dirty_markers()
        if problems:
            self.status.set(f"Check this value: {next(iter(problems.values()))}")
        elif previous:
            self.status.set("All values are valid. Save to keep your changes.")

    def _mark_tabs(self) -> None:
        groups = {"driving": set(DRIVING) | {f"rating.{key}" for key in RATING_ORDER},
                  "gears": set(TRANSMISSION) | {"gears", "rpm"},
                  "advanced": {key for key in prepare.PHYSICS if key not in DRIVING + TRANSMISSION},
                  "model": {"dial"}}
        for tab, keys in groups.items():
            flagged = any(key in self.problems for key in keys)
            self.notebook.tab(self.tabs[tab], text=self.tab_titles[tab] + ("  ⚠" if flagged else ""))

    RPM_KEYS = ("MinRPM", "MinRPMGearChange", "MaxTorqueRPM", "MaxTurboTorqueRPM", "CutRPM", "MaxRPM")

    def _effective_number(self, key: str):
        text = self.physics_vars[key].get().strip()
        if text:
            try:
                return float(text)
            except ValueError:
                return None
        value = self.base_values["physics"].get(key)
        return float(value) if reference.is_number(value) else float(prepare.DONOR_RPM.get(key, 0))

    def _update_rpm_line(self) -> None:
        """Live check of the gearbox RPM order using effective values."""
        if not hasattr(self, "rpm_label"):
            return
        values = {key: self._effective_number(key) for key in self.RPM_KEYS}
        if any(value is None for value in values.values()):
            self.rpm_line.set("RPM order: finish the value in progress to check it.")
            self.rpm_label.configure(style="Sub.TLabel")
            return
        idle, shift, peak, turbo, cutoff, redline = (values[key] for key in self.RPM_KEYS)
        ok = idle < shift < cutoff <= redline and idle < peak < cutoff and idle < turbo < cutoff
        text = (f"idle {reference.format_number(idle)} < minimum shift {reference.format_number(shift)} < cutoff "
                f"{reference.format_number(cutoff)} <= redline {reference.format_number(redline)}, with peak torque "
                f"{reference.format_number(peak)} and turbo peak {reference.format_number(turbo)} between idle and cutoff.")
        self.rpm_line.set(("✓ RPM order is valid: " if ok else "✗ RPM order needs fixing: ") + text)
        self.rpm_label.configure(style="Sub.TLabel" if ok else "Problem.TLabel")

    def _update_override_summary(self) -> None:
        if not hasattr(self, "override_summary") or self.original_current is not None:
            return
        count = sum(1 for variable in self.physics_vars.values() if variable.get().strip())
        count += 1 if self.gear_var.get().strip() else 0
        self.override_summary.configure(
            text=f"{count} value{'s' if count != 1 else ''} changed. Blank values use {self._base_name()}.")

    def clear_overrides(self) -> None:
        """Blank every physics value and the gearbox; keeps ratings, paint and model."""
        if self.original_current is not None:
            return
        for variable in self.physics_vars.values():
            variable.set("")
        self.gear_var.set("")
        note = " The factory engine profile was switched off." if self.factory_engine.get() else ""
        self.factory_engine.set(False)
        self.status.set(f"All driving values now come from {self._base_name()}.{note} Save to keep the change.")

    def copy_base_ratings(self) -> None:
        """Copy the Base stats car's menu ratings into this car."""
        if self.original_current is not None:
            return
        ratings = self.base_values["ratings"]
        if not ratings:
            self.status.set(f"{self._base_name()}'s menu ratings are not available.")
            return
        for key, variable in self.rating_vars.items():
            if key in ratings:
                variable.set(str(ratings[key]))
        self.status.set(f"Copied {self._base_name()}'s menu ratings. Save to keep them.")

    @staticmethod
    def _normalised(car: dict) -> dict:
        result = copy.deepcopy(car)
        result["colors"] = [str(color).lower() for color in result.get("colors", [])]
        return result

    def _car_is_dirty(self, index: int, draft: dict | None = None) -> bool:
        saved = self.model.saved["cars"]
        if index >= len(saved):
            return True
        current = draft if draft is not None else self.model.config["cars"][index]
        return self._normalised(current) != self._normalised(saved[index])

    def is_dirty(self) -> bool:
        """True when any car or original speedometer differs from the saved file."""
        return bool(self.dirty_cars) or (self.model.config.get("original_speedometers", {})
                                         != self.model.saved.get("original_speedometers", {}))

    def _update_dirty_markers(self) -> None:
        dirty = set()
        for index in range(len(self.model.config["cars"])):
            draft = None
            if self.original_current is None and index == self.current:
                try:
                    draft = self._draft_car()
                except (ValueError, OSError):
                    dirty.add(index)
                    continue
            if self._car_is_dirty(index, draft):
                dirty.add(index)
        if dirty != self.dirty_cars:
            self.dirty_cars = dirty
            self._filter_catalog()
        self.root.title(("*" if self.is_dirty() else "") + f"MM3 Car Workshop — {self.model.path.name}")
        if self.original_current is None and self.current in dirty:
            self.unsaved_badge.pack(side="left", padx=(14, 0), pady=(8, 0))
        else:
            self.unsaved_badge.pack_forget()
        self._update_install_status()

    def _update_install_status(self) -> None:
        style = "Hint.TLabel"
        if self.applying:
            text = "Applying saved settings..."
        elif self.is_dirty():
            text, style = "Unsaved edits", "Badge.TLabel"
        elif self.applied_settings == self.model.saved:
            text = "Applied · restart MM3" if self.applied_in_session else "Saved · matches last apply"
            style = "Applied.TLabel"
        elif self.applied_settings is None:
            text = "Saved · no recorded apply"
        else:
            text, style = "Saved · apply needed", "Badge.TLabel"
        self.install_status.set(text)
        self.install_badge.configure(style=style)

    def _ratings_table(self, parent) -> "widgets.SettingTable":
        """Menu rating rows: 0 to 9, with base, compare and game maximum."""
        table = widgets.SettingTable(parent, self._table_headings())
        self.tables.append(table)
        for key in RATING_ORDER:
            variable = tk.StringVar()
            self.rating_vars[key] = variable
            row = table.add_row(
                f"rating.{key}", key.replace("TOPSPEED", "TOP SPEED").title(),
                lambda owner, variable=variable: ttk.Spinbox(owner, from_=0, to=9, increment=1, width=3,
                                                             textvariable=variable, format="%.0f"),
                self.use_compare_value,
                make_extra=lambda owner, variable=variable: widgets.SegmentBar(owner, variable))
            self.rating_rows[key] = row
            self.rating_bars[key] = row.extra
            self._help(row.label, row.field, lambda key=key: self._rating_tip(key))
            ToolTip(row.extra, lambda key=key: self._rating_tip(key))
            ToolTip(row.game_label, lambda key=key: reference.long_range(self.extremes.get(f"rating.{key}")))
            variable.trace_add("write", self._form_changed)
        return table

    def _field_tip(self, key: str) -> str:
        low, high = reference.allowed_range(prepare.PHYSICS, key, self.factory_engine.get())
        return "\n".join((
            field_help.PHYSICS_HELP[key],
            f"Allowed for your value: {reference.format_number(low)} to {reference.format_number(high)}. "
            "Blank uses the Base column.",
            reference.long_range(self.extremes.get(key), key)))

    def _rating_tip(self, key: str) -> str:
        return "\n".join((field_help.RATING_HELP[key], "Allowed: 0 to 9.",
                          reference.long_range(self.extremes.get(f"rating.{key}"))))

    def _value_text(self, key: str, value, have_car: bool = True) -> str:
        if not have_car:
            return ""
        if value is None:
            return "game default"
        if key == "WheelDrive" and type(value) is int and 0 <= value <= 2:
            return reference.DRIVE_NAMES[value]
        if isinstance(value, list):
            return ", ".join(reference.format_number(item) for item in value)
        return reference.format_number(value)

    def _refresh_static_cells(self) -> None:
        """Max allowed (depends on the factory engine switch) and game maximums."""
        factory_mode = self.factory_engine.get()
        for key, row in self.setting_rows.items():
            _low, high = reference.allowed_range(prepare.PHYSICS, key, factory_mode)
            row.set_cells(allowed="0 to 2" if key == "WheelDrive" else reference.format_number(high))
            if key != "WheelDrive":
                row.field.configure(to=high)
            self._value_changed(key)
        game = [self.extremes.get(reference.gear_key(index)) for index in range(widgets.GearGrid.MAX_FORWARD + 1)]
        self.gear_grid.set_reference("Game max", [
            None if extreme is None or extreme.high is None else (-extreme.high if index == 0 else extreme.high)
            for index, extreme in enumerate(game)])
        for key, row in self.rating_rows.items():
            row.set_cells(allowed=str(reference.RATING_MAX),
                          game=reference.short_high(self.extremes.get(f"rating.{key}")))

    def _value_changed(self, key: str) -> None:
        """Mark overrides and flag values beyond the game's highest or lowest."""
        row = self.setting_rows.get(key)
        if row is None:
            return
        text = self.physics_vars[key].get().strip()
        row.set_overridden(bool(text))
        if text:
            try:
                value = float(text.split(" - ", 1)[0])
            except ValueError:
                value = None
        else:
            value = self.base_values["physics"].get(key)
        extreme = self.extremes.get(key)
        game = reference.short_high(extreme, key)
        if key != "WheelDrive":
            game += reference.outside_flag(value, extreme)
        row.set_cells(game=game)
        # Errors appear after a pause, but disappear as soon as a value is fixed.
        if row.problem and not self._field_problem(key):
            row.set_problem("")
        self._form_changed()

    def _inherited_stats(self) -> dict | None:
        """What blank fields inherit, using the installer's own merge rules."""
        car = self.model.config["cars"][self.current]
        donor = self.original_by_alias.get(car["donor"])
        alias = self._selected_base_alias()
        base = self.original_by_casefold.get(alias.casefold()) if alias else None
        if donor is None or (alias and base is None):
            return None
        probe = {**copy.deepcopy(car), "physics": {}}
        probe.pop("hud_style", None)  # Driving inheritance does not depend on HUD data.
        probe.pop("driving_base", None)
        if alias:
            probe["driving_base"] = alias
        try:
            return effective_stats(probe, donor, base)
        except (ValueError, OSError, UnicodeError):
            return None

    def _refresh_base_cells(self) -> None:
        car = self.model.config["cars"][self.current]
        donor = self.original_by_alias.get(car["donor"])
        alias = self._selected_base_alias()
        inherited = self._inherited_stats()
        rating_source = self.original_by_casefold.get(alias.casefold()) if alias else donor
        self.base_values = {
            "physics": reference.car_values(inherited)["physics"] if inherited is not None else {},
            # Menu ratings are never inherited; show the base car's for reference.
            "ratings": reference.car_values(rating_source)["ratings"],
            # The interior dial belongs to the MM3 donor's camera setup.
            "dial": reference.car_values(donor)["dial"],
        }
        have = inherited is not None
        for key, row in self.setting_rows.items():
            value = self.base_values["physics"].get(key)
            row.set_cells(base=self._value_text(key, value) if have else "unavailable")
            # Grey text in a blank box shows the value it inherits.
            placeholder = "" if not have or value is None else self._value_text(key, value)
            self.placeholders[key].set_text(f"Base: {placeholder}" if key == "WheelDrive" and placeholder else placeholder)
            self._value_changed(key)
        for key, row in self.rating_rows.items():
            value = self.base_values["ratings"].get(key)
            row.set_cells(base=("unavailable" if rating_source is None else "—" if value is None
                                else reference.format_number(value)))
        for table in self.tables:
            table.set_heading("base", "Base\n" + self._short_name(self._base_name()))
        gears = self.base_values["physics"].get("GearRatioVector")
        self.gear_grid.set_reference("Base", gears if isinstance(gears, list) else [])
        self._refresh_rating_markers()
        self._refresh_gear_lines()
        self._refresh_dial_info()

    def _refresh_rating_markers(self) -> None:
        for key, bar in self.rating_bars.items():
            bar.set_markers(self.base_values["ratings"].get(key), self.compare_values["ratings"].get(key))

    def _added_reference(self, car: dict) -> dict:
        """An added car as a comparison: its saved draft merged with its donor."""
        donor = self.original_by_alias.get(car["donor"])
        preview = None
        if donor is not None:
            try:
                preview = effective_stats(car, donor, self._base_reference(car), self._hud_reference(car))
            except (ValueError, OSError, UnicodeError):
                preview = None
        if preview is None:
            preview = {"id": car["id"], "alias": car["alias"],
                       "physics": copy.deepcopy(car["physics"]), "ratings": copy.deepcopy(car["ratings"])}
        return {**preview, "display_name": f"{car['display_name']} (added)", "reference_group": 0}

    def _refresh_references(self) -> None:
        """Offer other added cars, then original and DLC cars, for comparison."""
        references = [self._added_reference(car) for index, car in enumerate(self.model.config["cars"])
                      if self.original_current is not None or index != self.current]
        references.extend(self.original_cars)
        self.stats_view.set_original_cars(references)
        self.compare_combo.configure(values=self.stats_view.reference_values())
        self._refresh_compare()

    def _compare_changed(self, *_args) -> None:
        if not self._compare_pending:
            self._compare_pending = True
            self.root.after_idle(self._refresh_compare)

    def _refresh_compare(self) -> None:
        self._compare_pending = False
        if not hasattr(self, "stats_view"):
            return
        car = self.stats_view.reference_car()
        have = car is not None
        self.compare_values = reference.car_values(car)
        for table in self.tables:
            table.set_heading("compare", "Compare\n" + self._short_name(self._compare_name()))
        for key, row in self.setting_rows.items():
            row.set_cells(compare=self._value_text(key, self.compare_values["physics"].get(key), have))
        for key, row in self.rating_rows.items():
            value = self.compare_values["ratings"].get(key)
            row.set_cells(compare="" if not have else "—" if value is None else reference.format_number(value))
        gears = self.compare_values["physics"].get("GearRatioVector")
        self.gear_grid.set_reference("Compare", gears if have and isinstance(gears, list) else [])
        self._refresh_rating_markers()
        self._refresh_gear_lines()
        self._refresh_dial_info()

    def _refresh_gear_lines(self) -> None:
        base = self.base_values["physics"].get("GearRatioVector")
        base_text = self._value_text("GearRatioVector", base) if base is not None else "unavailable"
        self.gear_donor_label.configure(text=f"Base gearbox ({self._base_name()}): {base_text}")
        if self.stats_view.reference_car() is None:
            self.gear_compare_label.configure(text="")
            return
        ratios = self.compare_values["physics"].get("GearRatioVector")
        text = self._value_text("GearRatioVector", ratios) if isinstance(ratios, list) else "not stored"
        self.gear_compare_label.configure(text=f"Compare gearbox ({self._compare_name()}): {text}   Click to use these ratios.")

    def _gear_limits_text(self) -> str:
        lines = ["Max allowed: reverse from -10 to -0.1, then 1 to 7 forward gears from 10 down to 0.1, "
                 "each lower than the one before."]
        count, first = self.extremes.get("gear.count"), self.extremes.get("gear.1")
        if count is not None and count.high is not None and first is not None and first.high is not None:
            lines.append(f"Game max: {reference.format_number(count.high)} forward gears "
                         f"({reference.name_list(count.high_names)}); highest 1st gear "
                         f"{reference.format_number(first.high)} ({reference.name_list(first.high_names)}).")
        return "\n".join(lines)

    def _refresh_dial_info(self) -> None:
        def pair(values: dict) -> str:
            kmh, mph = values.get("max_kmh"), values.get("max_mph")
            if kmh is None and mph is None:
                return "no interior dial"
            return " · ".join(f"{reference.format_number(value)} {unit}" for value, unit in ((kmh, "km/h"), (mph, "mph"))
                              if value is not None)
        if self.original_current is None:
            car = self.model.config["cars"][self.current]
            alias = self.hud_choices.get(self.hud_var.get(), car.get("hud_style", "")) or car["donor"]
            source = self.original_by_casefold.get(alias.casefold())
            markings = reference.native_hud_markings(source)
            try:
                limits = edited_speedometer({key: variable.get() for key, variable in self.speed_vars.items()})
                pending = ""
            except ValueError:
                limits = None
                pending = " Complete both valid dial maxima to preview the change; showing the native range."
            ranges = limits if limits is not None else markings
            summary = (f"Dial maximum: {pair(ranges)}."
                       if markings else "HUD style is unavailable in the loaded car data.")
            name = source["display_name"] if source else alias
            self.hud_info.configure(text=f"{name} HUD style. {summary}{pending}")
            if len(self.hud_photos) >= 32:
                self.hud_photos.clear()
            for metric, key, unit in ((True, "max_kmh", "km/h"), (False, "max_mph", "mph")):
                canvas = self.hud_canvases[metric]
                canvas.delete("all")
                native_value = markings.get(key)
                self.placeholders[key].set_text(reference.format_number(native_value) if native_value is not None else "")
                value = ranges.get(key) if markings else None
                style = "native" if value == native_value else "custom"
                self.hud_captions[metric].configure(text=f"Dial max: {value} {unit} · {style}" if value is not None else f"{unit}: unavailable")
                photo_key = (markings.get("type"), metric, value)
                if photo_key not in self.hud_photos:
                    png = hud_preview.load_dial(self.game_dir, markings["type"], metric, value) if markings else None
                    self.hud_photos[photo_key] = tk.PhotoImage(data=base64.b64encode(png)) if png else None
                photo = self.hud_photos[photo_key]
                if photo is not None:
                    canvas.create_image(90, 85, image=photo)
                else:
                    canvas.create_text(90, 70, text=f"{style.title()} HUD dial\n{value} {unit}" if value is not None else "HUD dial\nArtwork unavailable",
                                       fill="#edf5ff", justify="center", font=("Segoe UI", 13))
                    canvas.create_text(90, 135, text="Artwork preview unavailable", fill="#bac7d8", font=("Segoe UI", 8))
        lines = [f"Donor interior ({self._donor_name()}): {pair(self.base_values['dial'])}"]
        if self.stats_view.reference_car() is not None:
            lines.append(f"Compare ({self._compare_name()}): {pair(self.compare_values['dial'])}")
        lines.append(f"Max allowed: {prepare.SPEEDOMETER['max_kmh'][1]} km/h · {prepare.SPEEDOMETER['max_mph'][1]} mph")
        kmh = self.extremes.get("dial.max_kmh")
        if kmh is not None and kmh.high is not None:
            lines.append(f"Game max: {reference.format_number(kmh.high)} km/h ({reference.name_list(kmh.high_names)})")
        self.dial_info.configure(text="\n".join(lines))

    def use_compare_value(self, key: str) -> None:
        """Copy the compared car's value for ``key`` into this car's field."""
        if self.original_current is not None or self.stats_view.reference_car() is None:
            return
        name = self._compare_name()
        if key.startswith("rating."):
            rating = key.split(".", 1)[1]
            value = self.compare_values["ratings"].get(rating)
            if type(value) is int and 0 <= value <= 9:
                self.rating_vars[rating].set(str(value))
                self.status.set(f"Copied the {rating.title()} rating {value} from {name}. Save to keep it.")
            return
        label = LABELS.get(key, key)
        value = self.compare_values["physics"].get(key)
        if not reference.is_number(value):
            self.status.set(f"{name} leaves {label} at the game default, so there is no value to copy.")
            return
        if key == "WheelDrive":
            if type(value) is int and 0 <= value <= 2:
                self.physics_vars[key].set(DRIVE_OPTIONS[value + 1])
                self.status.set(f"Copied driven wheels ({reference.DRIVE_NAMES[value]}) from {name}. Save to keep it.")
            return
        if self.physics_fields[key].instate(["readonly"]):
            self.status.set(f"{label} is set by the factory engine profile while it is switched on.")
            return
        low, high = reference.allowed_range(prepare.PHYSICS, key, self.factory_engine.get())
        if not low <= value <= high:
            self.status.set(f"{name}'s {label} is {reference.format_number(value)}, outside the "
                            f"{reference.format_number(low)} to {reference.format_number(high)} allowed for your value.")
            return
        self.physics_vars[key].set(reference.format_number(value))
        self.status.set(f"Copied {label} {reference.format_number(value)} from {name}. Save to keep it.")

    def use_compare_gears(self) -> None:
        """Copy the compared car's gearbox when it is valid as an override."""
        if self.original_current is not None or self.stats_view.reference_car() is None:
            return
        ratios = self.compare_values["physics"].get("GearRatioVector")
        if not isinstance(ratios, list) or not all(reference.is_number(ratio) for ratio in ratios):
            return
        forward = ratios[1:]
        valid = (2 <= len(ratios) <= 8 and -10 <= ratios[0] <= -0.1 and all(0.1 <= ratio <= 10 for ratio in forward)
                 and all(a > b for a, b in zip(forward, forward[1:])))
        if not valid:
            self.status.set(f"{self._compare_name()}'s gearbox cannot be used as an override: gear ratios must "
                            "decrease, with 1 to 7 forward gears.")
            return
        self.gear_var.set(", ".join(reference.format_number(ratio) for ratio in ratios))
        self.status.set(f"Copied the gearbox from {self._compare_name()}. Save to keep it.")

    def _range_for(self, section: str, key: str) -> str:
        """Game-wide range sentence for any numeric native setting."""
        if key.upper() in ("ID", "PHYSICSID"):
            return ""
        cache_key = (str(section).casefold(), key)
        if cache_key not in self._native_ranges:
            self._native_ranges[cache_key] = reference.native_extremes(self.original_cars, section, key)
        extreme = self._native_ranges[cache_key]
        return reference.long_range(extreme) if extreme is not None else ""

    def _factory_changed(self, *_args) -> None:
        for key in ("MaxTorqueRPM", "MaxTurboTorqueRPM", "TurboSize"):
            if key in self.physics_fields:
                self.physics_fields[key].configure(state="readonly" if self.factory_engine.get() else "normal")
        self._refresh_static_cells()
        for placeholder in self.placeholders.values():
            placeholder.update()
        if hasattr(self, "base_combo") and not self.loading:
            self._update_base_controls()
        self._form_changed()

    def _refresh_catalog_counts(self) -> None:
        filtered = bool(self.search_var.get().strip())
        for index, (name, visible, total) in enumerate((
                ("Added", len(self.visible_added), len(self.model.config["cars"])),
                ("Original", len(self.visible_original), self.retail_count),
                ("DLC", len(self.visible_dlc), len(self.dlc_cars)))):
            count = f"{visible}/{total}" if filtered else str(total)
            marker = "  •" if index == 0 and self.dirty_cars else ""
            self.catalog_tabs.tab(index, text=f"{name} ({count}){marker}")

    def _filter_catalog(self, *_args) -> None:
        if not hasattr(self, "listbox"):
            return
        query = self.search_var.get().strip().casefold()
        self.visible_added = [index for index, car in enumerate(self.model.config["cars"])
                              if query in f"{car['display_name']} {car['alias']} {car['id']}".casefold()]
        self.visible_original = [index for index, car in enumerate(self.original_cars)
                                 if index < self.retail_count and query in f"{car['display_name']} {car['alias']} {car['id']}".casefold()]
        self.visible_dlc = [index for index, car in enumerate(self.original_cars)
                           if index >= self.retail_count and query in f"{car['display_name']} {car['alias']} {car['id']}".casefold()]
        self._refresh_catalog_counts()
        for listing, indices, cars in ((self.listbox, self.visible_added, self.model.config["cars"]),
                                       (self.original_listbox, self.visible_original, self.original_cars),
                                       (self.dlc_listbox, self.visible_dlc, self.original_cars)):
            listing.delete(0, "end")
            for index in indices:
                unsaved = listing is self.listbox and index in self.dirty_cars
                listing.insert("end", cars[index]["display_name"] + ("  •" if unsaved else ""))
                if unsaved:
                    listing.itemconfigure("end", foreground="#b45309", selectforeground="white")
        self._mark_selection()

    def _mark_selection(self) -> None:
        self.listbox.selection_clear(0, "end")
        self.original_listbox.selection_clear(0, "end")
        self.dlc_listbox.selection_clear(0, "end")
        if self.original_current is None and self.current in self.visible_added:
            row = self.visible_added.index(self.current)
            self.listbox.selection_set(row)
            self.listbox.see(row)
        elif self.original_current in self.visible_original:
            row = self.visible_original.index(self.original_current)
            self.original_listbox.selection_set(row)
            self.original_listbox.see(row)
        elif self.original_current in self.visible_dlc:
            row = self.visible_dlc.index(self.original_current)
            self.dlc_listbox.selection_set(row)
            self.dlc_listbox.see(row)

    def _load_original(self, index: int) -> None:
        self.loading = True
        self.original_current = index
        car = self.original_cars[index]
        self.name_label.configure(text=car["display_name"])
        kind = "Original" if index < self.retail_count else "DLC"
        self.identity_label.configure(text=f"{kind} car  |  ID {car['id']}  |  {car['alias']}  |  Read-only reference")
        self._update_base_controls()
        # Read-only references have nothing to validate; keep the unsaved state.
        self.flush_validation()
        self.problems = {}
        self._mark_tabs()
        self._update_dirty_markers()
        self.notebook.select(self.tabs["stats"])
        for key in ("specs", "driving", "gears", "advanced", "model"):
            self.notebook.tab(self.tabs[key], state="disabled")
        self._refresh_references()
        self.stats_view.show_car(car)
        self.catalog_tabs.select(1 if index < self.retail_count else 2)
        self.help_box.pack_forget()
        self._mark_selection()
        self.setting_help.set("Select a stat for its explanation. Compare with another car, or copy a value for an added car.")
        self.status.set(f"{kind} stats are read-only. Switch to Added to edit your custom cars.")
        self.loading = False

    def select_original(self, _event=None) -> None:
        selection = self.original_listbox.curselection()
        self._select_reference(selection, self.visible_original)

    def select_dlc(self, _event=None) -> None:
        self._select_reference(self.dlc_listbox.curselection(), self.visible_dlc)

    def _select_reference(self, selection, indices) -> None:
        if self.loading or not selection:
            return
        index = indices[selection[0]]
        if index == self.original_current:
            return
        try:
            self._collect()
        except (ValueError, OSError) as error:
            self._mark_selection()
            self._error(error)
            return
        self._load_original(index)

    def _tab_changed(self, _event=None) -> None:
        if self.loading or not hasattr(self, "stats_view"):
            return
        selected = self.notebook.select()
        prompts = {
            "driving": "Select a driving value or menu rating for its explanation and accepted range.",
            "gears": "Select an engine setting or gear ratio for its explanation and accepted range.",
            "advanced": "Select a driving setting for its explanation and accepted range.",
            "model": "Select a model, paint color or interior needle setting for its explanation.",
        }
        editable = next((key for key in prompts if selected == str(self.tabs[key])), None)
        if editable is not None:
            if selected != getattr(self, "_guide_tab", None):
                self.setting_help.set(prompts[editable])
            self.help_box.pack(side="bottom", fill="x", pady=(10, 0), before=self.notebook)
        else:
            self.help_box.pack_forget()
        self._guide_tab = selected
        if selected == str(self.tabs["stats"]) and self.original_current is None:
            self._show_game_stats()

    def _show_game_stats(self) -> None:
        car = self.model.config["cars"][self.current]
        donor = self.original_by_alias.get(car["donor"])
        try:
            draft = self._draft_car()
            if donor is None:
                self.stats_view.show_car({"display_name": draft["display_name"], "ratings": draft["ratings"],
                                          "physics": draft["physics"], "source": (self.original_error or "Donor data is unavailable.") + " Draft overrides only."})
                return
            preview = effective_stats(draft, donor, self._base_reference(draft), self._hud_reference(draft))
            if draft.get("engine_profile") == "factory":
                preview["source"] += " Factory engine curve and shift target are defined by its profile."
            self.stats_view.show_car(preview)
        except (ValueError, OSError) as error:
            try:
                if donor is None:
                    raise ValueError("Donor data is unavailable")
                fallback = effective_stats(car, donor, self._base_reference(car), self._hud_reference(car))
            except (ValueError, OSError):
                fallback = {"display_name": car["display_name"], "ratings": car["ratings"], "physics": car["physics"],
                            "source": "Base stats car data is unavailable; saved overrides only."}
            self.stats_view.show_car(fallback)
            self.status.set(f"Showing the last valid settings. Complete the value in progress: {error}")

    def _load_car(self, index: int) -> None:
        self.loading = True
        self.original_current = None
        self.current = index
        car = self.model.config["cars"][index]
        self.name_label.configure(text=car["display_name"])
        for key in ("specs", "driving", "gears", "advanced", "model"):
            self.notebook.tab(self.tabs[key], state="normal")
        self.factory_engine.set(car.get("engine_profile") == "factory")
        self._show_specs(car)
        self.model_var.set(car.get("model_package", ""))
        self._set_hud_choices(car)
        for key, variable in self.rating_vars.items():
            variable.set(str(car["ratings"][key]))
        # Base cells first: the value traces below compare overrides with them.
        self._set_base_choices(car)
        self._refresh_base_cells()
        for key, variable in self.physics_vars.items():
            value = car["physics"].get(key, "")
            variable.set(DRIVE_OPTIONS[value + 1] if key == "WheelDrive" and value != "" else str(value))
        self.gear_var.set(", ".join(str(value) for value in car["physics"].get("GearRatioVector", [])))
        for key, variable in self.speed_vars.items():
            variable.set(str(car.get("speedometer", {}).get(key, "")))
        self._paint_rows(car["colors"])
        self.catalog_tabs.select(0)
        self._mark_selection()
        self.loading = False
        self._update_base_controls()
        self.setting_help.set("Select a value or hover over its label for an explanation and accepted range.")
        self._draw()
        self._refresh_references()
        self._show_game_stats()
        self._tab_changed()
        self._refresh_package_offer()
        self._update_override_summary()
        self._update_rpm_line()
        self.status.set("Edit added-car values; blank values use the Base column. Save & Apply with the game closed.")
        self.flush_validation()

    def _show_specs(self, car: dict) -> None:
        catalog_entry = self.factory_specs[car["alias"]]
        spec = catalog_entry["specification"]
        self.spec_title.configure(text=catalog_entry["variant"])
        self.spec_text.configure(state="normal")
        self.spec_text.delete("1.0", "end")
        lines = [f"Power: {spec.get('power_label') or 'Not published'}"]
        torque = spec.get("torque_nm")
        lines.append(f"Torque: {torque:g} Nm" if torque is not None else f"Torque: {spec.get('torque_label') or 'not published / unresolved'}")
        mass = spec.get("mass_kg")
        lines.append(f"Mass: {mass:g} kg ({spec.get('mass_basis', 'reference')})" if mass is not None else "Mass: not published")
        speed = spec.get("top_speed_kmh")
        lines.append(f"Top speed: {speed:g} km/h ({speed / 1.609344:.0f} mph)" if speed is not None else "Top speed: not published; circuit dependent")
        acceleration = spec.get("zero_to_100_s")
        lines.append(f"0–100 km/h: {acceleration:g} seconds" if acceleration is not None else "0–100 km/h: not published")
        lines.append(f"Transmission: {spec['gear_count']} forward gear(s)")
        lines.append(spec.get("drivetrain", ""))
        self.spec_text.insert("end", "\n".join(lines) + "\n\n")
        self.spec_text.insert("end", "Workshop defaults keep donor gears, RPM, grip and brakes; only weight and engine power are tuned. The specifications and factory calibration notes here are reference information.\n\nFactory reference notes\n" + "\n".join(catalog_entry.get("notes", [])) + "\n\nSources\n")
        for index, source in enumerate(catalog_entry.get("sources", [])):
            tag = f"source_{index}"
            self.spec_text.insert("end", source["title"] + "\n", ("source", tag))
            self.spec_text.tag_bind(tag, "<Button-1>", lambda _event, url=source["url"]: webbrowser.open(url))
        self.spec_text.configure(state="disabled")

    def restore_default_driving(self) -> None:
        try:
            # Tuning may be invalid while typing; the preset replaces it.
            # Preserve current art, menu ratings and gauges without parsing
            # in-progress transmission or handling fields.
            car = copy.deepcopy(self.model.config["cars"][self.current])
            hud = self._hud_style_value()
            if hud is not None:
                car.pop("hud_style", None)
                if hud:
                    car["hud_style"] = hud
            car["colors"] = [variable.get().strip().lower() for variable in self.color_vars]
            car["ratings"] = {key: number(variable.get(), key, True)
                              for key, variable in self.rating_vars.items()}
            car.pop("speedometer", None)
            gauges = edited_speedometer({key: variable.get() for key, variable in self.speed_vars.items()})
            if gauges is not None:
                car["speedometer"] = gauges
            car.pop("model_package", None)
            if self.model_var.get().strip():
                car["model_package"] = self.model_var.get().strip()
            self.model.replace_car(self.current, defaults.apply_preset(car))
            self._load_car(self.current)
        except (ValueError, OSError) as error:
            self._error(error)
            return
        self.status.set("Default donor driving restored for this car. Save & Apply to install it.")

    def select_initial(self, car_id: int | None, tab: str) -> None:
        """Optional launch selection; ordinary launches still start at car one."""
        if car_id is not None:
            index = next((index for index, car in enumerate(self.model.config["cars"])
                          if car["id"] == car_id), None)
            if index is None:
                original_index = next((i for i, car in enumerate(self.original_cars) if car["id"] == car_id), None)
                if original_index is None:
                    raise ValueError(f"Car ID {car_id} is not available in this settings file or game folder")
                self._load_original(original_index)
                return
            self._load_car(index)
        self.notebook.select(self.tabs[tab])

    def _paint_rows(self, colors: list[str]) -> None:
        for child in self.paint_frame.winfo_children():
            child.destroy()
        self.color_vars = []
        self.preview_color = min(self.preview_color, len(colors) - 1)
        for index, color in enumerate(colors):
            variable = tk.StringVar(value=color)
            variable.trace_add("write", self._draw)
            variable.trace_add("write", self._form_changed)
            self.color_vars.append(variable)
            row = ttk.Frame(self.paint_frame)
            row.pack(fill="x", pady=2)
            ttk.Label(row, text=f"Paint {index + 1}", width=8).pack(side="left")
            # Click the swatch to pick a color; it follows valid hex values.
            swatch = tk.Label(row, width=2, relief="solid", borderwidth=1, cursor="hand2",
                              background=color if re.fullmatch(r"#[0-9a-fA-F]{6}", color) else "#888888")
            swatch.pack(side="left", padx=(0, 2))
            swatch.bind("<Button-1>", lambda _event, i=index: self.pick_color(i))
            variable.trace_add("write", lambda *_args, swatch=swatch, variable=variable: swatch.configure(
                background=variable.get() if re.fullmatch(r"#[0-9a-fA-F]{6}", variable.get()) else "#888888"))
            entry = ttk.Entry(row, textvariable=variable, width=11)
            entry.pack(side="left", padx=4)
            self._help(entry, entry, field_help.OTHER_HELP["colors"])
            entry.bind("<FocusIn>", lambda _event, i=index: self.preview_paint(i))
            ttk.Button(row, text="Pick…", command=lambda i=index: self.pick_color(i), style="Small.TButton").pack(side="right")
        self.add_button.configure(state="normal" if len(colors) < 3 else "disabled")
        self.remove_button.configure(state="normal" if len(colors) > 1 else "disabled")

    def _draw(self, *_args) -> None:
        if self.loading or not hasattr(self, "canvas"):
            return
        colors = [variable.get() if re.fullmatch(r"#[0-9a-fA-F]{6}", variable.get()) else "#888888" for variable in self.color_vars]
        color = colors[self.preview_color] if colors else "#888888"
        c = self.canvas
        c.delete("all")
        c.create_text(16, 19, text=f"Paint {self.preview_color + 1}", anchor="w", fill="#d8e4f3", font=("Segoe UI", 9))
        c.create_oval(23, 128, 227, 155, fill="#101c29", outline="")
        for x in (48, 174):
            c.create_oval(x, 112, x + 30, 144, fill="#09121a", outline="#657488", width=2)
            c.create_oval(x + 8, 120, x + 22, 135, fill="#a1aeba", outline="")
        c.create_polygon(22, 116, 36, 92, 75, 87, 94, 63, 154, 63, 181, 88, 217, 98, 224, 119,
                         208, 127, 32, 127, fill=color, outline="#edf5ff", width=2, smooth=True)
        c.create_polygon(92, 69, 117, 69, 117, 89, 77, 89, fill="#7bc1e5", outline="#25344a")
        c.create_polygon(123, 69, 151, 69, 173, 89, 123, 89, fill="#7bc1e5", outline="#25344a")
        c.create_line(123, 95, 123, 121, fill="#405164", width=2)
        c.create_line(134, 98, 145, 98, fill="#edf5ff", width=2)
        c.create_rectangle(211, 104, 220, 113, fill="#f9ecc0", outline="")
        for index, swatch in enumerate(colors):
            item = c.create_oval(84 + index * 29, 162, 101 + index * 29, 179, fill=swatch, outline="white", width=3 if index == self.preview_color else 1)
            c.tag_bind(item, "<Button-1>", lambda _event, i=index: self.preview_paint(i))
        c.scale("all", 0, 0, 1, 0.78)

    def preview_paint(self, index: int) -> None:
        self.preview_color = index
        self._draw()

    def original_speedometers(self) -> None:
        OriginalSpeedometerDialog(self.root, self.model, self._original_speedometers_changed)

    def _original_speedometers_changed(self) -> None:
        self._update_dirty_markers()
        self.status.set("Original speedometer changes kept. Save & Apply to install them, then restart MM3.")

    def pick_color(self, index: int) -> None:
        self.preview_paint(index)
        current = self.color_vars[index].get()
        chosen = colorchooser.askcolor(current if re.fullmatch(r"#[0-9a-fA-F]{6}", current) else "#888888", parent=self.root, title=f"Paint {index + 1}")[1]
        if chosen:
            self.color_vars[index].set(chosen)

    def add_color(self) -> None:
        self._paint_rows([variable.get() for variable in self.color_vars] + ["#ffffff"])
        self._draw()

    def _model_changed(self, *_args) -> None:
        if self.original_current is not None:
            return
        folder = self.model_var.get().strip()
        car = self.model.config["cars"][self.current]
        exterior = "custom package selected" if folder else "donor model"
        self.identity_label.configure(text=f"Added car  |  ID {car['id']}  |  MM3 donor: {self._donor_name()}  |  Exterior: {exterior}")
        if hasattr(self, "package_offer"):
            self._refresh_package_offer()
        self._form_changed()
        if not folder:
            self.model_status.set(f"Selected exterior: {self._donor_name()} donor model. Save & Apply installs this selection.")
            return
        path = Path(folder)
        if not path.is_absolute():
            path = self.model.path.parent / path
        self.model_status.set("Custom exterior package selected; files and target car are checked before applying."
                              if (path / "package.json").is_file()
                              else "package.json is missing from this folder. Choose a prepared MM3 model package.")

    def _detected_package(self) -> Path | None:
        """A prepared package for this car in out/car-models/packages, if any."""
        if self.original_current is not None:
            return None
        folder = PACKAGES_DIR / self.model.config["cars"][self.current]["alias"]
        return folder if (folder / "package.json").is_file() else None

    def _package_text(self, folder: Path) -> str:
        """Manifest-relative POSIX path, as in cars.imported.json."""
        try:
            return Path(os.path.relpath(folder, self.model.path.parent)).as_posix()
        except ValueError:  # Different drives cannot be relative.
            return folder.as_posix()

    def _refresh_package_offer(self) -> None:
        folder = None if self.model_var.get().strip() else self._detected_package()
        if folder is None:
            self.package_offer.pack_forget()
        else:
            self.package_offer_label.configure(text=f"Prepared package found: {self._package_text(folder)}")
            self.package_offer.pack(anchor="w", pady=(6, 0))

    def use_detected_package(self) -> None:
        """Select the detected prepared package after checking its files."""
        folder = self._detected_package()
        if folder is None:
            return
        value = self._package_text(folder)
        try:
            prepare.load_model_package({**self.model.config["cars"][self.current], "model_package": value},
                                       self.model.path.parent)
        except (ValueError, OSError) as error:
            self.status.set(f"The prepared package cannot be used: {error}")
            return
        self.model_var.set(value)
        self.status.set("Using the prepared model package. Save & Apply to install it.")

    def browse_model(self) -> None:
        folder = filedialog.askdirectory(parent=self.root, title="Choose prepared MM3 model package",
                                         initialdir=str(self.model.path.parent), mustexist=True)
        if not folder:
            return
        try:
            car = {**self.model.config["cars"][self.current], "model_package": folder}
            prepare.load_model_package(car, self.model.path.parent)
        except (ValueError, OSError) as error:
            self._error(error)
            return
        self.model_var.set(folder)

    def remove_color(self) -> None:
        self._paint_rows([variable.get() for variable in self.color_vars][:-1])
        self._draw()

    def _collect(self) -> None:
        if getattr(self, "original_current", None) is not None:
            return
        self.model.replace_car(self.current, self._draft_car())
        self._loaded_base = self.model.config["cars"][self.current].get("driving_base")
        self._loaded_hud = self.model.config["cars"][self.current].get("hud_style")

    def _error(self, error) -> None:
        self.status.set(str(error))
        messagebox.showerror("Check car settings", str(error), parent=self.root)

    def select_car(self, _event=None) -> None:
        selection = self.listbox.curselection()
        if self.loading or not selection:
            return
        index = self.visible_added[selection[0]]
        if index == self.current and self.original_current is None:
            return
        try:
            self._collect()
        except (ValueError, OSError) as error:
            self._mark_selection()
            self._error(error)
            return
        self._load_car(index)

    def _apply_locked_widgets(self) -> tuple:
        """Controls that change settings, disabled while the installer runs."""
        return (self.save_button, self.apply_button, self.reload_button, self.original_speed_button, self.restore_button,
                self.clear_button, self.copy_ratings_button, self.package_offer_button,
                self.model_browse_button, self.model_clear_button, self.model_entry)

    def save(self) -> bool:
        if self.applying:
            return False
        try:
            self._collect()
            self.model.save()
        except (ValueError, OSError) as error:
            self._error(error)
            return False
        self._update_dirty_markers()
        self.status.set("Settings saved. Apply them to the game, then restart it to load the changes.")
        return True

    def reload(self) -> None:
        if not messagebox.askyesno("Reload settings", "Discard edits in this window and reload the settings file?", parent=self.root):
            return
        try:
            self.model.reload()
            recorded = last_applied_settings(self.game_dir)
            self.applied_in_session = self.applied_in_session and recorded == self.applied_settings
            self.applied_settings = recorded
            self._filter_catalog()
            self._load_car(min(self.current, len(self.model.config["cars"]) - 1))
            self.status.set("Reloaded settings from disk.")
        except (ValueError, OSError) as error:
            self._error(error)

    def apply(self) -> None:
        if self.applying or not self.save():
            return
        try:
            if game_is_running():
                self.status.set("Settings saved. Close the running MM3 game, then click Save & Apply again.")
                return
        except (OSError, subprocess.SubprocessError) as error:
            self._error(f"Unable to check whether the game is closed: {error}")
            return
        try:
            command, snapshot = self.model.apply_snapshot_command(self.game_dir)
        except OSError as error:
            self._error(error)
            return
        self.applying = True
        self._applying_settings = copy.deepcopy(self.model.saved)
        self._update_install_status()
        for button in self._apply_locked_widgets():
            button.configure(state="disabled")
        self.apply_progress.pack(side="left", padx=(14, 0))
        self.apply_progress.start(12)
        self.status.set("Applying settings… Original game-data backups are preserved by the installer.")
        def worker():
            try:
                result = subprocess.run(command, capture_output=True, text=True,
                                        creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0)
                self.events.put((result.returncode, result.stdout, result.stderr))
            except OSError as error:
                self.events.put((1, "", str(error)))
            finally:
                snapshot.unlink(missing_ok=True)

        threading.Thread(target=worker, daemon=True).start()

    def _poll_apply(self) -> None:
        try:
            code, stdout, stderr = self.events.get_nowait()
        except queue.Empty:
            pass
        else:
            self.applying = False
            self.apply_progress.stop()
            self.apply_progress.pack_forget()
            for button in self._apply_locked_widgets():
                button.configure(state="normal")
            if code:
                self._update_install_status()
                self._error((stderr or stdout).strip() or "The installer did not complete.")
            else:
                self.applied_settings = self._applying_settings
                self.applied_in_session = True
                self._update_dirty_markers()
                self.status.set("Saved settings applied. New edits in this window still need Save & Apply. Restart MM3 to load the changes."
                                if self.is_dirty() else "Applied successfully. Restart MM3 to load the cars, paint and new stats.")
        self.root.after(100, self._poll_apply)

    def close(self) -> None:
        if self.applying:
            self.status.set("Wait for the installation to finish before closing this window.")
            return
        try:
            self._collect()
            dirty = self.model.config != self.model.saved
        except (ValueError, OSError):
            dirty = True
        if dirty:
            answer = messagebox.askyesnocancel("Unsaved settings", "Save your changes before closing?", parent=self.root)
            if answer is None or (answer and not self.save()):
                return
        self.root.destroy()


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, default=prepare.default_config_path(),
                        help="Manifest path (default: cars.local.json when present, otherwise cars.json)")
    parser.add_argument("--game-dir", type=Path, default=prepare.ROOT / "game_files")
    parser.add_argument("--car-id", type=int, help="Select an added, original or installed DLC car when opening the workshop")
    parser.add_argument("--tab", choices=("stats", "specs", "driving", "gears", "advanced", "model"), default="specs",
                        help="Open this settings tab (default: specs)")
    parser.add_argument("--smoke-test", action="store_true", help="Open, verify and close the UI without saving or applying")
    return parser.parse_args(argv)


def main() -> int:
    args = parse_args()
    root = tk.Tk()
    if args.smoke_test:
        root.withdraw()
    try:
        app = CarEditor(root, args.config, args.game_dir)
        app.select_initial(args.car_id, args.tab)
        if args.smoke_test:
            root.update()
            expected_tab = "stats" if app.original_current is not None else args.tab
            if args.car_id is not None:
                selected = (app.original_cars[app.original_current] if app.original_current is not None
                            else app.model.config["cars"][app.current])
                assert selected["id"] == args.car_id
            assert app.notebook.select() == str(app.tabs[expected_tab])
            print(f"Initial selection checked: car {args.car_id or app.model.config['cars'][app.current]['id']}, tab {expected_tab}.")
            baseline = copy.deepcopy(app.model.config)
            for index in range(len(app.model.config["cars"])):
                app._load_car(index)
                app.flush_validation()
                assert not app.problems, f"{app.model.config['cars'][index]['display_name']}: {app.problems}"
                app._collect()
                root.update()
            for index in range(len(app.original_cars)):
                app._load_original(index)
                app._collect()
                root.update()
                assert app.stats_view.tree.get_children()
            app._load_car(0)
            assert app.model.config == baseline, "Browsing must not change saved car tuning"
            dialog = OriginalSpeedometerDialog(root, app.model, lambda: None, show=False)
            for alias in dialog.aliases:
                dialog.load(alias)
                dialog.collect()
                root.update()
            dialog.keep()
            print(f"Car editor UI checked: {len(app.model.config['cars'])} added cars, "
                  f"{app.retail_count} original stat references, {len(app.dlc_cars)} DLC cars, {len(dialog.aliases)} original speedometers, "
                  f"{len(app.physics_vars)} physics fields.")
            root.destroy()
        else:
            root.mainloop()
        return 0
    except (ValueError, OSError) as error:
        messagebox.showerror("MM3 Car Workshop", str(error), parent=root)
        root.destroy()
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
