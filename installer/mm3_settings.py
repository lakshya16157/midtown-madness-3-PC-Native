#!/usr/bin/env python3
"""Midtown Madness 3 (native Linux) settings and Play window.

The Windows launcher's mod choices on Linux: the same launcher.ini the game reads
at start-up (src/mm3_launcher_config.c), so a choice made here is what the Windows
launcher would save. Only the keys this window manages are rewritten; any other
line of the file (the key bindings) is kept as it is.

    python3 mm3_settings.py            use the saved launch preference
    python3 mm3_settings.py --play     start the game with the saved choices, no window
    python3 mm3_settings.py --settings open the window (needs python3-tk)
"""
import os
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
INI = Path(os.environ.get("MM3_LAUNCHER_CONFIG") or HERE / "launcher.ini").absolute()
EXTRA = HERE / "launcher-display.ini"          # this window's own graphics choices
START = HERE / "play-midtown-madness-3.sh"
REVISION = 2                                   # SETTINGS_REVISION in mm3_launcher_config.c
SKIP_LAUNCHER = "MM3_SKIP_LAUNCHER"
WHEEL_DEFAULTS = {
    "MM3_WHEEL_ENABLED": 0,
    "MM3_WHEEL_DEVICE": "",
    "MM3_WHEEL_AXES": "1,-2,-3",
    "MM3_WHEEL_FFB": 0,
}

# (key, label, default) in MM3LauncherOption order; mm3_launcher_options in the C source is the reference.
OPTIONS = [
    ("RECOMP_KEYBOARD", "Enable keyboard controls (player 1)", 1),
    ("MM3_MOD_WIDESCREEN", "Widescreen view and HUD", 1),
    ("MM3_MOD_DRAW_DISTANCE", "Longer view distance", 1),
    ("MM3_MOD_SKY", "Smooth skyline", 1),
    ("MM3_MOD_CAR_LIGHTING", "Improved car lights", 1),
    ("MM3_MOD_LAMP_LIGHTING", "Improved street lamps", 1),
    ("MM3_MOD_WORLD_LIGHTING", "Improved world lighting", 1),
    ("MM3_UNLOCK_ALL_CARS", "Unlock all vehicles", 0),
    ("MM3_MOD_DEV_TOOLS", "Retail developer tools", 0),
    ("MM3_DEV_HUD", "Developer HUD", 0),
    ("MM3_DEBUG_MENU", "Developer menu", 0),
    ("MM3_EXTENDED_CAMERAS", "Extended camera modes", 0),
    ("MM3_E3_PRESET", "E3 demonstration preset", 0),
    ("RECOMP_LOADING_SPINNER", "Loading animation", 1),
    ("MM3_NATIVE_FOG", "Native depth fog", 1),
    ("RECOMP_APU_DSP", "Accurate sound processor", 1),
    ("RECOMP_NET", "System Link networking", 0),
    ("MM3_HUD_NORTH_MARKER", "Compass pins on minimap", 0),
    ("MM3_HUD_RACE_ARROW", "Race direction arrow", 0),
    ("MM3_ROUTE_NAVIGATION", "Route line on the map", 1),
    ("MM3_DRAFTING", "Slipstream drafting", 0),
    ("MM3_ROLL_GUARD", "Rollover assist", 0),
    ("MM3_AI_ALL_CARS", "Circuit AI uses all cars", 1),
    ("MM3_IMPROVED", "Improved look", 1),
    ("MM3_GRAPHICS_MENU", "In-game Graphics menu", 1),
    ("MM3_EXTRA_CIRCUITS", "Extra circuit races", 1),
    ("MM3_EXTRA_CARS", "Extra installed cars", 1),
    ("MM3_DLC_RACE_INFO", "DLC race names and pictures", 1),
    ("MM3_FALLING_TREES", "Trees fall over when hit", 1),
    ("MM3_EXHAUST_TRIANGLES", "Exhaust smoke", 0),
    ("MM3_GAMEPLAY_MODS", "Modded gameplay", 0),
    ("MM3_MENU_QUIT", "Quit in the main menu", 1),
    ("MM3_SAME_CAR", "Opponents match your car", 0),
    ("MM3_FREE_CAMERA", "Free camera", 1),
    ("MM3_PHOTO_MODE", "Photo mode", 1),
    ("MM3_MOD_SKIP_INTRO", "Skip intro movies", 1),
    ("MM3_SOFT_CAR_REFLECTIONS", "Softer car paint reflections", 1),
    ("MM3_RENDERER_VULKAN", "Vulkan renderer (instead of DirectX 11)", 0),
    ("MM3_NO_GPU_WAIT", "Don't wait for the GPU (slow GPUs)", 0),
    ("MM3_PRELOAD_SHADERS", "Load all shaders before playing (Vulkan)", 1),
    ("RECOMP_APU_HRTF", "3D headphone sound (HRTF)", 1),
    ("MM3_UNLOCK_ALL_PAINTS", "Unlock all paint jobs", 0),
    ("MM3_DIGITAL_SPEEDO", "Digital speedometer (as in split screen)", 0),
    ("MM3_UNLOCK_ALL_RACES", "Unlock all races and jobs", 0),
    ("MM3_STUTTER_LOG", "Detailed log (for bug reports)", 0),
]
DEFAULTS = {key: default for key, _label, default in OPTIONS}
DEFAULTS[SKIP_LAUNCHER] = 0
if sys.platform != "win32":
    # Linux and the Steam Deck: on (as in 2.6.3), waiting for the GPU every frame was their slowdown.
    DEFAULTS["MM3_NO_GPU_WAIT"] = 1
LABELS = {key: label for key, label, _default in OPTIONS}

# What the two switches turn off (original_look and xbox_gameplay in the C source).
ORIGINAL_LOOK = ["MM3_MOD_DRAW_DISTANCE", "MM3_MOD_CAR_LIGHTING", "MM3_MOD_LAMP_LIGHTING",
                 "MM3_MOD_WORLD_LIGHTING", "RECOMP_LOADING_SPINNER",
                 "MM3_HUD_NORTH_MARKER", "MM3_GRAPHICS_MENU", "MM3_DLC_RACE_INFO",
                 "MM3_EXHAUST_TRIANGLES", "MM3_MENU_QUIT", "MM3_DIGITAL_SPEEDO"]
XBOX_GAMEPLAY = ["MM3_DRAFTING", "MM3_ROLL_GUARD", "MM3_FALLING_TREES", "MM3_EXTRA_CIRCUITS",
                 "MM3_EXTRA_CARS", "MM3_AI_ALL_CARS", "MM3_UNLOCK_ALL_CARS", "MM3_UNLOCK_ALL_PAINTS", "MM3_UNLOCK_ALL_RACES", "MM3_ROUTE_NAVIGATION",
                 "MM3_HUD_RACE_ARROW", "MM3_MOD_DEV_TOOLS", "MM3_DEV_HUD", "MM3_DEBUG_MENU",
                 "MM3_EXTENDED_CAMERAS", "MM3_E3_PRESET", "MM3_SAME_CAR", "MM3_FREE_CAMERA"]

# key: (label, low, high, default); 0 field-of-view means the retail one.
NUMBERS = {
    "MM3_FOV_DEGREES": ("Field of view, degrees (0 = the game's own, otherwise 60-110)", 0, 110, 0),
    "MM3_DAMAGE_PERCENT": ("Crash damage to every car, % of the Xbox's", 0, 400, 100),
    "MM3_OWN_DAMAGE_PERCENT": ("Extra crash damage to your own car, %", 0, 400, 100),
    "MM3_AI_LEVEL": ("Opponents' difficulty, 0-9 (5 = the Xbox)", 0, 9, 5),
    "RECOMP_NET_PORT": ("System Link port", 1, 65535, 3075),
}
CAMERA_KEYS = [("F1", 0x70), ("F2", 0x71), ("F3", 0x72), ("F4", 0x73), ("F5", 0x74), ("F6", 0x75),
               ("F7", 0x76), ("F8", 0x77), ("Insert", 0x2D), ("Delete", 0x2E), ("Home", 0x24),
               ("End", 0x23), ("Page Up", 0x21), ("Page Down", 0x22)]
PLAYER1 = ["Controller 1 + keyboard", "Controller 1", "Keyboard (arrows)", "Keyboard 2 (WASD)", "Controller 2"]
PLAYER2 = ["Controller 2", "Controller 1", "Keyboard (arrows)", "Keyboard 2 (WASD)"]
PROFILES = ["Balanced", "Quality", "LowPower", "Xbox", "Lowest"]
OWN = "Profile's own"                    # no override: the performance profile (and display.ini) decide
# The Windows launcher's Internal resolution list (src/mm3_display_choices.h): the vertical lines, smallest first.
SCALE_NAMES = {480: " (Xbox)", 720: " (HD)", 1080: " (Full HD)", 1440: " (QHD)", 2160: " (4K)"}
SCALE_VALUES = ["0.5", "0.75", "1", "1.5", "2", "2.25", "3", "4.5"]


def scale_label(value, custom=False):
    lines = round(480 * float(value))
    return f"{lines}p{' (custom)' if custom else SCALE_NAMES.get(lines, '')}"


SCALES = [(OWN, "")] + [(scale_label(value), value) for value in SCALE_VALUES]
FPS_LIMITS = [OWN, "15", "30", "40", "45", "60", "90", "120"]        # mm3_fps_choices, and the profile's own


def scale_choices(saved):
    """SCALES, plus a saved value that no entry offers (a hand-edited or older file), in order, as custom."""
    if not saved or saved in dict(SCALES).values():
        return SCALES
    try:
        value = float(saved)
    except ValueError:
        return SCALES
    if not 0.5 <= value <= 8.0:
        return SCALES
    at = next((i for i, (_name, v) in enumerate(SCALES) if v and float(v) > value), len(SCALES))
    return SCALES[:at] + [(scale_label(saved, custom=True), saved)] + SCALES[at:]


def fps_choices(saved):
    """FPS_LIMITS, plus a saved limit that is not in it."""
    if not saved or saved in FPS_LIMITS or not saved.isdigit():
        return FPS_LIMITS
    at = next((i for i, v in enumerate(FPS_LIMITS) if v.isdigit() and int(v) > int(saved)), len(FPS_LIMITS))
    return FPS_LIMITS[:at] + [saved] + FPS_LIMITS[at:]


def write_text(path, text):
    """UTF-8 with Unix line ends (Path.write_text takes newline= only from Python 3.10; the README says 3.9)."""
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)

HELP = {
    "MM3_IMPROVED": "Off gives the Xbox's own visuals, menus and HUD at the PC's resolution.",
    "MM3_GAMEPLAY_MODS": "Off gives the Xbox's gameplay: every gameplay mod below switches off.",
    "RECOMP_KEYBOARD": "Arrow keys drive, Enter is Start, Escape goes back. Needed without a controller.",
    "RECOMP_NET": "System Link over the network: list the other players below.",
    "MM3_SOFT_CAR_REFLECTIONS": "Untick for the sharp, easier to see car paint reflections of the Xbox.",
}


def read_ini(path=INI):
    """The saved lines of launcher.ini: (managed values by key, every other line)."""
    values, others = {}, []
    try:
        text = Path(path).read_text(encoding="utf-8", errors="replace")
    except OSError:
        return values, others
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped or stripped[0] in "#;[":
            continue
        key, equals, value = stripped.partition("=")
        if not equals:
            continue
        key = key.strip()
        if key in MANAGED:
            values[key] = value.strip()
        elif key not in ("MM3_SETTINGS_REVISION", "MM3_TIME_PERCENT"):  # Time limits: gone, now F10
            others.append(f"{key}={value.strip()}")
    return values, others


MANAGED = set(DEFAULTS) | set(NUMBERS) | set(WHEEL_DEFAULTS) | {
    "MM3_FREE_CAMERA_KEY", "RECOMP_USB_PADS", "RECOMP_NET_PEERS", "MM3_PLAYER1_INPUT", "MM3_PLAYER2_INPUT",
}


def clamp(text, low, high, default):
    try:
        number = int(str(text).strip())
    except ValueError:
        return default
    return number if low <= number <= high else default


def wheel_values(values, *, strict=False):
    """Validate the racing wheel fields, or replace invalid saved fields individually."""
    out = dict(WHEEL_DEFAULTS)
    enabled = str(values.get("MM3_WHEEL_ENABLED", out["MM3_WHEEL_ENABLED"])).strip()
    if enabled in ("0", "1"):
        out["MM3_WHEEL_ENABLED"] = int(enabled)
    elif strict:
        raise ValueError("Turn the racing wheel on or off.")

    device = str(values.get("MM3_WHEEL_DEVICE", out["MM3_WHEEL_DEVICE"])).strip()
    if not device or (re.fullmatch(r"[0-9A-Fa-f]{4}:[0-9A-Fa-f]{4}", device)
                      and all(int(part, 16) for part in device.split(":"))):
        out["MM3_WHEEL_DEVICE"] = device.lower()
    elif strict:
        raise ValueError("Leave the wheel Device ID blank, or enter two nonzero four-digit hex IDs, such as 046d:c24f.")

    axes = [part.strip() for part in str(values.get("MM3_WHEEL_AXES", out["MM3_WHEEL_AXES"])).split(",")]
    indices = [int(axis) for axis in axes if re.fullmatch(r"[+-]?[0-9]{1,3}", axis)]
    if (len(axes) == len(indices) == 3 and all(1 <= abs(axis) <= 255 for axis in indices)
            and len({abs(axis) for axis in indices}) == 3):
        out["MM3_WHEEL_AXES"] = ",".join(str(axis) for axis in indices)
    elif strict:
        raise ValueError("Give steering, accelerator and brake different axis numbers from 1 to 255. Add a minus sign to reverse one.")

    strength = str(values.get("MM3_WHEEL_FFB", out["MM3_WHEEL_FFB"])).strip()
    if re.fullmatch(r"[0-9]{1,2}", strength) and int(strength) <= 25:
        out["MM3_WHEEL_FFB"] = int(strength)
    elif strict:
        raise ValueError("Enter a feedback strength from 0 to 25.")
    return out


def normalise(values):
    """The full, valid set of managed values (what the game's loader accepts) from whatever was typed."""
    out = {}
    for key, default in DEFAULTS.items():
        text = str(values.get(key, default)).strip().lower()
        out[key] = 1 if text in ("1", "true", "on", "nat") else 0 if text in ("0", "false", "off", "") else default
    for key, (_label, low, high, default) in NUMBERS.items():
        number = clamp(values.get(key, default), low, high, default)
        if key == "MM3_FOV_DEGREES" and 0 < number < 60:
            number = 0
        out[key] = number
    camera = clamp(values.get("MM3_FREE_CAMERA_KEY", 0x77), 1, 255, 0x77)
    out["MM3_FREE_CAMERA_KEY"] = camera if camera in [vk for _n, vk in CAMERA_KEYS] else 0x77
    pads = str(values.get("RECOMP_USB_PADS", "auto")).strip().lower()
    out["RECOMP_USB_PADS"] = "auto" if pads.startswith("auto") or pads == "0" else str(clamp(pads, 1, 4, 1))
    # Comma separated, as the game's network code and launcher.ini's loader read them (a space makes the key invalid).
    out["RECOMP_NET_PEERS"] = ",".join(part for part in re.split(r"[,\s]+", str(values.get("RECOMP_NET_PEERS", ""))) if part)
    out["MM3_PLAYER1_INPUT"] = clamp(values.get("MM3_PLAYER1_INPUT", 0), 0, len(PLAYER1) - 1, 0)
    out["MM3_PLAYER2_INPUT"] = clamp(values.get("MM3_PLAYER2_INPUT", 0), 0, len(PLAYER2) - 1, 0)
    out.update(wheel_values(values))
    return out


def write_ini(values, others, path=INI):
    values = normalise(values)
    lines = ["# Midtown Madness 3 launcher preferences", "[launcher]", f"MM3_SETTINGS_REVISION={REVISION}"]
    lines += [f"{key}={values[key]}" for key in DEFAULTS]
    lines += [f"{key}={values[key]}" for key in WHEEL_DEFAULTS]
    for key in NUMBERS:
        lines.append(f"{key}={values[key]}")
    for key in ("MM3_FREE_CAMERA_KEY", "RECOMP_USB_PADS", "RECOMP_NET_PEERS", "MM3_PLAYER1_INPUT", "MM3_PLAYER2_INPUT"):
        lines.append(f"{key}={values[key]}")
    lines += others
    path = Path(path)
    temporary = path.with_name(path.name + ".tmp")
    write_text(temporary, "\n".join(lines) + "\n")
    os.replace(temporary, path)


def read_display(path=EXTRA, *, use_defaults=True):
    defaults = {"MM3_PERFORMANCE_PROFILE": "Xbox", "RECOMP_RENDER_SCALE": "", "RECOMP_FPS_LIMIT": "",
                "RECOMP_FULLSCREEN": "0"}
    saved = {}
    try:
        for line in Path(path).read_text(encoding="utf-8", errors="replace").splitlines():
            key, equals, value = line.partition("=")
            if equals and key.strip() in defaults:
                saved[key.strip()] = value.strip()
    except OSError:
        pass
    if saved.get("MM3_PERFORMANCE_PROFILE") not in PROFILES:
        saved.pop("MM3_PERFORMANCE_PROFILE", None)
    return defaults | saved if use_defaults else saved


def write_display(values, path=EXTRA):
    write_text(path, "".join(f"{key}={value}\n" for key, value in values.items()))


def play(display, *, override=False):
    """Pass display choices to the start script and keep the launch process attached to the game."""
    env = dict(os.environ, MM3_NO_SETTINGS="1", MM3_SETTINGS_APPLIED="1", MM3_LAUNCHER_CONFIG=str(INI))
    for key, value in display.items():
        if key == "RECOMP_FULLSCREEN" and value == "0" and not override:
            continue                  # windowed is the default: display.ini (the in-game menus' choice) decides
        if value and override:
            env[key] = value
        elif value:
            env.setdefault(key, value)
    os.execvpe("sh", ["sh", str(START)], env)


def window():
    import tkinter as tk
    from tkinter import ttk

    saved, others = read_ini()
    values = normalise(saved)
    display = read_display()
    chosen_display = None
    root = tk.Tk()
    root.title("Midtown Madness 3 - Settings")
    root.geometry("900x640")
    root.minsize(760, 520)
    root.columnconfigure(0, weight=1)
    root.rowconfigure(0, weight=1)
    status = tk.StringVar(value="Choose your settings, then press Play.")
    options = {key: tk.IntVar(value=values[key]) for key in DEFAULTS}
    numbers = {key: tk.StringVar(value=str(values[key])) for key in NUMBERS}
    camera = tk.StringVar(value=next(name for name, vk in CAMERA_KEYS if vk == values["MM3_FREE_CAMERA_KEY"]))
    pads = tk.StringVar(value=values["RECOMP_USB_PADS"])
    player1 = tk.StringVar(value=PLAYER1[values["MM3_PLAYER1_INPUT"]])
    player2 = tk.StringVar(value=PLAYER2[values["MM3_PLAYER2_INPUT"]])
    wheel_enabled = tk.IntVar(value=values["MM3_WHEEL_ENABLED"])
    wheel_device = tk.StringVar(value=values["MM3_WHEEL_DEVICE"])
    wheel_axes = [tk.StringVar(value=axis) for axis in values["MM3_WHEEL_AXES"].split(",")]
    wheel_ffb = tk.StringVar(value=str(values["MM3_WHEEL_FFB"]))
    peers = tk.StringVar(value=values["RECOMP_NET_PEERS"])
    profile = tk.StringVar(value=display["MM3_PERFORMANCE_PROFILE"])
    scales = scale_choices(display["RECOMP_RENDER_SCALE"])
    fps_list = fps_choices(display["RECOMP_FPS_LIMIT"])
    scale = tk.StringVar(value=next((n for n, v in scales if v == display["RECOMP_RENDER_SCALE"]), OWN))
    fps = tk.StringVar(value=display["RECOMP_FPS_LIMIT"] if display["RECOMP_FPS_LIMIT"] in fps_list else OWN)
    fullscreen = tk.IntVar(value=1 if display["RECOMP_FULLSCREEN"] == "1" else 0)
    guide = tk.StringVar(value="Point at a setting for its guide.")
    dependent = {key: [] for key in ("MM3_IMPROVED", "MM3_GAMEPLAY_MODS")}
    boxes = {}

    notebook = ttk.Notebook(root)
    notebook.grid(row=0, column=0, sticky="nsew", padx=10, pady=(10, 0))

    def tab(title):
        frame = ttk.Frame(notebook, padding=12)
        notebook.add(frame, text=title)
        return frame

    def check(parent, key, row, column=0, indent=0):
        box = ttk.Checkbutton(parent, text=LABELS[key], variable=options[key], command=refresh)
        box.grid(row=row, column=column, sticky="w", padx=(indent, 16), pady=2)
        box.bind("<Enter>", lambda _e, k=key: guide.set(HELP.get(k, LABELS[k])))
        boxes[key] = box
        return box

    def refresh():
        for master, keys in (("MM3_IMPROVED", ORIGINAL_LOOK), ("MM3_GAMEPLAY_MODS", XBOX_GAMEPLAY)):
            for key in keys:
                boxes[key].state(["!disabled"] if options[master].get() else ["disabled"])

    def section(parent, title, keys, master=None, row=0, column=0):
        box = ttk.LabelFrame(parent, text=title, padding=8)
        box.grid(row=row, column=column, sticky="nsew", padx=6, pady=6)
        for index, key in enumerate(keys):
            check(box, key, index)
        return box

    graphics = tab("Graphics")
    graphics.columnconfigure((0, 1), weight=1)
    top = ttk.LabelFrame(graphics, text="Quality", padding=8)
    top.grid(row=0, column=0, columnspan=2, sticky="ew", padx=6, pady=6)
    for index, (text, variable, choices) in enumerate((
            ("Performance profile", profile, PROFILES),
            ("Internal resolution", scale, [name for name, _v in scales]),
            ("Frame-rate limit", fps, fps_list))):
        ttk.Label(top, text=text).grid(row=0, column=index * 2, sticky="w", padx=(0, 6))
        ttk.Combobox(top, textvariable=variable, values=choices, state="readonly", width=12).grid(
            row=0, column=index * 2 + 1, padx=(0, 18))
    ttk.Checkbutton(top, text="Fullscreen", variable=fullscreen).grid(row=0, column=6)
    look = ttk.LabelFrame(graphics, text="Look", padding=8)
    look.grid(row=1, column=0, sticky="nsew", padx=6, pady=6)
    check(look, "MM3_IMPROVED", 0)
    for index, key in enumerate(("MM3_MOD_WIDESCREEN", "MM3_MOD_SKY", "MM3_NATIVE_FOG", "RECOMP_APU_DSP", "RECOMP_APU_HRTF"), 1):
        check(look, key, index)
    for index, key in enumerate(ORIGINAL_LOOK, 6):
        check(look, key, index, indent=18)
    lower = ttk.LabelFrame(graphics, text="Extras", padding=8)
    lower.grid(row=1, column=1, sticky="nsew", padx=6, pady=6)
    ttk.Label(lower, text=NUMBERS["MM3_FOV_DEGREES"][0], wraplength=330).grid(row=0, column=0, sticky="w")
    ttk.Spinbox(lower, from_=0, to=110, textvariable=numbers["MM3_FOV_DEGREES"], width=6).grid(row=1, column=0, sticky="w")
    check(lower, "MM3_MOD_SKIP_INTRO", 2)
    check(lower, "MM3_PHOTO_MODE", 3)
    check(lower, "MM3_SOFT_CAR_REFLECTIONS", 4)

    gameplay = tab("Gameplay")
    check(gameplay, "MM3_GAMEPLAY_MODS", 0)
    for index, key in enumerate(XBOX_GAMEPLAY, 1):
        check(gameplay, key, index, indent=18)
    gameplay.columnconfigure(1, weight=1)
    sliders = ttk.LabelFrame(gameplay, text="Difficulty", padding=8)
    sliders.grid(row=0, column=1, rowspan=19, sticky="nsew", padx=6)
    for index, key in enumerate(("MM3_DAMAGE_PERCENT", "MM3_OWN_DAMAGE_PERCENT", "MM3_AI_LEVEL")):
        label, low, high, _default = NUMBERS[key]
        ttk.Label(sliders, text=label, wraplength=330).grid(row=index * 2, column=0, sticky="w", pady=(6, 0))
        ttk.Spinbox(sliders, from_=low, to=high, textvariable=numbers[key], width=6).grid(
            row=index * 2 + 1, column=0, sticky="w")
    ttk.Label(sliders, text="Free camera key").grid(row=8, column=0, sticky="w", pady=(10, 0))
    ttk.Combobox(sliders, textvariable=camera, values=[name for name, _v in CAMERA_KEYS], state="readonly",
                 width=10).grid(row=9, column=0, sticky="w")

    players = tab("Controls")
    controls = ttk.LabelFrame(players, text="Players", padding=8)
    controls.pack(fill="x", padx=6, pady=6)
    check(controls, "RECOMP_KEYBOARD", 0)
    for row, (text, variable, choices) in enumerate((("Player 1", player1, PLAYER1), ("Player 2", player2, PLAYER2)), 1):
        ttk.Label(controls, text=text).grid(row=row, column=0, sticky="w")
        ttk.Combobox(controls, textvariable=variable, values=choices, state="readonly", width=26).grid(
            row=row, column=1, sticky="w", pady=2)
    ttk.Label(controls, text="Controllers shown to the game").grid(row=3, column=0, sticky="w")
    ttk.Combobox(controls, textvariable=pads, values=["auto", "1", "2", "3", "4"], state="readonly", width=8).grid(
        row=3, column=1, sticky="w", pady=2)

    wheel = ttk.LabelFrame(players, text="Racing wheel", padding=8)
    wheel.pack(fill="x", padx=6, pady=6)
    ttk.Checkbutton(wheel, text="Enable racing wheel for Player 1", variable=wheel_enabled).grid(
        row=0, column=0, columnspan=6, sticky="w", pady=(0, 4))
    ttk.Label(wheel, text="Device ID (VID:PID)").grid(row=1, column=0, sticky="w", padx=(0, 6))
    ttk.Entry(wheel, textvariable=wheel_device, width=10).grid(row=1, column=1, sticky="w", padx=(0, 16))
    ttk.Label(wheel, text="Feedback strength (%)").grid(row=1, column=2, sticky="w", padx=(0, 6))
    ttk.Spinbox(wheel, from_=0, to=25, textvariable=wheel_ffb, width=5).grid(row=1, column=3, sticky="w")
    for index, (label, axis) in enumerate(zip(("Steering axis", "Accelerator axis", "Brake axis"), wheel_axes)):
        ttk.Label(wheel, text=label).grid(row=2, column=index * 2, sticky="w", padx=(0, 6), pady=4)
        ttk.Entry(wheel, textvariable=axis, width=5).grid(row=2, column=index * 2 + 1, sticky="w", padx=(0, 16))
    ttk.Label(wheel, wraplength=640, justify="left", text=(
        "A detected wheel uses Configuration C automatically. The game's Vibration option controls feedback. "
        "Leave Device ID blank for automatic detection. Axis numbers start at 1. Use a minus sign to reverse an axis."
    )).grid(row=3, column=0, columnspan=6, sticky="w")

    ttk.Label(players, wraplength=700, justify="left", text=(
        "Keys with a keyboard: arrows drive, Z X A S are A B X Y, Enter is Start, Backspace is Back, "
        "Escape is the menu back. Custom key and button bindings: edit the RECOMP_KEYMAP, "
        "MM3_DRIVE_KEYMAP and RECOMP_PADMAP lines of launcher.ini (they are kept when you save here)."
    )).pack(fill="x", padx=6, pady=10)

    network = tab("Network")
    check(network, "RECOMP_NET", 0)
    ttk.Label(network, text=NUMBERS["RECOMP_NET_PORT"][0]).grid(row=1, column=0, sticky="w")
    ttk.Spinbox(network, from_=1, to=65535, textvariable=numbers["RECOMP_NET_PORT"], width=8).grid(row=1, column=1, sticky="w")
    ttk.Label(network, text="Other players (address:port, separated by commas)").grid(row=2, column=0, sticky="w", pady=(10, 0))
    ttk.Entry(network, textvariable=peers, width=60).grid(row=3, column=0, columnspan=3, sticky="w")

    bar = ttk.Frame(root, padding=10)
    bar.grid(row=1, column=0, sticky="ew")
    bar.columnconfigure(0, weight=1)
    ttk.Label(bar, textvariable=guide, wraplength=300).grid(row=0, column=0, sticky="ew")
    ttk.Checkbutton(bar, text="Skip the settings window next time", variable=options[SKIP_LAUNCHER]).grid(
        row=0, column=1, padx=4)

    def collect():
        merged = {key: options[key].get() for key in DEFAULTS}
        merged.update({key: numbers[key].get() for key in NUMBERS})
        merged["MM3_FREE_CAMERA_KEY"] = dict(CAMERA_KEYS)[camera.get()]
        merged["RECOMP_USB_PADS"] = pads.get()
        merged["RECOMP_NET_PEERS"] = peers.get()
        merged["MM3_PLAYER1_INPUT"] = PLAYER1.index(player1.get())
        merged["MM3_PLAYER2_INPUT"] = PLAYER2.index(player2.get())
        merged.update(wheel_values({
            "MM3_WHEEL_ENABLED": wheel_enabled.get(),
            "MM3_WHEEL_DEVICE": wheel_device.get(),
            "MM3_WHEEL_AXES": ",".join(axis.get() for axis in wheel_axes),
            "MM3_WHEEL_FFB": wheel_ffb.get(),
        }, strict=True))
        return merged

    def shown():
        return {"MM3_PERFORMANCE_PROFILE": profile.get(), "RECOMP_RENDER_SCALE": dict(scales)[scale.get()],
                "RECOMP_FPS_LIMIT": "" if fps.get() == OWN else fps.get(),
                "RECOMP_FULLSCREEN": "1" if fullscreen.get() else "0"}

    def save():
        try:
            write_ini(collect(), others)
            write_display(shown())
        except ValueError as error:
            notebook.select(players)
            status.set(str(error))
            return False
        except OSError as error:
            status.set(f"Cannot save settings: {error}")
            return False
        status.set("Saved.")
        return True

    def start():
        nonlocal chosen_display
        if not save():
            return
        chosen_display = shown()
        root.destroy()

    ttk.Button(bar, text="Play", command=start).grid(row=0, column=2, padx=4)
    ttk.Button(bar, text="Save", command=save).grid(row=0, column=3, padx=4)
    ttk.Label(root, textvariable=status, wraplength=700).grid(row=2, column=0, sticky="ew", padx=12, pady=(0, 8))
    refresh()
    root.mainloop()
    return chosen_display


def main(argv):
    show = None
    for arg in argv:
        if arg in ("--play", "--settings"):
            show = arg == "--settings"
    if show is None:
        show = not os.environ.get("MM3_NO_SETTINGS") and not normalise(read_ini()[0])[SKIP_LAUNCHER]
    has_display = sys.platform == "win32" or os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY")
    if show and has_display:
        try:
            display = window()
        except Exception as error:     # no Tk or no display: start the game with the saved choices
            print(f"Settings window unavailable ({error}); starting the game.", file=sys.stderr)
        else:
            if display is not None:
                play(display, override=True)
            return 0
    play(read_display(use_defaults=False))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
