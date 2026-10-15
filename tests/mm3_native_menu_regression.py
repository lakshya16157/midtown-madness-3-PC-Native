"""Verify generated native Meme menus against the actual retail menu graphs."""
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/native-menu"))
from native_menu import Object, Reference, Resource, named_references, row_button, rows, sid_text
from generate import (ADVANCED, ADVANCED_ROWS, DISPLAY, DISPLAY_ROWS, GRAPHICS_INFOBOX,
                      GRAPHICS_INFO_WIDGET, INFOBOX, INFO_WIDGET, MAIN, MULTIPOP_TEMPLATE, OPTIONS,
                      PAUSE_HUD, PAUSE_OPTIONS, PICKER_COUNTS, PICKERS, PICKER_TITLES, RESOLUTION_OTHER,
                      RESOLUTION_PRIMARY, RESOLUTION_SECONDARY, ROW_DESTINATIONS,
                      TITLE, TITLE_ADVANCED, TITLE_DISPLAY, generate_resources, picker_rows, write_header)


def function_names(obj):
    return [ref.name for node in obj.walk() for ref in node.references()
            if ref.object and ref.object.kind == "Function"]


class NativeMenuTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.archive = ROOT / "game_files/Data/Data_hd.zip"
        if not cls.archive.is_file():
            raise unittest.SkipTest("Retail Data_hd.zip is not installed")
        cls.encoded = generate_resources(cls.archive)
        cls.resources = {name: Resource.decode(data) for name, data in cls.encoded.items()}

    def test_retail_resources_round_trip(self):
        with zipfile.ZipFile(self.archive) as archive:
            for name in (MAIN, OPTIONS, TITLE, INFOBOX, INFO_WIDGET):
                original = archive.read("Menu/Frontend/" + name)
                self.assertEqual(Resource.decode(original).encode(), original)

    def test_reference_span_and_scalar_preservation(self):
        resource = Resource(["MemeFile 2.0"], Object("TransformNode", [
            Reference(), struct.pack("<4f", 50, 90, 540, 210),
            Reference("test/label", Object("IntData", [struct.pack("<i", 7)]))]))
        encoded = resource.encode()
        decoded = Resource.decode(encoded)
        self.assertEqual(decoded.encode(), encoded)
        self.assertEqual(decoded.root.parts[1], struct.pack("<4f", 50, 90, 540, 210))
        self.assertEqual(decoded.root.parts[2].name, "test/label")
        self.assertEqual(decoded.root.parts[2].object.parts, [struct.pack("<i", 7)])

    def test_main_placement_preserves_existing_destinations(self):
        menu = self.resources[MAIN]
        labels = [label for label, _, _ in rows(menu)]
        self.assertEqual(labels, ["SID_CAREER", "SID_SINGLE_PLAYER", "SID_MULTIPLAYER",
                                  "SID_OPTIONS", "SID_MM3_GRAPHICS_TITLE", "SID_PROFILES"])
        # These rows share the original 30-pixel pitch and do not overlap.
        self.assertEqual([rect[1] for _, rect, _ in rows(menu)], [0, 30, 60, 90, 120, 150])
        self.assertEqual(len(named_references(menu, "options/screenOptions")), 1)
        self.assertEqual(len(named_references(menu, "profiles/screenProfiles")), 1)
        graphics = rows(menu)[4][2]
        action_list = row_button(graphics).parts[3].object
        self.assertEqual(action_list.references()[0].object.kind, "CallFunctionAction")
        self.assertEqual(function_names(action_list), ["functions/graphicsBegin"])
        self.assertEqual(action_list.references()[1].object.references()[1].name, DISPLAY)
        # The new help item occupies the same position as its controller focus.
        data = named_references(menu, "main/oftMain")[0].object
        self.assertEqual(sid_text(data.parts[4].object), "SID_MM3_GRAPHICS_HELP")
        for ref in named_references(menu, "main/oftMainIndex"):
            self.assertEqual(struct.unpack("<i", ref.object.parts[0])[0], 6)

    def test_page_rows_and_controller_actions(self):
        for name, specs in ((DISPLAY, DISPLAY_ROWS), (ADVANCED, ADVANCED_ROWS)):
            resource = self.resources[name]
            actual = rows(resource)
            self.assertEqual([label for label, _, _ in actual],
                             ["SID_MM3_GRAPHICS_" + key for key, _ in specs])
            for index, ((_, function), (_, rect, row)) in enumerate(zip(specs, actual)):
                self.assertEqual(rect, (0, index * 26, 530, 26))
                self.assertLessEqual(90 + rect[1] + rect[3], 324)
                action = row_button(row).parts[3].object
                if function:
                    self.assertEqual(function_names(action), ["functions/" + function])
                else:
                    push = action.references()[0].object
                    self.assertEqual(push.kind, "PushPathAction")
                    self.assertEqual(push.references()[1].name, ROW_DESTINATIONS[specs[index][0]])
            # No original Options actions survive in the generated row list.
            self.assertNotIn("functions/movieBonus", function_names(resource.root))
            self.assertNotIn("functions/profileSave", function_names(resource.root))
            content = [obj for obj in resource.root.walk() if obj.kind == "TransformNode" and
                       len(obj.parts) == 3 and isinstance(obj.parts[1], bytes) and
                       struct.unpack("<4f", obj.parts[1])[:3] == (50, 90, 540)]
            self.assertEqual(len(content), 1)
            self.assertEqual(struct.unpack("<4f", content[0].parts[1])[3], len(specs) * 26)

    def test_native_back_and_titles(self):
        for name, discard in ((DISPLAY, True), (ADVANCED, False)):
            resource = self.resources[name]
            back = [obj for obj in resource.root.walk() if obj.kind == "ActionListAction" and
                    any(ref.object and ref.object.kind == "PopPathAction" for ref in obj.references())]
            self.assertEqual(len(back), 1)
            self.assertEqual(function_names(back[0]), ["functions/graphicsDiscard"] if discard else [])
            if discard:
                self.assertEqual(back[0].references()[0].object.kind, "CallFunctionAction")
                self.assertEqual(back[0].references()[1].object.kind, "PopPathAction")
            self.assertTrue(any(obj.kind == "SoundAction" and b"Back.wav" in obj.parts[0]
                                for obj in back[0].walk()))
        for name, key in ((TITLE_DISPLAY, "TITLE"), (TITLE_ADVANCED, "ADVANCED")):
            labels = [sid_text(obj) for obj in self.resources[name].root.walk() if obj.kind == "WSidData"]
            self.assertEqual(labels, ["SID_MM3_GRAPHICS_" + key])

    def test_primary_help_does_not_overlap_settings(self):
        display = self.resources[DISPLAY]
        self.assertEqual(len(named_references(display, GRAPHICS_INFOBOX)), 1)
        self.assertEqual(len(named_references(display, GRAPHICS_INFO_WIDGET)), 1)
        self.assertEqual(named_references(display, INFOBOX), [])
        self.assertEqual(named_references(display, INFO_WIDGET), [])
        box = self.resources[GRAPHICS_INFOBOX]
        transform = [obj for obj in box.root.walk() if obj.kind == "TransformNode"]
        self.assertEqual(len(transform), 1)
        x, y, width, height = struct.unpack("<4f", transform[0].parts[1])
        self.assertEqual((x, y, width, height), (50, 340, 310, 65))
        last_row_bottom = max(90 + rect[1] + rect[3] for _, rect, _ in rows(display))
        self.assertGreaterEqual(y - last_row_bottom, 16)
        self.assertLessEqual(y + height, 405)
        widget = named_references(box, GRAPHICS_INFO_WIDGET)
        self.assertEqual(len(widget), 1)
        self.assertIsNone(widget[0].object)
        page_widget = named_references(display, GRAPHICS_INFO_WIDGET)
        self.assertIsNone(page_widget[0].object)
        standalone = self.resources[GRAPHICS_INFO_WIDGET]
        self.assertEqual(standalone.root.kind, "MmxInfoNode")
        self.assertEqual(len(named_references(standalone, "styles/styleInfoNode")), 1)
        with zipfile.ZipFile(self.archive) as archive:
            self.assertEqual(self.encoded[GRAPHICS_INFO_WIDGET], archive.read("Menu/Frontend/infoBox"))
        # Advanced's shorter list keeps the original retail help positioning.
        self.assertEqual(len(named_references(self.resources[ADVANCED], INFOBOX)), 1)
        self.assertEqual(len(named_references(self.resources[ADVANCED], INFO_WIDGET)), 1)

    def test_setting_rows_open_pickers_without_changing_values(self):
        for name, specs in ((DISPLAY, DISPLAY_ROWS), (ADVANCED, ADVANCED_ROWS)):
            resource = self.resources[name]
            for (key, function), (_, _, row) in zip(specs, rows(resource)):
                action = row_button(row).parts[3].object
                if key in PICKER_COUNTS:
                    self.assertIsNone(function)
                    self.assertEqual(function_names(action), [])
                    self.assertEqual(action.references()[0].object.kind, "PushPathAction")
                    self.assertEqual(action.references()[0].object.references()[1].name, PICKERS[key])
            help_data = named_references(resource, "graphics/" +
                         ("display" if name == DISPLAY else "advanced") + "/oft")[0].object
            self.assertEqual([sid_text(ref.object) for ref in help_data.references() if ref.object],
                             ["SID_MM3_GRAPHICS_HELP_" + key for key, _ in specs])

    def test_exact_choices_and_parent_navigation(self):
        choices = {}
        for key, count in PICKER_COUNTS.items():
            specs = picker_rows(key, RESOLUTION_PRIMARY if key == "RESOLUTION" else None)
            choices[key] = specs[:]
            if key == "RESOLUTION":
                specs.append(("MORE_RESOLUTIONS", None))
            resource = self.resources[PICKERS[key]]
            self.assertEqual([label for label, _, _ in rows(resource)],
                             ["SID_MM3_GRAPHICS_" + choice for choice, _ in specs])
            for (choice, function), (_, _, row) in zip(specs, rows(resource)):
                action = row_button(row).parts[3].object
                if function:
                    self.assertEqual(function_names(action), ["functions/" + function])
                    self.assertEqual([ref.object.kind for ref in action.references() if ref.object],
                                     ["CallFunctionAction", "PopPathAction"])
                else:
                    self.assertEqual(action.references()[0].object.references()[1].name, RESOLUTION_OTHER)
            titles = [sid_text(obj) for obj in self.resources[PICKER_TITLES[key]].root.walk()
                      if obj.kind == "WSidData"]
            self.assertEqual(titles, ["SID_MM3_GRAPHICS_PICKER_" + key])
            self.assertEqual(len(named_references(resource, PICKER_TITLES[key])), 1)
            self.assertEqual(named_references(resource, "titles/titleOptions"), [])
        extra = self.resources[RESOLUTION_OTHER]
        choices["RESOLUTION"] += picker_rows("RESOLUTION", RESOLUTION_SECONDARY)
        self.assertEqual([label for label, _, _ in rows(extra)],
                         ["SID_MM3_GRAPHICS_" + key for key, _ in picker_rows("RESOLUTION", RESOLUTION_SECONDARY)])
        for _, _, row in rows(extra):
            action = row_button(row).parts[3].object
            self.assertEqual([ref.object.kind for ref in action.references() if ref.object],
                             ["CallFunctionAction", "MultiPopPathAction"])
            # The retail two-screen return avoids competing transitions from
            # consecutive single pops. Preserve its proven serialized payload.
            multipop = action.references()[1].object
            self.assertEqual(multipop.references()[0].name, "pathMain")
            self.assertEqual(struct.unpack_from("<I", multipop.parts[1], 17)[0], 2)
            with zipfile.ZipFile(self.archive) as archive:
                retail = Resource.decode(archive.read("Menu/Frontend/" + MULTIPOP_TEMPLATE))
            template = next(node for node in retail.root.walk() if node.kind == "MultiPopPathAction")
            self.assertEqual(multipop.parts, template.parts)
        for key, count in PICKER_COUNTS.items():
            self.assertEqual(set(choices[key]), set(picker_rows(key)))
            self.assertEqual(len(choices[key]), count)

    def test_picker_back_preserves_draft_and_help_geometry(self):
        for name in [*PICKERS.values(), RESOLUTION_OTHER]:
            resource = self.resources[name]
            back = [obj for obj in resource.root.walk() if obj.kind == "ActionListAction" and
                    any(ref.object and ref.object.kind == "PopPathAction" for ref in obj.references()) and
                    not function_names(obj)]
            self.assertEqual(len(back), 1)
            self.assertEqual(sum(bool(ref.object) and ref.object.kind == "PopPathAction"
                                 for ref in back[0].references()), 1)
            self.assertNotIn("functions/graphicsDiscard", function_names(resource.root))
            last_bottom = max(90 + rect[1] + rect[3] for _, rect, _ in rows(resource))
            lower_help = len(rows(resource)) > 8
            self.assertLessEqual(last_bottom, 324 if lower_help else 298)
            self.assertEqual(len(named_references(resource, GRAPHICS_INFOBOX if lower_help else INFOBOX)), 1)
            self.assertEqual(len(named_references(resource, GRAPHICS_INFO_WIDGET if lower_help else INFO_WIDGET)), 1)
            self.assertTrue(any(obj.kind == "WSidData" and sid_text(obj) == "SID_MM3_GRAPHICS_PICKER_HELP"
                                for obj in resource.root.walk()))

    def test_header_and_determinism(self):
        self.assertEqual(len(self.encoded), 7 + 2 * len(PICKER_COUNTS) + 1 + 2)
        self.assertEqual(generate_resources(self.archive), self.encoded)
        with tempfile.TemporaryDirectory(prefix="mm3-native-menu-") as directory:
            header = Path(directory) / "resources.h"
            write_header(self.encoded, header)
            text = header.read_text()
            self.assertIn("MM3_MENU_RESOURCE_COUNT", text)
            for name, data in self.encoded.items():
                self.assertIn('"' + name + '"', text)
                self.assertIn(str(len(data)) + "u", text)

    def test_pause_hud_choices_preserve_retail_controls(self):
        options = self.resources[PAUSE_OPTIONS]
        self.assertEqual([label for label, _, _ in rows(options)],
                         ["SID_SOUND_SETUP", "SID_GAME_SETUP", "SID_MM3_GRAPHICS_HUD"])
        self.assertEqual([rect for _, rect, _ in rows(options)],
                         [(0, index * 30, 500, 30) for index in range(3)])
        for destination in ("menuOptionsSound", "menuOptionsGame", "menuHud"):
            self.assertEqual(len(named_references(options, destination)), 1)
        hud = self.resources[PAUSE_HUD]
        self.assertEqual([label for label, _, _ in rows(hud)],
                         ["SID_MM3_GRAPHICS_HUD_MINIMAP_STOCK", "SID_MM3_GRAPHICS_HUD_MINIMAP_IMPROVED"])
        for (_, rect, row), function in zip(rows(hud), ("hudMinimapStock", "hudMinimapImproved")):
            self.assertEqual(rect[3], 30)
            button = row_button(row)
            self.assertEqual(button.kind, "MmxCenteredSimpleButtonNode")
            action = button.parts[3].object
            self.assertEqual(function_names(action), ["functions/" + function])
            self.assertEqual([ref.object.kind for ref in action.references() if ref.object],
                             ["CallFunctionAction", "PopPathAction", "SoundAction"])
            self.assertEqual(action.references()[1].object.references()[0].name, "pathMenu")
        # The Back event has no host callback, so merely browsing does not save.
        back = [obj for obj in hud.root.walk() if obj.kind == "ActionListAction" and
                any(ref.object and ref.object.kind == "PopPathAction" for ref in obj.references()) and
                not function_names(obj)]
        self.assertEqual(len(back), 1)
        self.assertEqual(len(named_references(hud, "variables/indexHud")), 1)
        self.assertEqual(named_references(hud, "variables/indexOptions"), [])


if __name__ == "__main__":
    unittest.main()
