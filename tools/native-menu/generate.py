"""Generate native MM3 graphics menus without modifying the retail archive."""
from pathlib import Path
import argparse
import json
import struct
import zipfile

from native_menu import (Object, Reference, Resource, actions, call_function, clone,
                         find_row, named_references, row_button, rows, sid, sid_text)

MAIN = "main/screenMain"
OPTIONS = "options/screenOptions"
TITLE = "titles/titleOptions"
MULTIPOP_TEMPLATE = "singlePlayer/career/mission/screenCareerSlides"
DISPLAY = "graphics/screenGraphics"
ADVANCED = "graphics/screenAdvanced"
TITLE_DISPLAY = "titles/titleGraphics"
TITLE_ADVANCED = "titles/titleGraphicsAdvanced"
INFOBOX = "nodeInfoBox"
INFO_WIDGET = "infoBox"
GRAPHICS_INFOBOX = "graphics/nodeInfoBox"
GRAPHICS_INFO_WIDGET = "graphics/infoBox"
# Setting rows open native pickers rather than cycling the current value.
DISPLAY_ROWS = [("RESOLUTION", None), ("MODE", None), ("FPS", None),
                ("VSYNC", None), ("SCALE", None), ("SHOW_FPS", None),
                ("ADVANCED", None), ("APPLY", "graphicsApply"), ("DEFAULTS", "graphicsDefaults")]
ADVANCED_ROWS = [("ASPECT", None), ("MSAA", None), ("ANISO", None), ("VIEW_DISTANCE", None), ("DISTANCE", None),
                 ("MINIMAP", None),
                 ("APPLY", "graphicsApply"), ("DEFAULTS", "graphicsDefaults")]
PICKER_COUNTS = {"RESOLUTION": 12, "MODE": 2, "FPS": 7, "VSYNC": 2, "SCALE": 9,
                 "SHOW_FPS": 2, "ASPECT": 5, "MSAA": 4, "ANISO": 5, "DISTANCE": 5, "MINIMAP": 2,
                 "VIEW_DISTANCE": 5}
PAUSE_OPTIONS = "Pausemenu/menuOptions"
PAUSE_HUD = "Pausemenu/menuHud"
PICKERS = {key: "graphics/picker" + key for key in PICKER_COUNTS}
PICKER_TITLES = {key: "titles/titleGraphics" + key for key in PICKER_COUNTS}
RESOLUTION_OTHER = "graphics/pickerRESOLUTIONOther"
# Put common 4:3 and 16:9 resolutions together, with the less common sizes one
# selection away. Both pages keep the retail focus order and Back behavior.
RESOLUTION_PRIMARY = (0, 2, 6, 7, 9, 11)
RESOLUTION_SECONDARY = (1, 3, 4, 5, 8, 10)
ROW_DESTINATIONS = {**PICKERS, "ADVANCED": ADVANCED, "MORE_RESOLUTIONS": RESOLUTION_OTHER}
# Single player gets a Circuit mode: Checkpoint races 20-29 (tools/races). Its
# own city select, race setup, title and race list lead to the same Checkpoint
# game; src/mm3_circuit_races.c lists only circuits there and hides them from
# Checkpoint, following circuitBegin/circuitEnd.
SP_TYPE = "singlePlayer/screenSinglePlayerType"
SP_SETUP = "singlePlayer/screenSinglePlayerRaceSetup"
CITY_SELECT = "shared/screenCitySelect"
CHECKPOINT_MODE = "singlePlayer/modes/spCheckpointMode"
TITLE_GAME_MODE = "titles/titleGameMode"
CIRCUIT_CITY = "singlePlayer/screenCircuitCitySelect"
CIRCUIT_SETUP = "singlePlayer/screenCircuitRaceSetup"
CIRCUIT_MODE = "singlePlayer/modes/spCircuitMode"
CIRCUIT_TITLE = "titles/titleCircuit"
CIRCUIT_FIRST_RACE = 20


def picker_rows(key, choices=None):
    return [(f"CHOICE_{key}_{index}", f"graphicsSelect{key}{index}")
            for index in (range(PICKER_COUNTS[key]) if choices is None else choices)]


def path_action(template, destination):
    result = clone(template)
    refs = result.object.references()
    if result.object.kind != "PushPathAction" or len(refs) != 2 or refs[0].name != "pathMain":
        raise ValueError("Retail PushPathAction schema changed")
    refs[1].name = destination
    return result


def main_menu(retail):
    resource = clone(retail)
    options = find_row(resource, "strings/choiceOptions")
    profile = find_row(resource, "strings/choiceProfiles")
    if options.parts[0].object is not profile:
        raise ValueError("Options/Profile retail sibling order changed")
    button = row_button(options)
    push = button.parts[3].object.references()[0]
    graphics = clone(options)
    graphics.parts[0] = options.parts[0]
    graphics.parts[1] = struct.pack("<4f", 0, 120, 520, 30)
    graphics_button = row_button(graphics)
    graphics_button.parts[1] = Reference("graphics/choiceGraphics", sid("SID_MM3_GRAPHICS_TITLE"))
    graphics_button.parts[3] = actions([call_function("graphicsBegin"), path_action(push, DISPLAY)])
    options.parts[0] = Reference(object=graphics)
    for obj in (profile, profile.parts[0].object):
        x, y, width, height = struct.unpack("<4f", obj.parts[1])
        obj.parts[1] = struct.pack("<4f", x, y + 30, width, height)
    # Keep the existing help text aligned with the new controller focus order.
    data = named_references(resource, "main/oftMain")[0].object
    if data.kind != "DataListData" or len(data.parts) != 7:
        raise ValueError("Retail main help list changed")
    data.parts.insert(4, Reference(object=sid("SID_MM3_GRAPHICS_HELP")))
    for ref in named_references(resource, "main/oftMainIndex"):
        if ref.object and ref.object.kind == "IntData":
            value, = struct.unpack("<i", ref.object.parts[0])
            if value >= 4:
                ref.object.parts[0] = struct.pack("<i", value + 1)
    # Multiplayer and Track Records share the checkpoint race list: every way
    # on from the main menu ends the single player Circuit list.
    for label, _, row in rows(resource):
        if label in ("SID_CAREER", "SID_SINGLE_PLAYER", "SID_MULTIPLAYER", "SID_OPTIONS"):
            action_list(row_button(row)).parts.insert(0, call_function("circuitEnd"))
    return resource


def replace_object(resource, old, new):
    refs = [ref for obj in resource.root.walk() for ref in obj.references() if ref.object is old]
    if len(refs) != 1:
        raise ValueError("Expected exactly one reference to a row chain")
    refs[0].object = new


def page(retail, row_specs, namespace, title, discard=False, pop_count=0, picker=False, multipop=None):
    resource = clone(retail)
    first = find_row(resource, "options/choiceGameSetup")
    template = clone(first)
    template.parts[0] = Reference()
    push = row_button(template).parts[3]
    back_lists = [obj for obj in resource.root.walk() if obj.kind == "ActionListAction" and
                  any(ref.object and ref.object.kind == "PopPathAction" for ref in obj.references())]
    if len(back_lists) != 1:
        raise ValueError("Retail Back action changed")
    pop = next(ref for ref in back_lists[0].references()
               if ref.object and ref.object.kind == "PopPathAction")
    if pop_count not in (0, 1, 2) or (pop_count == 2 and multipop is None):
        raise ValueError("Picker return requires a proven native pop action")
    selection_return = ([pop] if pop_count == 1 else [multipop] if pop_count == 2 else [])
    next_row = Reference()
    for index, (key, function) in reversed(list(enumerate(row_specs))):
        row = clone(template)
        row.parts[0] = next_row
        row.parts[1] = struct.pack("<4f", 0, index * 26, 530, 26)
        button = row_button(row)
        button.parts[1] = Reference(namespace + "/choice" + key, sid("SID_MM3_GRAPHICS_" + key))
        button.parts[3] = actions([call_function(function)] + [clone(item) for item in selection_return]
                                  if function else [path_action(push, ROW_DESTINATIONS[key])])
        next_row = Reference(object=row)
    replace_object(resource, first, next_row.object)
    # The stock page supplies title/background/help, transitions and native Back.
    for ref in named_references(resource, "titles/titleOptions"):
        ref.name = title
    if len(row_specs) > 8:
        # The nine-row primary page reaches y324. Give it a dedicated lower
        # help box; other frontend screens retain the retail y300 rectangle.
        for ref in named_references(resource, INFOBOX):
            ref.name = GRAPHICS_INFOBOX
        for ref in named_references(resource, INFO_WIDGET):
            ref.name = GRAPHICS_INFO_WIDGET
    for obj in resource.root.walk():
        for ref in obj.references():
            if ref.name.startswith("options/"):
                ref.name = namespace + ref.name[len("options"):]
    help_data = named_references(resource, namespace + "/oft")[0].object
    if help_data.kind != "DataListData":
        raise ValueError("Retail options help list changed")
    help_data.parts = [Reference(object=sid("SID_MM3_GRAPHICS_" +
                       ("PICKER_HELP" if picker else "HELP_" + key)))
                       for key, _ in row_specs] + [Reference()]
    for ref in named_references(resource, namespace + "/oftIndex"):
        if ref.object and ref.object.kind == "IntData":
            ref.object.parts = [struct.pack("<i", 0)]
    # Only the original Back event discards on the primary page. Choice action
    # lists also contain PopPathAction, so never identify Back by class alone.
    back = back_lists[0]
    back.parts = [ref for ref in back.parts
                  if not (isinstance(ref, Reference) and ref.object and
                          ref.object.kind == "CallFunctionAction")]
    if discard:
        back.parts.insert(0, call_function("graphicsDiscard"))
    # Increase the content rectangle for nine rows, preserving the original origin.
    for obj in resource.root.walk():
        if obj.kind == "TransformNode" and len(obj.parts) == 3 and isinstance(obj.parts[1], bytes):
            rect = struct.unpack("<4f", obj.parts[1])
            if rect == (50.0, 90.0, 540.0, 210.0):
                obj.parts[1] = struct.pack("<4f", 50, 90, 540, len(row_specs) * 26)
    return resource


def title_resource(retail, key):
    resource = clone(retail)
    matches = [obj for obj in resource.root.walk() if obj.kind == "WSidData" and sid_text(obj) == "SID_OPTIONS"]
    if len(matches) != 1:
        raise ValueError("Retail options title changed")
    matches[0].parts = sid("SID_MM3_GRAPHICS_" + key).parts
    return resource


def info_box_resource(retail):
    resource = clone(retail)
    transforms = [obj for obj in resource.root.walk() if obj.kind == "TransformNode"]
    if len(transforms) != 1 or len(transforms[0].parts) != 3:
        raise ValueError("Retail information box transform changed")
    transform = transforms[0]
    if struct.unpack("<4f", transform.parts[1]) != (50, 300, 310, 110):
        raise ValueError("Retail information box rectangle changed")
    transform.parts[1] = struct.pack("<4f", 50, 340, 310, 65)
    refs = named_references(resource, INFO_WIDGET)
    if len(refs) != 1 or refs[0].object is not None:
        raise ValueError("Retail information widget binding changed")
    # Both page and positioned node resolve the standalone private widget.
    refs[0].name = GRAPHICS_INFO_WIDGET
    return resource


def pause_hud_resources(retail):
    """Keep the retail pause transitions and controller controls for HUD choices."""
    options = clone(retail)
    option_rows = rows(options)
    if len(option_rows) != 2 or [label for label, _, _ in option_rows] != ["SID_SOUND_SETUP", "SID_GAME_SETUP"]:
        raise ValueError("Retail pause Options rows changed")
    first, last = option_rows[0][2], option_rows[1][2]
    template = clone(first)
    template.parts[0] = Reference()
    hud_entry = clone(template)
    hud_entry.parts[1] = struct.pack("<4f", 0, 60, 500, 30)
    button = row_button(hud_entry)
    button.parts[1] = Reference(object=sid("SID_MM3_GRAPHICS_HUD"))
    push = button.parts[3].object.references()[0]
    push.object.references()[1].name = "menuHud"
    last.parts[0] = Reference(object=hud_entry)
    for obj in options.root.walk():
        if (obj.kind == "TransformNode" and len(obj.parts) == 3 and
                isinstance(obj.parts[1], bytes) and len(obj.parts[1]) == 16 and
                struct.unpack("<4f", obj.parts[1]) == (70, 160, 520, 80)):
            obj.parts[1] = struct.pack("<4f", 70, 160, 520, 110)

    hud = clone(retail)
    first = rows(hud)[0][2]
    pop_actions = [obj for obj in hud.root.walk() if obj.kind == "PopPathAction"]
    if len(pop_actions) != 1 or pop_actions[0].references()[0].name != "pathMenu":
        raise ValueError("Retail pause Back action changed")
    next_row = Reference()
    for index, (key, function) in reversed(list(enumerate([
            ("HUD_MINIMAP_STOCK", "hudMinimapStock"),
            ("HUD_MINIMAP_IMPROVED", "hudMinimapImproved")]))):
        row = clone(template)
        row.parts[0] = next_row
        row.parts[1] = struct.pack("<4f", 0, index * 30, 500, 30)
        button = row_button(row)
        button.parts[1] = Reference(object=sid("SID_MM3_GRAPHICS_" + key))
        button.parts[3] = actions([call_function(function),
                                  Reference(object=clone(pop_actions[0])),
                                  Reference(object=Object("SoundAction", [b"\x0aSelect.wav"]))])
        next_row = Reference(object=row)
    replace_object(hud, first, next_row.object)
    for ref in named_references(hud, "variables/indexOptions"):
        ref.name = "variables/indexHud"
    return {PAUSE_OPTIONS: options, PAUSE_HUD: hud}


def rename_references(resource, old, new):
    refs = named_references(resource, old)
    if len(refs) != 1:
        raise ValueError(f"Expected exactly one reference to {old}: {len(refs)}")
    refs[0].name = new


def action_list(button):
    items = button.parts[3].object
    if not items or items.kind != "ActionListAction":
        raise ValueError("Retail mode button action list changed")
    return items


def single_player_type(retail):
    """Cruise, Blitz, Checkpoint and a fourth row, Circuit."""
    resource = clone(retail)
    by_label = {label: row for label, _, row in rows(resource)}
    if set(by_label) != {"SID_CRUISE", "SID_BLITZ", "SID_CHECKPOINT"}:
        raise ValueError("Retail single player mode rows changed")
    checkpoint = by_label["SID_CHECKPOINT"]
    if checkpoint.parts[0].object is not None:
        raise ValueError("Checkpoint is no longer the last mode row")
    circuit = clone(checkpoint)
    circuit.parts[0] = Reference()
    circuit.parts[1] = struct.pack("<4f", 0, 90, 250, 30)
    button = row_button(circuit)
    button.parts[1] = Reference(object=sid("SID_MM3_CIRCUIT"))
    items = action_list(button)
    pushes = [ref.object for ref in items.references() if ref.object and ref.object.kind == "PushPathAction"]
    if len(pushes) != 1 or pushes[0].references()[1].name != CITY_SELECT:
        raise ValueError("Retail Checkpoint row no longer opens the city select")
    pushes[0].references()[1].name = CIRCUIT_CITY
    items.parts.insert(0, call_function("circuitBegin"))
    checkpoint.parts[0] = Reference(object=circuit)
    for label in ("SID_CRUISE", "SID_BLITZ", "SID_CHECKPOINT"):
        action_list(row_button(by_label[label])).parts.insert(0, call_function("circuitEnd"))
    frames = [obj for obj in resource.root.walk() if obj.kind == "TransformNode" and len(obj.parts) == 3 and
              isinstance(obj.parts[1], bytes) and struct.unpack("<4f", obj.parts[1]) == (50, 90, 270, 110)]
    if len(frames) != 1:
        raise ValueError("Retail mode list rectangle changed")
    frames[0].parts[1] = struct.pack("<4f", 50, 90, 270, 140)
    helps = [obj for obj in resource.root.walk() if obj.kind == "DataListData" and
             [sid_text(ref.object) if ref.object.kind == "WSidData" else ref.object.kind
              for ref in obj.references() if ref.object] ==
             ["SID_OFT_SRT_CRUISE", "SID_OFT_SRT_BLITZ", "SID_OFT_SRT_CHECKPOINT"]]
    if len(helps) != 1 or helps[0].parts[-1] != Reference():
        raise ValueError("Retail mode help list changed")
    helps[0].parts.insert(len(helps[0].parts) - 1, Reference(object=sid("SID_MM3_OFT_SRT_CIRCUIT")))
    return resource


def on_show(resource, function):
    """Call a function first in the screen's on-show actions (title, background...)."""
    lists = [obj for obj in resource.root.walk() if obj.kind == "ActionListAction" and
             any(ref.object and ref.object.kind == "SetPathAction" and
                 any(inner.name == "pathBackground" for inner in ref.object.references())
                 for ref in obj.references())]
    if len(lists) != 1:
        raise ValueError("Retail screen on-show actions changed")
    lists[0].parts.insert(0, call_function(function))
    return resource


def circuit_screen(retail, renames):
    resource = clone(retail)
    for old, new in renames:
        rename_references(resource, old, new)
    # Back from a race the screen comes up again without its mode row.
    return on_show(resource, "circuitBegin")


def circuit_title(retail):
    resource = clone(retail)
    matches = [obj for obj in resource.root.walk() if obj.kind == "WSidData" and sid_text(obj) == "SID_CHECKPOINT"]
    if len(matches) != 1:
        raise ValueError("Retail game mode title changed")
    matches[0].parts = sid("SID_MM3_CIRCUIT").parts
    return resource


def circuit_mode(retail):
    """The Checkpoint race list, starting at the first circuit."""
    resource = clone(retail)
    buttons = [obj for obj in resource.root.walk() if obj.kind == "MmxCheckpointRaceButtonNode"]
    if len(buttons) != 1:
        raise ValueError("Retail checkpoint race button changed")
    refs = buttons[0].references()
    # Variable, Step Value, Min Value, Max Value (SubData: races - 1).
    last = [i for i, ref in enumerate(refs) if ref.object and ref.object.kind == "SubData"]
    if len(last) != 1 or last[0] < 3:
        raise ValueError("Retail checkpoint race range changed")
    variable, first = refs[last[0] - 3], refs[last[0] - 1]
    if (variable.name != "variables/varCheckpointRace" or first.name or not first.object or
            first.object.kind != "IntData" or first.object.parts != [struct.pack("<i", 0)]):
        raise ValueError("Retail checkpoint race range changed")
    first.object.parts = [struct.pack("<i", CIRCUIT_FIRST_RACE)]
    # The picture strip counts its boxes from the first circuit
    # (src/mm3_circuit_races.c walks its pictures from there).
    strips = [obj for obj in resource.root.walk() if obj.kind == "MmxRaceListNode"]
    if len(strips) != 1:
        raise ValueError("Retail checkpoint race strip changed")
    races = [ref for ref in strips[0].references() if ref.name == "variables/varCheckpointRace"]
    if len(races) != 1:
        raise ValueError("Retail checkpoint race strip changed")
    offset = clone(refs[last[0]].object)
    offset_refs = offset.references()
    offset_refs[0].name, offset_refs[0].object = races[0].name, races[0].object
    offset_refs[1].object.parts = [struct.pack("<i", CIRCUIT_FIRST_RACE)]
    races[0].name, races[0].object = "", offset
    return resource


def circuit_resources(retail):
    return {SP_TYPE: single_player_type(retail[SP_TYPE]),
            SP_SETUP: on_show(clone(retail[SP_SETUP]), "circuitEnd"),
            CITY_SELECT: on_show(clone(retail[CITY_SELECT]), "circuitEnd"),
            CIRCUIT_CITY: circuit_screen(retail[CITY_SELECT], [(SP_SETUP, CIRCUIT_SETUP),
                                                               (TITLE_GAME_MODE, CIRCUIT_TITLE)]),
            CIRCUIT_SETUP: circuit_screen(retail[SP_SETUP], [(CHECKPOINT_MODE, CIRCUIT_MODE),
                                                             (TITLE_GAME_MODE, CIRCUIT_TITLE)]),
            CIRCUIT_TITLE: circuit_title(retail[TITLE_GAME_MODE]),
            CIRCUIT_MODE: circuit_mode(retail[CHECKPOINT_MODE])}


def generate_resources(archive):
    with zipfile.ZipFile(archive) as files:
        original = {name: files.read("Menu/Frontend/" + name)
                    for name in (MAIN, OPTIONS, TITLE, INFOBOX, INFO_WIDGET, MULTIPOP_TEMPLATE,
                                 SP_TYPE, SP_SETUP, CITY_SELECT, CHECKPOINT_MODE, TITLE_GAME_MODE)}
        pause_options = Resource.decode(files.read("Menu/" + PAUSE_OPTIONS))
    decoded = {name: Resource.decode(data) for name, data in original.items()}
    multipops = [obj for obj in decoded[MULTIPOP_TEMPLATE].root.walk() if obj.kind == "MultiPopPathAction"]
    if len(multipops) != 1 or len(multipops[0].parts) != 2:
        raise ValueError("Retail career multi-pop action changed")
    multipop = multipops[0]
    if (multipop.parts[0].name != "pathMain" or not isinstance(multipop.parts[1], bytes) or
            len(multipop.parts[1]) != 21 or struct.unpack_from("<I", multipop.parts[1], 17)[0] != 2):
        raise ValueError("Retail career multi-pop count changed")
    resources = {MAIN: main_menu(decoded[MAIN]),
                 DISPLAY: page(decoded[OPTIONS], DISPLAY_ROWS, "graphics/display", TITLE_DISPLAY, True),
                 ADVANCED: page(decoded[OPTIONS], ADVANCED_ROWS, "graphics/advanced", TITLE_ADVANCED, False),
                 TITLE_DISPLAY: title_resource(decoded[TITLE], "TITLE"),
                 TITLE_ADVANCED: title_resource(decoded[TITLE], "ADVANCED"),
                 GRAPHICS_INFOBOX: info_box_resource(decoded[INFOBOX]),
                 GRAPHICS_INFO_WIDGET: clone(decoded[INFO_WIDGET])}
    resources.update(pause_hud_resources(pause_options))
    resources.update(circuit_resources(decoded))
    for key in PICKER_COUNTS:
        specs = picker_rows(key, RESOLUTION_PRIMARY if key == "RESOLUTION" else None)
        if key == "RESOLUTION":
            specs.append(("MORE_RESOLUTIONS", None))
        resources[PICKERS[key]] = page(decoded[OPTIONS], specs, "graphics/select" + key,
                                       PICKER_TITLES[key], pop_count=1, picker=True)
        resources[PICKER_TITLES[key]] = title_resource(decoded[TITLE], "PICKER_" + key)
    resources[RESOLUTION_OTHER] = page(decoded[OPTIONS], picker_rows("RESOLUTION", RESOLUTION_SECONDARY),
                                       "graphics/selectRESOLUTIONOther", PICKER_TITLES["RESOLUTION"],
                                       pop_count=2, picker=True, multipop=Reference(object=clone(multipop)))
    return {name: resource.encode() for name, resource in resources.items()}


def write_header(resources, destination):
    lines = ["/* Generated by tools/native-menu/generate.py; retail assets remain unchanged. */",
             "#ifndef MM3_MENU_RESOURCES_H", "#define MM3_MENU_RESOURCES_H",
             "typedef struct MM3MenuResource { const char *name; const unsigned char *data; unsigned size; } MM3MenuResource;"]
    for index, (name, data) in enumerate(resources.items()):
        lines.append(f"static const unsigned char mm3_menu_resource_{index}[] = {{")
        lines.extend("    " + ",".join(f"0x{x:02X}" for x in data[pos:pos + 24]) + ","
                     for pos in range(0, len(data), 24))
        lines.append("};")
    lines.append("static const MM3MenuResource mm3_menu_resources[] = {")
    for index, (name, data) in enumerate(resources.items()):
        lines.append(f'    {{ "{name}", mm3_menu_resource_{index}, {len(data)}u }},')
    lines.extend(["};", "#define MM3_MENU_RESOURCE_COUNT (sizeof(mm3_menu_resources) / sizeof(mm3_menu_resources[0]))", "#endif", ""])
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text("\n".join(lines), encoding="ascii")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--header", type=Path, required=True)
    parser.add_argument("--resources-dir", type=Path)
    args = parser.parse_args()
    resources = generate_resources(args.archive)
    write_header(resources, args.header)
    output = args.resources_dir or args.header.parent
    for name, data in resources.items():
        path = output / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    manifest = {name: {"size": len(data), "rows": [label for label, _, _ in rows(Resource.decode(data))]}
                for name, data in resources.items()}
    (args.header.parent / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Generated {len(resources)} native Meme resources and {args.header}")


if __name__ == "__main__":
    main()
