#!/usr/bin/env python3
"""Midtown Madness 3 (native Linux) settings and Play window.

The Windows launcher's mod choices on Linux: the same launcher.ini the game reads
at start-up (src/mm3_launcher_config.c), so a choice made here is what the Windows
launcher would save. Only the keys this window manages are rewritten; any other
line of the file (the key bindings) is kept as it is.

    python3 mm3_settings.py            the window (needs python3-tk)
    python3 mm3_settings.py --play     start the game with the saved choices, no window
"""
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
INI = Path(os.environ.get("MM3_LAUNCHER_CONFIG") or HERE / "launcher.ini")
EXTRA = HERE / "launcher-display.ini"          # this window's own graphics choices
START = HERE / "play-midtown-madness-3.sh"
REVISION = 2                                   # SETTINGS_REVISION in mm3_launcher_config.c

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
]
DEFAULTS = {key: default for key, _label, default in OPTIONS}
if sys.platform != "win32":
    # Linux and the Steam Deck: on (as in 2.6.3), waiting for the GPU every frame was their slowdown.
    DEFAULTS["MM3_NO_GPU_WAIT"] = 1
LABELS = {key: label for key, label, _default in OPTIONS}

# What the two switches turn off (original_look and xbox_gameplay in the C source).
ORIGINAL_LOOK = ["MM3_MOD_DRAW_DISTANCE", "MM3_MOD_CAR_LIGHTING", "MM3_MOD_LAMP_LIGHTING",
                 "MM3_MOD_WORLD_LIGHTING", "RECOMP_LOADING_SPINNER",
                 "MM3_HUD_NORTH_MARKER", "MM3_GRAPHICS_MENU", "MM3_DLC_RACE_INFO",
                 "MM3_EXHAUST_TRIANGLES", "MM3_MENU_QUIT"]
XBOX_GAMEPLAY = ["MM3_DRAFTING", "MM3_ROLL_GUARD", "MM3_FALLING_TREES", "MM3_EXTRA_CIRCUITS",
                 "MM3_EXTRA_CARS", "MM3_AI_ALL_CARS", "MM3_UNLOCK_ALL_CARS", "MM3_ROUTE_NAVIGATION",
                 "MM3_HUD_RACE_ARROW", "MM3_MOD_DEV_TOOLS", "MM3_DEV_HUD", "MM3_DEBUG_MENU",
                 "MM3_EXTENDED_CAMERAS", "MM3_E3_PRESET", "MM3_SAME_CAR", "MM3_FREE_CAMERA"]

# key: (label, low, high, default); 0 field-of-view means the retail one.
NUMBERS = {
    "MM3_FOV_DEGREES": ("Field of view, degrees (0 = the game's own, otherwise 60-110)", 0, 110, 0),
    "MM3_DAMAGE_PERCENT": ("Crash damage to every car, % of the Xbox's", 0, 400, 100),
    "MM3_OWN_DAMAGE_PERCENT": ("Extra crash damage to your own car, %", 0, 400, 100),
    "MM3_TIME_PERCENT": ("Race and job time limits and bonuses, %", 25, 400, 100),
    "MM3_AI_LEVEL": ("Opponents' difficulty, 0-9 (5 = the Xbox)", 0, 9, 5),
    "RECOMP_NET_PORT": ("System Link port", 1, 65535, 3075),
}
CAMERA_KEYS = [("F1", 0x70), ("F2", 0x71), ("F3", 0x72), ("F4", 0x73), ("F5", 0x74), ("F6", 0x75),
               ("F7", 0x76), ("F8", 0x77), ("Insert", 0x2D), ("Delete", 0x2E), ("Home", 0x24),
               ("End", 0x23), ("Page Up", 0x21), ("Page Down", 0x22)]
PLAYER1 = ["Controller 1 + keyboard", "Controller 1", "Keyboard (arrows)", "Keyboard 2 (WASD)", "Controller 2"]
PLAYER2 = ["Controller 2", "Controller 1", "Keyboard (arrows)", "Keyboard 2 (WASD)"]
PROFILES = ["Balanced", "Quality", "LowPower"]
SCALES = [("Profile's own", ""), ("1x", "1"), ("1.5x", "1.5"), ("2x", "2"), ("3x", "3")]
FPS_LIMITS = ["30", "45", "60", "90", "120"]

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
        elif key != "MM3_SETTINGS_REVISION":
            others.append(f"{key}={value.strip()}")
    return values, others


MANAGED = set(DEFAULTS) | set(NUMBERS) | {"MM3_FREE_CAMERA_KEY", "RECOMP_USB_PADS", "RECOMP_NET_PEERS",
                                          "MM3_PLAYER1_INPUT", "MM3_PLAYER2_INPUT"}


def clamp(text, low, high, default):
    try:
        number = int(str(text).strip())
    except ValueError:
        return default
    return number if low <= number <= high else default


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
    out["RECOMP_NET_PEERS"] = " ".join(str(values.get("RECOMP_NET_PEERS", "")).replace(",", " ").split())
    out["MM3_PLAYER1_INPUT"] = clamp(values.get("MM3_PLAYER1_INPUT", 0), 0, len(PLAYER1) - 1, 0)
    out["MM3_PLAYER2_INPUT"] = clamp(values.get("MM3_PLAYER2_INPUT", 0), 0, len(PLAYER2) - 1, 0)
    return out


def write_ini(values, others, path=INI):
    values = normalise(values)
    lines = ["# Midtown Madness 3 launcher preferences", "[launcher]", f"MM3_SETTINGS_REVISION={REVISION}"]
    lines += [f"{key}={values[key]}" for key in DEFAULTS]
    for key in NUMBERS:
        lines.append(f"{key}={values[key]}")
    for key in ("MM3_FREE_CAMERA_KEY", "RECOMP_USB_PADS", "RECOMP_NET_PEERS", "MM3_PLAYER1_INPUT", "MM3_PLAYER2_INPUT"):
        lines.append(f"{key}={values[key]}")
    lines += others
    path = Path(path)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    os.replace(temporary, path)


def read_display(path=EXTRA):
    out = {"MM3_PERFORMANCE_PROFILE": "Balanced", "RECOMP_RENDER_SCALE": "", "RECOMP_FPS_LIMIT": "",
           "RECOMP_FULLSCREEN": "0"}
    try:
        for line in Path(path).read_text(encoding="utf-8").splitlines():
            key, equals, value = line.partition("=")
            if equals and key.strip() in out:
                out[key.strip()] = value.strip()
    except OSError:
        pass
    if out["MM3_PERFORMANCE_PROFILE"] not in PROFILES:
        out["MM3_PERFORMANCE_PROFILE"] = "Balanced"
    return out


def write_display(values, path=EXTRA):
    Path(path).write_text("".join(f"{key}={value}\n" for key, value in values.items()),
                          encoding="utf-8", newline="\n")


def play(display):
    """Start the game through the start script; explicit environment is only what the user picked here."""
    env = dict(os.environ, MM3_NO_SETTINGS="1", MM3_LAUNCHER_CONFIG=str(INI))
    for key, value in display.items():
        if value:
            env[key] = value
    subprocess.Popen(["sh", str(START)], env=env, cwd=str(HERE), start_new_session=True)


def window():
    import tkinter as tk
    from tkinter import ttk

    saved, others = read_ini()
    values = normalise(saved)
    display = read_display()
    root = tk.Tk()
    root.title("Midtown Madness 3 - Settings")
    root.geometry("900x640")
    root.minsize(760, 520)
    status = tk.StringVar(value="Choose your settings, then press Play.")
    options = {key: tk.IntVar(value=values[key]) for key in DEFAULTS}
    numbers = {key: tk.StringVar(value=str(values[key])) for key in NUMBERS}
    camera = tk.StringVar(value=next(name for name, vk in CAMERA_KEYS if vk == values["MM3_FREE_CAMERA_KEY"]))
    pads = tk.StringVar(value=values["RECOMP_USB_PADS"])
    player1 = tk.StringVar(value=PLAYER1[values["MM3_PLAYER1_INPUT"]])
    player2 = tk.StringVar(value=PLAYER2[values["MM3_PLAYER2_INPUT"]])
    peers = tk.StringVar(value=values["RECOMP_NET_PEERS"])
    profile = tk.StringVar(value=display["MM3_PERFORMANCE_PROFILE"])
    scale = tk.StringVar(value=next((n for n, v in SCALES if v == display["RECOMP_RENDER_SCALE"]), SCALES[0][0]))
    fps = tk.StringVar(value=display["RECOMP_FPS_LIMIT"] or "60")
    fullscreen = tk.IntVar(value=1 if display["RECOMP_FULLSCREEN"] == "1" else 0)
    guide = tk.StringVar(value="Point at a setting for its guide.")
    dependent = {key: [] for key in ("MM3_IMPROVED", "MM3_GAMEPLAY_MODS")}
    boxes = {}

    notebook = ttk.Notebook(root)
    notebook.pack(fill="both", expand=True, padx=10, pady=(10, 0))

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
            ("Render scale", scale, [name for name, _v in SCALES]),
            ("Frame-rate limit", fps, FPS_LIMITS))):
        ttk.Label(top, text=text).grid(row=0, column=index * 2, sticky="w", padx=(0, 6))
        ttk.Combobox(top, textvariable=variable, values=choices, state="readonly", width=12).grid(
            row=0, column=index * 2 + 1, padx=(0, 18))
    ttk.Checkbutton(top, text="Fullscreen", variable=fullscreen).grid(row=0, column=6)
    look = ttk.LabelFrame(graphics, text="Look", padding=8)
    look.grid(row=1, column=0, sticky="nsew", padx=6, pady=6)
    check(look, "MM3_IMPROVED", 0)
    for index, key in enumerate(("MM3_MOD_WIDESCREEN", "MM3_MOD_SKY", "MM3_NATIVE_FOG", "RECOMP_APU_DSP"), 1):
        check(look, key, index)
    for index, key in enumerate(ORIGINAL_LOOK, 5):
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
    for index, key in enumerate(("MM3_DAMAGE_PERCENT", "MM3_OWN_DAMAGE_PERCENT", "MM3_TIME_PERCENT", "MM3_AI_LEVEL")):
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
    ttk.Label(players, wraplength=700, justify="left", text=(
        "Keys with a keyboard: arrows drive, Z X A S are A B X Y, Enter is Start, Backspace is Back, "
        "Escape is the menu back. Custom key and button bindings: edit the RECOMP_KEYMAP, "
        "MM3_DRIVE_KEYMAP and RECOMP_PADMAP lines of launcher.ini (they are kept when you save here)."
    )).pack(fill="x", padx=6, pady=10)

    network = tab("Network")
    check(network, "RECOMP_NET", 0)
    ttk.Label(network, text=NUMBERS["RECOMP_NET_PORT"][0]).grid(row=1, column=0, sticky="w")
    ttk.Spinbox(network, from_=1, to=65535, textvariable=numbers["RECOMP_NET_PORT"], width=8).grid(row=1, column=1, sticky="w")
    ttk.Label(network, text="Other players (address:port, space separated)").grid(row=2, column=0, sticky="w", pady=(10, 0))
    ttk.Entry(network, textvariable=peers, width=60).grid(row=3, column=0, columnspan=3, sticky="w")

    bar = ttk.Frame(root, padding=10)
    bar.pack(fill="x")
    ttk.Label(bar, textvariable=guide, wraplength=560).pack(side="left", fill="x", expand=True)

    def collect():
        merged = {key: options[key].get() for key in DEFAULTS}
        merged.update({key: numbers[key].get() for key in NUMBERS})
        merged["MM3_FREE_CAMERA_KEY"] = dict(CAMERA_KEYS)[camera.get()]
        merged["RECOMP_USB_PADS"] = pads.get()
        merged["RECOMP_NET_PEERS"] = peers.get()
        merged["MM3_PLAYER1_INPUT"] = PLAYER1.index(player1.get())
        merged["MM3_PLAYER2_INPUT"] = PLAYER2.index(player2.get())
        return merged

    def shown():
        return {"MM3_PERFORMANCE_PROFILE": profile.get(), "RECOMP_RENDER_SCALE": dict(SCALES)[scale.get()],
                "RECOMP_FPS_LIMIT": fps.get(), "RECOMP_FULLSCREEN": "1" if fullscreen.get() else "0"}

    def save():
        write_ini(collect(), others)
        write_display(shown())
        status.set("Saved.")

    def start():
        try:
            save()
            play(shown())
        except OSError as error:
            status.set(f"Cannot save or start: {error}")
            return
        root.destroy()

    ttk.Button(bar, text="Save", command=save).pack(side="right", padx=4)
    ttk.Button(bar, text="Play", command=start).pack(side="right", padx=4)
    ttk.Label(root, textvariable=status).pack(fill="x", padx=12, pady=(0, 8))
    refresh()
    root.mainloop()


def main(argv):
    if "--play" in argv:
        play(read_display())
        return 0
    try:
        window()
    except Exception as error:     # no Tk or no display: start the game with the saved choices
        print(f"Settings window unavailable ({error}); starting the game.", file=sys.stderr)
        play(read_display())
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
