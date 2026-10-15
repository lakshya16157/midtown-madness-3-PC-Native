"""Hidden Tk checks for original/DLC references and editable added-car drafts."""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("car_workshop_ui_test", ROOT / "tools/car-mods/editor.py")
editor = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(editor)


def original(ident, alias, display_name, mass, torque):
    """A self-contained donor fixture; no live game archives are consulted."""
    text = ("Begin(CarInfo)\r\n"
            f"ID {ident}\r\nPAINTJOBNAME {alias}\r\n"
            "TOPSPEED 8\r\nACCELERATION 8\r\nMASS 3\r\nHANDLING 6\r\nDURABILITY 2\r\n"
            "COLOR 0 0xa3a9af 0xa3a9af\r\nCOLOR 1 0xffffff 0xffffff\r\n"
            "SKIN 1 0xdeebef 0x5a718c\r\nLOCKED\r\nEnd(CarInfo)\r\n"
            "Begin(CarBody)\r\n"
            f"Mass {mass}\r\nTorqueScale {torque}\r\n"
            "BrakeTorque 4000\r\nWheelDrive 1\r\nNGears 5\r\n"
            "GearRatioVector -3.3 2.90 1.77 1.21 0.92 0.81\r\n"
            "MinRPM 1000\r\nCutRPM 8000\r\nMaxRPM 10000\r\n"
            "End(CarBody)\r\n"
            "Begin(CarWheel0)\r\nSpringK 70000\r\nEnd(CarWheel0)\r\n"
            "Begin(CarCamera)\r\nHoodPos 0 0.727769 0.547450\r\n"
            "SpeedometerMaxKmh 620.0\r\nSpeedometerMaxMph 500.0\r\nEnd(CarCamera)\r\n"
            "Begin(CarCameraSS)\r\nSpeedometerMaxKmh 620.0\r\n"
            "SpeedometerMaxMph 500.0\r\nEnd(CarCameraSS)\r\n")
    return {"id": ident, "alias": alias, "display_name": display_name,
            "sections": editor.original_stats.parse_sections(text),
            "ratings": {"TOPSPEED": 8, "ACCELERATION": 8, "MASS": 3, "HANDLING": 6, "DURABILITY": 2},
            "physics": {"Mass": mass, "TorqueScale": torque, "BrakeTorque": 4000, "WheelDrive": 1,
                        "GearRatioVector": [-3.3, 2.90, 1.77, 1.21, .92, .81]},
            "raw_text": text, "source": f"fixture/Data_hd.zip :: Cars/Tweak/{alias}.txt"}


# Complete retail-style driving values, so a fixture car can serve as a base.
FULL_DRIVING = {
    "NGears": "5", "GearRatioVector": "-3.3 2.90 1.77 1.21 0.92 0.81\t", "WheelDrive": "1",
    "Mass": "1380", "TorqueScale": "700", "WheelRatio": "4.0", "WheelRadius": "0.33",
    "TurboSize": "0.0", "TurboSpinup": "0.0", "AirKZY": "1.5", "AirKZZ": "0.03",
    "MaxTorqueRPM": "6500.0", "MaxTurboTorqueRPM": "6750.0", "MinRPMGearChange": "4000.0",
    "BrakeTorque": "4000.0", "MinRPM": "1000.0", "MaxRPM": "10000.0", "CutRPM": "8000.0",
    "TimeChangeGear": "0.4", "TimeChangeGearFR": "0.15", "GearStatusDownGearFactor": "0.75",
    "HandBrakeTorque": "7000.0", "TorqueScaleFactor": "1.125", "LatFrictionFactor": "2.0",
    "SteeringEndSpeed": "300.0",
}


def full_original(ident, alias, display_name, physics_id=None, **driving):
    """A fixture car with every driving value; keyword arguments replace some."""
    body = {**FULL_DRIVING, **{key: str(value) for key, value in driving.items()}}
    text = ("Begin(CarInfo)\r\n"
            f"ID {ident}\r\n" + (f"PHYSICSID {physics_id}\r\n" if physics_id else "") +
            f"PAINTJOBNAME {alias}\r\n"
            "TOPSPEED 7\r\nACCELERATION 6\r\nMASS 5\r\nHANDLING 4\r\nDURABILITY 3\r\n"
            "COLOR 0 0xa3a9af 0xa3a9af\r\nLOCKED\r\nEnd(CarInfo)\r\n"
            "Begin(CarBody)\r\n\tNWheels\t4\r\n" +
            "".join(f"\t{key}\t{value}\r\n" for key, value in body.items()) +
            "End(CarBody)\r\n"
            "Begin(CarWheel0)\r\nSpringK 70000\r\nEnd(CarWheel0)\r\n"
            "Begin(CarCamera)\r\nHoodPos 0 0.727769 0.547450\r\n"
            "SpeedometerMaxKmh 620.0\r\nSpeedometerMaxMph 500.0\r\nEnd(CarCamera)\r\n")
    sections = editor.original_stats.parse_sections(text)
    physics = {key: editor.original_stats.typed_value(value)
               for section in sections if section["name"] == "CarBody" for key, value in section["entries"]}
    return {"id": ident, "alias": alias, "display_name": display_name, "sections": sections,
            "ratings": {"TOPSPEED": 7, "ACCELERATION": 6, "MASS": 5, "HANDLING": 4, "DURABILITY": 3},
            "physics": physics, "raw_text": text, "source": f"fixture/Data_hd.zip :: Cars/Tweak/{alias}.txt"}


class WorkshopFixture(unittest.TestCase):
    """Builds a hidden workshop over fixture cars; subclasses hold the tests."""

    def fixture_originals(self):
        return [original(18, "Lotus", "Lotus Esprit V8", 1380, 700),
                original(11, "Viper", "Dodge Viper", 1600, 1000)]

    def fixture_dlc(self):
        return [original(201, "car201", "DLC Racer", 1050, 850),
                original(215, "car215", "DLC Heavyweight", 2200, 900)]

    def fixture_manifest(self, config):
        """Return a changed manifest to start from, or None for the shipped one."""
        return None

    def setUp(self):
        try:
            self.root = editor.tk.Tk()
            self.root.withdraw()
        except editor.tk.TclError as error:
            self.skipTest(f"Tk is unavailable: {error}")
        self.addCleanup(self.destroy_root)
        # Tk prints and swallows errors raised in traces and bindings; record
        # them so a broken callback fails the test that triggered it.
        self.callback_errors = []
        self.root.report_callback_exception = lambda *details: self.callback_errors.append(details)
        self.addCleanup(lambda: self.assertEqual(self.callback_errors, [], "A Tk callback raised an exception"))
        temporary = tempfile.TemporaryDirectory(prefix="mm3-workshop-ui-")
        self.addCleanup(temporary.cleanup)
        self.path = Path(temporary.name) / "draft settings.json"
        shipped = (ROOT / "tools/car-mods/cars.json").read_bytes()
        changed = self.fixture_manifest(json.loads(shipped))
        if changed is None:
            self.path.write_bytes(shipped)
        else:
            self.path.write_text(json.dumps(changed, indent=2) + "\n", encoding="utf-8")
        self.disk_before = self.path.read_bytes()
        self.originals = self.fixture_originals()
        self.dlc = self.fixture_dlc()
        for car in self.dlc:
            car["source"] = f"fixture/TDATA/4d53002a/$C/pack/Cars/Tweak/{car['alias']}.txt"
        reader = patch.object(editor.original_stats, "load_original_cars", return_value=copy.deepcopy(self.originals))
        reader.start()
        self.addCleanup(reader.stop)
        dlc_reader = patch.object(editor.original_stats, "load_dlc_cars", return_value=copy.deepcopy(self.dlc))
        dlc_reader.start()
        self.addCleanup(dlc_reader.stop)
        errors = patch.object(editor.messagebox, "showerror")
        self.error_dialog = errors.start()
        self.addCleanup(errors.stop)
        self.app = editor.CarEditor(self.root, self.path, Path(temporary.name) / "game")
        self.root.update()
        self.baseline = copy.deepcopy(self.app.model.config)

    def destroy_root(self):
        if getattr(self, "root", None) is not None:
            # Prevent deferred editor polls from firing after their widgets
            # have been destroyed when the next test creates a Tcl window.
            for timer in self.root.tk.call("after", "info"):
                self.root.after_cancel(timer)
            self.root.destroy()
            self.root = None

    def choose_original(self, ident):
        index = next(index for index in self.app.visible_original
                     if self.app.original_cars[index]["id"] == ident)
        self.app.original_listbox.selection_clear(0, "end")
        self.app.original_listbox.selection_set(self.app.visible_original.index(index))
        self.app.select_original()
        self.root.update()

    def choose_added(self, ident):
        index = next(index for index in self.app.visible_added
                     if self.app.model.config["cars"][index]["id"] == ident)
        self.app.listbox.selection_clear(0, "end")
        self.app.listbox.selection_set(self.app.visible_added.index(index))
        self.app.select_car()
        self.root.update()

    def choose_dlc(self, ident):
        index = next(index for index in self.app.visible_dlc
                     if self.app.original_cars[index]["id"] == ident)
        self.app.dlc_listbox.selection_clear(0, "end")
        self.app.dlc_listbox.selection_set(self.app.visible_dlc.index(index))
        self.app.select_dlc()
        self.root.update()

    def row(self, section, key):
        tree = self.app.stats_view.tree
        matches = [tree.item(item, "values") for item in tree.get_children()
                   if tree.set(item, "section") == section and tree.set(item, "setting") == key]
        self.assertEqual(len(matches), 1, f"Expected one {section}.{key} row, found {matches!r}")
        return matches[0]


class CarWorkshopUITest(WorkshopFixture):
    def test_original_browsing_is_read_only_and_editing_tabs_are_disabled(self):
        for ident in (18, 11):
            with self.subTest(ident=ident):
                self.choose_original(ident)
                self.assertEqual(self.app.stats_view.car["id"], ident)
                self.assertEqual(self.app.notebook.select(), str(self.app.tabs["stats"]))
                for key in ("specs", "driving", "gears", "advanced", "model"):
                    self.assertEqual(self.app.notebook.tab(self.app.tabs[key], "state"), "disabled")
                # Form variables still belong to the added car draft. Their
                # stale contents must never be collected as original tuning.
                self.app.physics_vars["TorqueScale"].set("999999")
                self.app._collect()
                self.assertEqual(self.app.model.config, self.baseline)
                self.assertEqual(self.path.read_bytes(), self.disk_before)
        self.error_dialog.assert_not_called()

    def test_draft_edit_survives_original_reference_and_return_to_added_car(self):
        added = self.app.model.config["cars"][self.app.current]
        ident = added["id"]
        self.app.physics_vars["TorqueScale"].set("777")
        self.choose_original(18)
        draft = copy.deepcopy(self.app.model.config)
        self.assertEqual(draft["cars"][self.app.current]["physics"]["TorqueScale"], 777)
        self.assertEqual(self.app.model.saved, self.baseline)
        self.assertEqual(self.path.read_bytes(), self.disk_before)
        self.choose_original(11)
        self.assertEqual(self.app.model.config, draft)
        self.choose_added(ident)
        self.assertIsNone(self.app.original_current)
        self.assertEqual(self.app.physics_vars["TorqueScale"].get(), "777")
        for key in ("specs", "driving", "gears", "advanced", "model"):
            self.assertEqual(self.app.notebook.tab(self.app.tabs[key], "state"), "normal")
        self.assertEqual(self.app.model.config, draft)
        self.assertEqual(self.path.read_bytes(), self.disk_before)
        self.error_dialog.assert_not_called()

    def test_catalog_search_maps_filtered_rows_to_correct_added_and_original_ids(self):
        self.app.search_var.set("9011")
        self.assertEqual(self.app.listbox.size(), 1)
        self.app.listbox.selection_set(0)
        self.app.select_car()
        self.assertEqual(self.app.model.config["cars"][self.app.current]["id"], 9011)
        self.app.search_var.set("Dodge")
        self.assertEqual(self.app.original_listbox.size(), 1)
        self.app.original_listbox.selection_set(0)
        self.app.select_original()
        self.assertEqual(self.app.original_cars[self.app.original_current]["id"], 11)
        self.app.search_var.set("18")
        self.assertEqual(self.app.original_listbox.size(), 1)
        self.app.original_listbox.selection_set(0)
        self.app.select_original()
        self.assertEqual(self.app.original_cars[self.app.original_current]["id"], 18)
        self.app.search_var.set("not a car")
        self.assertEqual(self.app.listbox.size(), 0)
        self.assertEqual(self.app.original_listbox.size(), 0)
        self.assertEqual(self.app.dlc_listbox.size(), 0)
        self.app.search_var.set("")
        self.assertEqual(self.app.listbox.size(), len(self.baseline["cars"]))
        self.assertEqual(self.app.original_listbox.size(), len(self.originals))
        self.assertEqual(self.app.dlc_listbox.size(), len(self.dlc))
        self.assertEqual(self.app.model.config, self.baseline)
        self.error_dialog.assert_not_called()

    def test_catalog_tabs_show_filtered_counts_and_keep_dirty_markers_when_cleared(self):
        self.choose_dlc(201)
        selected = self.app.original_current
        self.app.search_var.set("Senna")
        added_total = len(self.baseline["cars"])
        self.assertEqual(self.app.catalog_tabs.tab(0, "text"), f"Added (2/{added_total})")
        self.assertEqual(self.app.catalog_tabs.tab(1, "text"), f"Original (0/{len(self.originals)})")
        self.assertEqual(self.app.catalog_tabs.tab(2, "text"), f"DLC (0/{len(self.dlc)})")
        self.assertEqual(self.app.catalog_tabs.index(self.app.catalog_tabs.select()), 2)
        self.assertEqual(self.app.original_current, selected)
        self.assertEqual(self.app.model.config, self.baseline)

        self.choose_added(9011)
        self.app.physics_vars["Mass"].set("999")
        self.app.flush_validation()
        self.app._update_dirty_markers()
        self.assertEqual(self.app.catalog_tabs.tab(0, "text"), f"Added (2/{added_total})  •")
        self.assertEqual(self.app.catalog_tabs.tab(1, "text"), f"Original (0/{len(self.originals)})")
        self.assertEqual(self.app.catalog_tabs.tab(2, "text"), f"DLC (0/{len(self.dlc)})")

        self.app.search_var.set("")
        self.assertEqual(self.app.catalog_tabs.tab(0, "text"), f"Added ({added_total})  •")
        self.assertEqual(self.app.catalog_tabs.tab(1, "text"), f"Original ({len(self.originals)})")
        self.assertEqual(self.app.catalog_tabs.tab(2, "text"), f"DLC ({len(self.dlc)})")
        self.assertEqual(self.app.model.config["cars"][self.app.current]["id"], 9011)
        self.assertEqual(self.app.model.config, self.baseline)
        self.assertEqual(self.path.read_bytes(), self.disk_before)
        self.error_dialog.assert_not_called()

    def test_dlc_browsing_has_separate_catalog_and_read_only_identity(self):
        self.assertEqual(self.app.retail_count, len(self.originals))
        self.assertEqual(self.app.original_listbox.size(), len(self.originals))
        self.assertEqual(self.app.dlc_listbox.size(), len(self.dlc))
        self.assertEqual(self.app.visible_dlc,
                         list(range(len(self.originals), len(self.originals) + len(self.dlc))))
        for car in self.dlc:
            with self.subTest(ident=car["id"]):
                self.choose_dlc(car["id"])
                self.assertGreaterEqual(self.app.original_current, self.app.retail_count)
                self.assertEqual(self.app.stats_view.car["id"], car["id"])
                self.assertEqual(self.app.catalog_tabs.index(self.app.catalog_tabs.select()), 2)
                self.assertEqual(self.app.notebook.select(), str(self.app.tabs["stats"]))
                identity = self.app.identity_label.cget("text")
                self.assertTrue(identity.startswith("DLC car"), identity)
                self.assertIn(f"ID {car['id']}", identity)
                self.assertIn("Read-only reference", identity)
                for key in ("specs", "driving", "gears", "advanced", "model"):
                    self.assertEqual(self.app.notebook.tab(self.app.tabs[key], "state"), "disabled")
                # Stale added-car form values cannot become DLC overrides.
                self.app.physics_vars["TorqueScale"].set("999999")
                self.app._collect()
                self.assertEqual(self.app.model.config, self.baseline)
                self.assertEqual(self.path.read_bytes(), self.disk_before)
        self.error_dialog.assert_not_called()

    def test_filtered_dlc_selection_maps_global_index_and_can_compare_dlc_values(self):
        self.app.search_var.set("215")
        self.assertEqual(self.app.dlc_listbox.size(), 1)
        self.assertEqual(self.app.original_listbox.size(), 0)
        self.assertEqual(self.app.visible_dlc, [self.app.retail_count + 1])
        self.app.dlc_listbox.selection_set(0)
        self.app.select_dlc()
        self.root.update()
        self.assertEqual(self.app.original_cars[self.app.original_current]["id"], 215)
        self.assertEqual(self.row("CarBody", "Mass")[2:], ("2200", "1380"))
        self.assertEqual(self.row("CarBody", "TorqueScale")[2:], ("900", "700"))
        self.assertIn("DLC Racer", self.app.stats_view.reference_combo.cget("values"))
        self.app.stats_view.reference_var.set("DLC Racer")
        self.app.stats_view.reference_combo.event_generate("<<ComboboxSelected>>")
        self.root.update()
        self.assertEqual(self.row("CarBody", "TorqueScale")[2:], ("900", "850"))
        self.assertEqual(self.row("CarBody", "Mass")[3], "1050")
        self.app.search_var.set("")
        self.choose_added(9006)
        self.app.notebook.select(self.app.tabs["stats"])
        self.root.update()
        self.assertIsNone(self.app.original_current)
        self.assertEqual(self.row("CarBody", "TorqueScale")[3], "850")
        self.assertEqual(self.row("CarBody", "Mass")[3], "1050")
        self.assertEqual(self.app.model.config, self.baseline)
        self.assertEqual(self.path.read_bytes(), self.disk_before)
        self.error_dialog.assert_not_called()

    def test_draft_edit_survives_dlc_browse_and_return_to_added_car(self):
        ident = self.app.model.config["cars"][self.app.current]["id"]
        self.app.physics_vars["TorqueScale"].set("888")
        self.choose_dlc(215)
        draft = copy.deepcopy(self.app.model.config)
        self.assertEqual(draft["cars"][self.app.current]["physics"]["TorqueScale"], 888)
        self.assertEqual(self.app.model.saved, self.baseline)
        self.choose_dlc(201)
        self.choose_original(18)
        self.assertEqual(self.app.model.config, draft)
        self.choose_added(ident)
        self.assertEqual(self.app.physics_vars["TorqueScale"].get(), "888")
        self.assertIsNone(self.app.original_current)
        self.assertEqual(self.app.model.config, draft)
        self.assertEqual(self.path.read_bytes(), self.disk_before)
        self.error_dialog.assert_not_called()

    def test_added_stats_combine_donor_values_with_live_draft_and_comparison(self):
        self.choose_added(9006)  # Formula 1 inherits Lotus physics.
        self.app.physics_vars["TorqueScale"].set("812")
        self.app.gear_var.set("-3.3, 3.0, 2.0, 1.2")
        self.app.speed_vars["max_kmh"].set("600")
        self.app.speed_vars["max_mph"].set("373")
        self.app.notebook.select(self.app.tabs["stats"])
        self.root.update()
        self.assertEqual(self.row("CarBody", "TorqueScale")[2:], ("812", "700"))
        self.assertEqual(self.row("CarBody", "BrakeTorque")[2:], ("4000", "4000"))
        self.assertEqual(self.row("CarBody", "Mass")[2], "600")
        self.assertEqual(self.row("CarBody", "NGears")[2], "3")
        self.assertEqual(self.row("CarBody", "GearRatioVector")[2], "-3.3 3 2 1.2")
        self.assertEqual(self.row("CarCamera", "SpeedometerMaxKmh")[2], "600")
        self.assertEqual(self.row("CarCameraSS", "SpeedometerMaxKmh")[2], "600")
        self.assertEqual(self.row("CarWheel0", "SpringK")[2], "70000")
        colors = [self.app.stats_view.tree.item(item, "values")[2]
                  for item in self.app.stats_view.tree.get_children()
                  if self.app.stats_view.tree.set(item, "setting") == "COLOR"]
        self.assertEqual(len(colors), len(self.baseline["cars"][self.app.current]["colors"]))
        self.assertFalse(any(self.app.stats_view.tree.set(item, "setting") in ("LOCKED", "SKIN")
                             for item in self.app.stats_view.tree.get_children()))
        self.app.stats_view.reference_var.set("Dodge Viper")
        self.app.stats_view._render()
        self.assertEqual(self.row("CarBody", "TorqueScale")[2:], ("812", "1000"))
        self.assertEqual(self.row("CarBody", "Mass")[3], "1600")
        # Inspecting a live preview should neither save nor change the draft
        # model until the user leaves the car or presses Save.
        self.assertEqual(self.app.model.config, self.baseline)
        self.assertEqual(self.path.read_bytes(), self.disk_before)
        self.error_dialog.assert_not_called()

    def test_setting_rows_show_allowed_limit_and_named_game_maximum(self):
        rows = self.app.setting_rows
        self.assertEqual(rows["Mass"].cell_text("allowed"), "10000")
        # Panoz's 980 kg override sits below every reference car.
        self.assertEqual(rows["Mass"].cell_text("game"), "2200 · DLC Heavyweight ▼")
        self.assertEqual(rows["TorqueScale"].cell_text("game"), "1000 · Dodge Viper")
        self.assertEqual(rows["BrakeTorque"].cell_text("game"), "4000 · all 4 cars")
        self.assertEqual(rows["AirKZY"].cell_text("game"), "—")
        self.assertEqual(rows["WheelDrive"].cell_text("game"), "R 4")
        self.assertEqual(rows["WheelDrive"].cell_text("allowed"), "0 to 2")
        self.assertEqual(self.app.rating_rows["TOPSPEED"].cell_text("allowed"), "9")
        self.assertEqual(self.app.rating_rows["TOPSPEED"].cell_text("game"), "8 · all 4 cars")
        self.assertEqual(rows["MaxTorqueRPM"].cell_text("allowed"), "8000")
        self.app.factory_engine.set(True)
        self.assertEqual(rows["MaxTorqueRPM"].cell_text("allowed"), "20000")
        self.app.factory_engine.set(False)
        self.assertEqual(rows["MaxTorqueRPM"].cell_text("allowed"), "8000")
        self.assertIn("Allowed for your value: 100 to 10000", self.app._field_tip("Mass"))
        self.assertIn("highest 2200 (DLC Heavyweight)", self.app._field_tip("Mass"))
        self.assertEqual(self.app.model.config, self.baseline)
        self.error_dialog.assert_not_called()

    def test_overrides_are_marked_and_values_beyond_game_range_flagged(self):
        row = self.app.setting_rows["Mass"]
        self.assertTrue(row.overridden)
        self.assertEqual(row.cell_text("base"), "1600")  # Panoz inherits from its Viper donor.
        self.app.physics_vars["Mass"].set("5000")
        self.assertEqual(row.cell_text("game"), "2200 · DLC Heavyweight ▲")
        self.app.physics_vars["Mass"].set("1500")
        self.assertEqual(row.cell_text("game"), "2200 · DLC Heavyweight")
        self.app.physics_vars["Mass"].set("")
        self.assertFalse(row.overridden)
        self.assertEqual(row.cell_text("game"), "2200 · DLC Heavyweight")
        self.assertEqual(self.path.read_bytes(), self.disk_before)

    def test_compare_picker_lists_added_cars_and_fills_compare_column(self):
        values = list(self.app.compare_combo.cget("values"))
        self.assertEqual(values, self.app.stats_view.reference_values())
        self.assertIn("Formula 1 (added)", values)
        self.assertNotIn("Panoz GTR-1 (added)", values)  # The car being edited is not a reference.
        self.assertLess(values.index("Formula 1 (added)"), values.index("Dodge Viper"))
        self.assertIn("DLC Racer", values)
        self.app.physics_vars["TorqueScale"].set("777")  # Panoz draft, kept when switching cars.
        self.choose_added(9006)
        self.app.compare_var.set("Panoz GTR-1 (added)")
        self.root.update()
        self.assertEqual(self.app.setting_rows["TorqueScale"].cell_text("compare"), "777")
        self.assertEqual(self.app.setting_rows["BrakeTorque"].cell_text("compare"), "4000")
        self.app.compare_var.set("DLC Heavyweight")
        self.root.update()
        self.assertEqual(self.app.setting_rows["Mass"].cell_text("compare"), "2200")
        self.assertEqual(self.app.rating_rows["MASS"].cell_text("compare"), "3")
        self.assertIn("DLC Heavyweight", self.app.tables[0].heading_labels["compare"].cget("text"))
        self.assertEqual(self.row("CarBody", "Mass")[3], "2200")
        self.app.compare_var.set("None")
        self.root.update()
        self.assertEqual(self.app.setting_rows["Mass"].cell_text("compare"), "")
        self.assertEqual(self.path.read_bytes(), self.disk_before)
        self.error_dialog.assert_not_called()

    def test_compare_choice_is_shared_with_game_stats_reference(self):
        self.assertEqual(self.app.stats_view.reference_combo.winfo_manager(), "")
        self.assertEqual(self.app.compare_var.get(), "Lotus Esprit V8")
        self.app.stats_view.reference_var.set("Dodge Viper")
        self.root.update()
        self.assertEqual(self.app.compare_var.get(), "Dodge Viper")
        self.assertEqual(self.app.setting_rows["TorqueScale"].cell_text("compare"), "1000")
        self.assertIn("Dodge Viper", self.app.gear_compare_label.cget("text"))
        self.assertIn("Compare (Dodge Viper): 620 km/h · 500 mph", self.app.dial_info.cget("text"))

    def test_clicking_compare_value_copies_it_into_the_override(self):
        self.app.compare_var.set("DLC Heavyweight")
        self.root.update()
        self.app.use_compare_value("Mass")
        self.assertEqual(self.app.physics_vars["Mass"].get(), "2200")
        self.app.use_compare_value("BrakeTorque")
        self.assertEqual(self.app.physics_vars["BrakeTorque"].get(), "4000")
        self.app.use_compare_value("rating.HANDLING")
        self.assertEqual(self.app.rating_vars["HANDLING"].get(), "6")
        self.app.use_compare_value("AirKZY")  # Not stored by the reference: nothing to copy.
        self.assertEqual(self.app.physics_vars["AirKZY"].get(), "")
        self.app.use_compare_gears()
        self.assertEqual(self.app.gear_var.get(), "-3.3, 2.9, 1.77, 1.21, 0.92, 0.81")
        self.app.compare_values["physics"]["Mass"] = 15000  # Retail buses exceed the override limit.
        self.app.use_compare_value("Mass")
        self.assertEqual(self.app.physics_vars["Mass"].get(), "2200")
        self.assertIn("outside", self.app.status.get())
        self.app.compare_var.set("None")
        self.root.update()
        self.app.use_compare_value("TorqueScale")
        self.assertEqual(self.app.physics_vars["TorqueScale"].get(), "750")
        self.assertEqual(self.path.read_bytes(), self.disk_before)
        self.error_dialog.assert_not_called()

    def test_mouse_wheel_over_value_fields_scrolls_without_changing_them(self):
        def spinboxes(parent):
            for child in parent.winfo_children():
                if isinstance(child, editor.ttk.Spinbox):
                    yield child
                yield from spinboxes(child)

        dial = list(spinboxes(self.app.tabs["model_form"]))
        self.assertEqual(len(dial), 2)
        self.app.physics_vars["WheelDrive"].set(editor.DRIVE_OPTIONS[2])
        fields = [self.app.rating_rows["TOPSPEED"].field, self.app.physics_fields["WheelDrive"], *dial]
        variables = [self.app.rating_vars["TOPSPEED"], self.app.physics_vars["WheelDrive"],
                     *self.app.speed_vars.values()]
        before = [variable.get() for variable in variables]
        for field in fields:
            for delta in (-120, 120, -120):
                field.event_generate("<MouseWheel>", delta=delta)
        self.root.update()
        self.assertEqual([variable.get() for variable in variables], before)
        self.app.compare_combo.event_generate("<MouseWheel>", delta=-120)
        self.root.update()
        self.assertEqual(self.app.compare_var.get(), "Lotus Esprit V8")

    def test_blank_value_shows_inherited_placeholder_and_arrows_start_there(self):
        self.choose_added(9006)  # Formula 1 inherits BrakeTorque 4000 from its Lotus donor.
        field, variable = self.app.physics_fields["BrakeTorque"], self.app.physics_vars["BrakeTorque"]
        placeholder = self.app.placeholders["BrakeTorque"]
        self.assertEqual(variable.get(), "")
        self.assertEqual(placeholder.text, "4000")
        self.assertTrue(placeholder.visible)
        field.event_generate("<<Increment>>")
        self.assertEqual(variable.get(), "4250")
        self.assertFalse(placeholder.visible)
        row = self.app.setting_rows["BrakeTorque"]
        self.assertTrue(row.overridden)
        self.assertTrue(row.reset_button.instate(["!disabled"]))
        row.reset_button.invoke()
        self.assertEqual(variable.get(), "")
        self.assertTrue(row.reset_button.instate(["disabled"]))
        field.event_generate("<<Decrement>>")
        self.assertEqual(variable.get(), "3750")
        self.assertEqual(self.app.placeholders["WheelDrive"].text, "Base: Rear")
        self.app.factory_engine.set(True)  # Factory mode locks the peak RPM fields.
        self.app.physics_fields["MaxTorqueRPM"].event_generate("<<Increment>>")
        self.assertEqual(self.app.physics_vars["MaxTorqueRPM"].get(), "")
        self.app.factory_engine.set(False)
        self.assertEqual(self.path.read_bytes(), self.disk_before)
        self.error_dialog.assert_not_called()

    def test_live_validation_marks_fields_and_tabs_without_dialogs(self):
        row = self.app.setting_rows["Mass"]
        self.app.physics_vars["Mass"].set("50000")
        self.app.flush_validation()
        self.assertIn("Mass", self.app.problems)
        self.assertIn("100 to 10000", row.problem)
        self.assertTrue(row.field.instate(["invalid"]))
        self.assertTrue(self.app.notebook.tab(self.app.tabs["driving"], "text").endswith("⚠"))
        self.assertIn("Check this value", self.app.status.get())
        self.app.physics_vars["Mass"].set("1500")  # A valid value clears the mark at once.
        self.assertEqual(row.problem, "")
        self.assertTrue(row.field.instate(["!invalid"]))
        self.app.physics_vars["Mass"].set("abc")
        self.app.flush_validation()
        self.assertIn("must be a number", row.problem)
        self.app.physics_vars["Mass"].set("980")
        self.app.flush_validation()
        self.assertEqual(self.app.problems, {})
        self.assertEqual(self.app.notebook.tab(self.app.tabs["driving"], "text"), "Driving")
        self.assertEqual(self.app.model.config, self.baseline)
        self.error_dialog.assert_not_called()

    def test_rpm_order_problem_is_reported_beside_the_rpm_settings(self):
        self.assertTrue(self.app.rpm_line.get().startswith("✓"))
        self.app.physics_vars["CutRPM"].set("12000")  # Above the 10000 redline it inherits.
        self.app.flush_validation()
        self.assertIn("rpm", self.app.problems)
        self.assertTrue(self.app.rpm_line.get().startswith("✗"))
        self.assertTrue(self.app.setting_rows["CutRPM"].field.instate(["invalid"]))
        self.assertTrue(self.app.notebook.tab(self.app.tabs["gears"], "text").endswith("⚠"))
        self.app.physics_vars["CutRPM"].set("")
        self.app.flush_validation()
        self.assertTrue(self.app.rpm_line.get().startswith("✓"))
        self.assertEqual(self.app.problems, {})
        self.error_dialog.assert_not_called()

    def test_gear_grid_and_gear_text_stay_in_sync(self):
        grid = self.app.gear_grid
        self.app.gear_var.set("-3.3, 3.0, 2.0, 1.2")
        self.assertEqual([cell.get() for cell in grid.cells], ["-3.3", "3.0", "2.0", "1.2", "", "", "", ""])
        self.assertEqual(grid.count, 3)
        grid.cells[2].set("2.1")
        self.assertEqual(self.app.gear_var.get(), "-3.3, 3.0, 2.1, 1.2")
        grid.add_gear()
        tokens = self.app.gear_var.get().split(", ")
        self.assertEqual(len(tokens), 5)
        self.assertLess(float(tokens[-1]), 1.2)
        grid.remove_gear()
        self.assertEqual(self.app.gear_var.get(), "-3.3, 3.0, 2.1, 1.2")
        grid.cells[1].set("")  # A gap must not shift the other gears.
        self.assertEqual(self.app.gear_var.get(), "-3.3, ?, 2.1, 1.2")
        self.app.flush_validation()
        self.assertIn("Gear ratio", self.app.gear_problem.get())
        self.assertIn("gears", self.app.problems)
        self.app.gear_var.set("")
        self.assertEqual([cell.get() for cell in grid.cells], [""] * 8)
        self.assertEqual(grid.count, 5)  # The inherited Viper gearbox has five forward gears.
        self.app.flush_validation()
        self.assertEqual(self.app.gear_problem.get(), "")
        self.error_dialog.assert_not_called()

    def test_first_gear_edit_starts_from_the_inherited_gearbox(self):
        self.choose_added(9006)
        grid = self.app.gear_grid
        self.assertEqual(self.app.gear_var.get(), "")
        grid.cells[1].set("3.1")
        self.assertEqual(self.app.gear_var.get(), "-3.3, 3.1, 1.77, 1.21, 0.92, 0.81")
        grid.clear()
        self.assertEqual(self.app.gear_var.get(), "")
        grid.start_from_base()
        self.assertEqual(self.app.gear_var.get(), "-3.3, 2.9, 1.77, 1.21, 0.92, 0.81")

    def test_unsaved_markers_follow_the_draft_and_clear_after_save(self):
        self.assertFalse(self.app.is_dirty())
        self.assertFalse(self.app.root.title().startswith("*"))
        self.app.physics_vars["TorqueScale"].set("777")
        self.app.flush_validation()
        self.assertTrue(self.app.is_dirty())
        self.assertTrue(self.app.root.title().startswith("*"))
        self.assertEqual(self.app.unsaved_badge.winfo_manager(), "pack")
        self.assertTrue(self.app.listbox.get(0).endswith("•"))
        self.app.physics_vars["TorqueScale"].set("750")
        self.app.flush_validation()
        self.assertFalse(self.app.is_dirty())
        self.assertEqual(self.app.unsaved_badge.winfo_manager(), "")
        self.app.physics_vars["TorqueScale"].set("777")
        self.choose_added(9006)
        self.assertTrue(self.app.listbox.get(0).endswith("•"))
        self.assertEqual(self.app.unsaved_badge.winfo_manager(), "")
        self.assertTrue(self.app.save())
        self.assertFalse(any(self.app.listbox.get(index).endswith("•") for index in range(self.app.listbox.size())))
        self.assertFalse(self.app.root.title().startswith("*"))
        self.error_dialog.assert_not_called()

    def test_clear_overrides_and_restore_live_on_the_driving_tab(self):
        self.assertTrue(str(self.app.restore_button).startswith(str(self.app.tabs["driving"])))
        self.choose_added(9006)
        self.app.gear_var.set("-3.3, 3, 2, 1")
        self.app.clear_overrides()
        self.assertTrue(all(variable.get() == "" for variable in self.app.physics_vars.values()))
        self.assertEqual(self.app.gear_var.get(), "")
        self.assertEqual(self.app.rating_vars["TOPSPEED"].get(), "8")
        self.assertIn("0 values changed", self.app.override_summary.cget("text"))
        self.app._collect()
        self.assertEqual(self.app.model.config["cars"][self.app.current]["physics"], {})
        self.app.restore_button.invoke()
        self.assertEqual(self.app.model.config["cars"][self.app.current]["physics"], {"Mass": 600, "TorqueScale": 420})
        self.error_dialog.assert_not_called()

    def test_copy_base_ratings_and_rating_bar_clicks(self):
        self.choose_added(9006)  # The Lotus fixture rates 8/8/3/6/2.
        self.app.rating_vars["TOPSPEED"].set("1")
        self.app.copy_base_ratings()
        self.assertEqual([self.app.rating_vars[key].get() for key in editor.RATING_ORDER], ["8", "8", "3", "6", "2"])
        bar = self.app.rating_bars["HANDLING"]
        bar.select(4)
        self.assertEqual(self.app.rating_vars["HANDLING"].get(), "4")
        bar.select(4)  # Clicking the top lit segment steps down by one.
        self.assertEqual(self.app.rating_vars["HANDLING"].get(), "3")
        self.assertEqual(self.app.rating_bars["MASS"].markers["base"], 3)

    def test_appearance_offers_a_prepared_package_and_shows_the_donor_dial(self):
        packages = self.path.parent / "packages"
        (packages / "Form1").mkdir(parents=True)
        (packages / "Form1" / "package.json").write_text("{}", encoding="utf-8")
        with patch.object(editor, "PACKAGES_DIR", packages), \
                patch.object(editor.prepare, "load_model_package", return_value=[]) as checker:
            self.choose_added(9006)
            self.assertEqual(self.app.package_offer.winfo_manager(), "pack")
            self.assertIn("packages/Form1", self.app.package_offer_label.cget("text"))
            self.app.use_detected_package()
            checker.assert_called_once()
            self.assertEqual(self.app.model_var.get(), "packages/Form1")
            self.assertEqual(self.app.package_offer.winfo_manager(), "")
        self.assertEqual(self.app.placeholders["max_kmh"].text, "")  # Fixture has no native HUD type.
        self.assertEqual(self.app.placeholders["max_mph"].text, "")
        self.error_dialog.assert_not_called()

    def test_missing_donor_stats_show_live_draft_without_saving(self):
        car = next(car for car in self.app.model.config["cars"]
                   if car["donor"] not in self.app.original_by_alias)
        self.choose_added(car["id"])
        before = copy.deepcopy(self.app.model.config)
        self.app.physics_vars["Mass"].set("999")
        self.app.rating_vars["TOPSPEED"].set("9")
        self.app._show_game_stats()
        self.assertEqual(self.row("Simulation", "Mass")[2], "999")
        self.assertEqual(self.row("Ratings", "TOPSPEED")[2], "9")
        self.assertEqual(self.app.model.config, before)
        self.assertEqual(self.path.read_bytes(), self.disk_before)
        self.error_dialog.assert_not_called()

    def test_missing_donor_invalid_draft_keeps_last_valid_stats(self):
        car = next(car for car in self.app.model.config["cars"]
                   if car["donor"] not in self.app.original_by_alias)
        self.choose_added(car["id"])
        before = copy.deepcopy(self.app.model.config)
        self.app.physics_vars["Mass"].set("unfinished")
        self.app._show_game_stats()
        self.assertEqual(self.row("Simulation", "Mass")[2], str(car["physics"]["Mass"]))
        self.assertIn("last valid settings", self.app.status.get())
        self.assertEqual(self.app.model.config, before)
        self.assertEqual(self.path.read_bytes(), self.disk_before)
        self.error_dialog.assert_not_called()

    def test_kept_original_hud_changes_mark_window_unsaved_and_save_clears_it(self):
        dialogs = []
        real_dialog = editor.OriginalSpeedometerDialog

        def make_dialog(root, model, changed):
            dialog = real_dialog(root, model, changed, show=False)
            dialogs.append(dialog)
            return dialog

        with patch.object(editor, "OriginalSpeedometerDialog", side_effect=make_dialog):
            self.app.original_speedometers()
        dialog = dialogs[0]
        self.addCleanup(lambda: dialog.window.destroy() if dialog.window.winfo_exists() else None)
        dialog.speed_vars["max_kmh"].set("900")
        dialog.speed_vars["max_mph"].set("560")
        dialog.keep()
        self.root.update()
        self.assertTrue(self.app.is_dirty())
        self.assertTrue(self.root.title().startswith("*"), self.root.title())
        self.assertEqual(self.path.read_bytes(), self.disk_before)
        self.assertTrue(self.app.save())
        self.assertFalse(self.app.is_dirty())
        self.assertFalse(self.root.title().startswith("*"), self.root.title())
        saved = json.loads(self.path.read_text(encoding="utf-8"))
        self.assertEqual(saved["original_speedometers"]["Koenigsegg"],
                         {"max_kmh": 900, "max_mph": 560})
        self.error_dialog.assert_not_called()

    def test_apply_uses_snapshot_and_keeps_later_window_edits_unsaved(self):
        self.app.physics_vars["Mass"].set("999")
        with patch.object(editor, "game_is_running", return_value=False), \
                patch.object(editor.threading, "Thread") as thread, \
                patch.object(editor.subprocess, "run") as installer:
            self.app.apply()
            self.assertTrue(self.app.applying)
            self.assertIn("Applying", self.app.install_status.get())
            self.assertTrue(all(widget.instate(["disabled"])
                                for widget in self.app._apply_locked_widgets()))
            thread.return_value.start.assert_called_once_with()
            snapshots = list(self.path.parent.glob(".mm3-workshop-apply-*.json"))
            self.assertEqual(len(snapshots), 1)
            snapshot = snapshots[0]
            self.assertEqual(snapshot.read_bytes(), self.path.read_bytes())
            self.assertEqual(editor.prepare.load_config(snapshot)["cars"][0]["physics"]["Mass"], 999)
            self.app.physics_vars["Mass"].set("888")
            installer.return_value = editor.subprocess.CompletedProcess([], 0, "Installed", "")
            thread.call_args.kwargs["target"]()
            self.assertEqual(installer.call_args.args[0][3], str(snapshot))
            self.assertFalse(snapshot.exists())
            self.app._poll_apply()
        self.assertFalse(self.app.applying)
        self.assertTrue(all(not widget.instate(["disabled"])
                            for widget in self.app._apply_locked_widgets()))
        self.assertEqual(self.app.physics_vars["Mass"].get(), "888")
        self.assertEqual(json.loads(self.path.read_text())["cars"][0]["physics"]["Mass"], 999)
        self.assertTrue(self.app.is_dirty())
        self.assertIn("Unsaved", self.app.install_status.get())
        self.assertEqual(self.app.applied_settings, self.app.model.saved)
        self.assertEqual(self.app.applied_settings["cars"][0]["physics"]["Mass"], 999)
        self.assertTrue(self.app.applied_in_session)
        self.assertIn("New edits", self.app.status.get())
        self.assertFalse(self.app.apply_progress.winfo_manager())
        self.app.physics_vars["Mass"].set("999")
        self.app.flush_validation()
        self.assertFalse(self.app.is_dirty())
        self.assertIn("Applied", self.app.install_status.get())
        self.assertIn("restart", self.app.install_status.get())
        self.error_dialog.assert_not_called()

    def test_failed_apply_releases_controls_and_removes_snapshot(self):
        previous_apply = copy.deepcopy(self.app.model.saved)
        self.app.applied_settings = copy.deepcopy(previous_apply)
        self.app.physics_vars["Mass"].set("999")
        with patch.object(editor, "game_is_running", return_value=False), \
                patch.object(editor.threading, "Thread") as thread, \
                patch.object(editor.subprocess, "run", return_value=
                             editor.subprocess.CompletedProcess([], 1, "", "Fixture installer failed")):
            self.app.apply()
            self.assertIn("Applying", self.app.install_status.get())
            snapshot = next(self.path.parent.glob(".mm3-workshop-apply-*.json"))
            thread.call_args.kwargs["target"]()
            self.assertFalse(snapshot.exists())
            self.app._poll_apply()
        self.assertFalse(self.app.applying)
        self.assertTrue(all(not widget.instate(["disabled"])
                            for widget in self.app._apply_locked_widgets()))
        self.assertIn("Fixture installer failed", self.app.status.get())
        self.assertEqual(self.app.applied_settings, previous_apply)
        self.assertFalse(self.app.applied_in_session)
        self.assertFalse(self.app.is_dirty())
        self.assertIn("apply needed", self.app.install_status.get())
        self.assertFalse(self.app.apply_progress.winfo_manager())
        self.error_dialog.assert_called_once()

    def test_installer_launch_error_releases_controls_and_removes_snapshot(self):
        with patch.object(editor, "game_is_running", return_value=False), \
                patch.object(editor.threading, "Thread") as thread, \
                patch.object(editor.subprocess, "run", side_effect=OSError("Fixture launch failed")):
            self.app.apply()
            snapshot = next(self.path.parent.glob(".mm3-workshop-apply-*.json"))
            thread.call_args.kwargs["target"]()
            self.assertFalse(snapshot.exists())
            self.app._poll_apply()
        self.assertFalse(self.app.applying)
        self.assertTrue(all(not widget.instate(["disabled"])
                            for widget in self.app._apply_locked_widgets()))
        self.assertIn("Fixture launch failed", self.app.status.get())
        self.assertIsNone(self.app.applied_settings)
        self.assertFalse(self.app.applied_in_session)
        self.assertIn("no recorded apply", self.app.install_status.get())
        self.error_dialog.assert_called_once()

    def test_running_game_saves_settings_without_starting_installation(self):
        self.app.physics_vars["Mass"].set("999")
        with patch.object(editor, "game_is_running", return_value=True), \
                patch.object(self.app.model, "apply_snapshot_command") as snapshot, \
                patch.object(editor.threading, "Thread") as thread:
            self.app.apply()
        snapshot.assert_not_called()
        thread.assert_not_called()
        self.assertFalse(self.app.applying)
        self.assertEqual(json.loads(self.path.read_text())["cars"][0]["physics"]["Mass"], 999)
        self.assertFalse(self.app.is_dirty())
        self.assertIn("Close the running MM3 game", self.app.status.get())
        self.assertEqual(list(self.path.parent.glob(".mm3-workshop-apply-*.json")), [])
        self.error_dialog.assert_not_called()

    def test_cli_original_selection_opens_stat_reference(self):
        args = editor.parse_args(["--car-id", "18", "--tab", "stats"])
        self.app.select_initial(args.car_id, args.tab)
        self.root.update()
        self.assertEqual(self.app.original_cars[self.app.original_current]["id"], 18)
        self.assertEqual(self.app.notebook.select(), str(self.app.tabs["stats"]))
        self.assertEqual(self.app.model.config, self.baseline)
        self.assertEqual(self.path.read_bytes(), self.disk_before)

    def test_cli_dlc_selection_opens_dlc_catalog_and_stat_reference(self):
        args = editor.parse_args(["--car-id", "201", "--tab", "stats"])
        self.app.select_initial(args.car_id, args.tab)
        self.root.update()
        self.assertEqual(self.app.original_cars[self.app.original_current]["id"], 201)
        self.assertEqual(self.app.catalog_tabs.index(self.app.catalog_tabs.select()), 2)
        self.assertEqual(self.app.notebook.select(), str(self.app.tabs["stats"]))
        self.assertTrue(self.app.identity_label.cget("text").startswith("DLC car"))
        self.assertEqual(self.app.model.config, self.baseline)
        self.assertEqual(self.path.read_bytes(), self.disk_before)
        self.error_dialog.assert_not_called()


class AppliedMetadataUITest(WorkshopFixture):
    """Session badges use recorded install settings, independently of file saves."""

    def fixture_manifest(self, config):
        self.state_path = self.path.parent / "game" / editor.prepare.STATE_DIR / "state.json"
        self.state_path.parent.mkdir(parents=True)
        self.state_bytes = (json.dumps({"config": config}) + "\n").encode("utf-8")
        self.state_path.write_bytes(self.state_bytes)
        return None

    def test_initial_matching_metadata_identifies_last_apply_without_session_apply(self):
        self.assertEqual(self.app.applied_settings, self.app.model.saved)
        self.assertFalse(self.app.applied_in_session)
        self.assertFalse(self.app.is_dirty())
        self.assertIn("matches last apply", self.app.install_status.get())
        self.assertEqual(self.state_path.read_bytes(), self.state_bytes)
        self.assertEqual(self.path.read_bytes(), self.disk_before)

    def test_save_clears_unsaved_state_but_keeps_apply_needed_when_browsing(self):
        previous_apply = copy.deepcopy(self.app.applied_settings)
        self.app.physics_vars["Mass"].set("999")
        self.app.flush_validation()
        self.assertTrue(self.app.is_dirty())
        self.assertIn("Unsaved", self.app.install_status.get())
        self.assertTrue(self.app.save())
        self.assertFalse(self.app.is_dirty())
        self.assertFalse(self.root.title().startswith("*"))
        self.assertIn("apply needed", self.app.install_status.get())
        self.choose_original(18)
        self.assertIn("apply needed", self.app.install_status.get())
        self.choose_added(9006)
        self.assertIn("apply needed", self.app.install_status.get())
        self.assertEqual(self.app.applied_settings, previous_apply)
        self.assertFalse(self.app.applied_in_session)
        self.assertEqual(self.state_path.read_bytes(), self.state_bytes)
        self.assertEqual(json.loads(self.path.read_text())["cars"][0]["physics"]["Mass"], 999)
        self.error_dialog.assert_not_called()

    def test_reload_preserves_session_apply_only_while_metadata_is_unchanged(self):
        self.app.applying = True
        self.app._applying_settings = copy.deepcopy(self.app.model.saved)
        self.app.events.put((0, "Fixture installation completed", ""))
        self.app._poll_apply()
        self.assertTrue(self.app.applied_in_session)
        with patch.object(editor.messagebox, "askyesno", return_value=True):
            self.app.reload()
        self.assertTrue(self.app.applied_in_session)
        self.assertIn("Applied", self.app.install_status.get())

        external = copy.deepcopy(self.app.model.saved)
        external["cars"][0]["physics"]["Mass"] = 999
        self.path.write_text(json.dumps(external) + "\n", encoding="utf-8")
        self.state_path.write_text(json.dumps({"config": external}) + "\n", encoding="utf-8")
        with patch.object(editor.messagebox, "askyesno", return_value=True):
            self.app.reload()
        self.assertEqual(self.app.applied_settings, external)
        self.assertEqual(self.app.model.saved, external)
        self.assertFalse(self.app.applied_in_session)
        self.assertFalse(self.app.is_dirty())
        self.assertIn("matches last apply", self.app.install_status.get())
        self.error_dialog.assert_not_called()


class BaseStatsUITest(WorkshopFixture):
    """Base stats picker over complete fixture cars and an identical DLC twin."""
    CORVETTE = {"Mass": 1500, "TorqueScale": 800, "BrakeTorque": "7500.0", "WheelRadius": "0.6",
                "NGears": 6, "GearRatioVector": "-3.2 3.0 2.0 1.4 1.0 0.8 0.6"}

    def fixture_originals(self):
        return [full_original(18, "Lotus", "Lotus Esprit V8"),
                original(11, "Viper", "Dodge Viper", 1600, 1000),
                full_original(25, "Corvette", "Chevrolet Corvette", **self.CORVETTE)]

    def fixture_dlc(self):
        return [full_original(201, "car201", "Twin Racer", physics_id=25, **self.CORVETTE),
                original(215, "car215", "DLC Heavyweight", 2200, 900)]

    def fixture_manifest(self, config):
        config["cars"][0]["driving_base"] = "corvette"  # Panoz, saved in lower case.
        return config

    def base_label(self, display_name):
        return next(label for label in self.app.base_combo.cget("values") if label.startswith(display_name))

    def test_base_is_independent_and_dial_maxima_update_the_hud(self):
        for alias, gauge_type in (("Lotus", 1), ("Corvette", 4)):
            car = self.app.original_by_alias[alias]
            info = next(section for section in car["sections"] if section["name"] == "CarInfo")
            info["entries"].append(("SPEEDOMETERTYPE", str(gauge_type)))
            car["raw_text"] = car["raw_text"].replace("End(CarInfo)",
                                                    f"SPEEDOMETERTYPE {gauge_type}\r\nEnd(CarInfo)", 1)
        self.app.hud_photos.clear()
        with patch.object(editor.hud_preview, "load_dial", return_value=None) as artwork:
            self.choose_added(9006)
            self.assertIn("Lotus Esprit V8 HUD style", self.app.hud_info.cget("text"))
            self.assertIn("380 km/h", self.app.hud_captions[True].cget("text"))
            self.assertIn("240 mph", self.app.hud_captions[False].cget("text"))
            self.app.base_var.set(self.base_label("Chevrolet Corvette"))
            self.app.speed_vars["max_kmh"].set("1100")
            self.app.speed_vars["max_mph"].set("690")
            self.app.compare_var.set("Chevrolet Corvette")
            self.root.update()
            self.app._show_game_stats()
            self.assertEqual(self.app.base_values["physics"]["BrakeTorque"], 7500)
            self.assertEqual(self.app.base_values["physics"]["WheelRadius"], 0.33)
            self.assertEqual(editor.reference.car_values(self.app.stats_view.car)["dial"],
                             {"max_kmh": 1100, "max_mph": 690})
            self.assertEqual(editor.reference.native_hud_markings(self.app.stats_view.car),
                             {"type": 1, "max_kmh": 300, "max_mph": 200})
            self.assertIn("1100 km/h", self.app.hud_captions[True].cget("text"))
            self.assertIn("690 mph", self.app.hud_captions[False].cget("text"))
            self.assertTrue(artwork.call_args_list)
            self.assertTrue(all(call.args[1] == 1 for call in artwork.call_args_list))
        self.assertEqual(self.path.read_bytes(), self.disk_before)
        self.error_dialog.assert_not_called()

    def test_base_choice_changes_inherited_values_but_keeps_overrides(self):
        self.choose_added(9006)  # Formula 1: Lotus MM3 donor, overrides Mass 600 and TorqueScale 420.
        values = list(self.app.base_combo.cget("values"))
        self.assertEqual(values[0], "Lotus Esprit V8 (MM3 donor)")
        self.assertIn("Chevrolet Corvette (also DLC: Twin Racer)", values)
        rows = self.app.setting_rows
        self.assertEqual(rows["BrakeTorque"].cell_text("base"), "4000")
        self.app.base_var.set(self.base_label("Chevrolet Corvette"))
        self.root.update()
        self.assertEqual(rows["BrakeTorque"].cell_text("base"), "7500")
        self.assertEqual(rows["Mass"].cell_text("base"), "1500")
        self.assertEqual(rows["WheelRadius"].cell_text("base"), "0.33")  # Wheel size stays with the body.
        self.assertEqual(self.app.physics_vars["Mass"].get(), "600")
        self.assertIn("Chevrolet Corvette", self.app.tables[0].heading_labels["base"].cget("text"))
        self.assertIn("-3.2, 3, 2, 1.4, 1, 0.8, 0.6", self.app.gear_donor_label.cget("text"))
        self.assertIn("Donor interior (Lotus Esprit V8)", self.app.dial_info.cget("text"))
        self.assertIn("Base stats now from Chevrolet Corvette", self.app.status.get())
        self.app._collect()
        car = self.app.model.config["cars"][self.app.current]
        self.assertEqual(car["driving_base"], "Corvette")
        self.assertEqual(car["physics"], {"Mass": 600, "TorqueScale": 420})
        self.app.base_var.set(values[0])
        self.app._collect()
        self.assertNotIn("driving_base", self.app.model.config["cars"][self.app.current])
        self.assertEqual(self.path.read_bytes(), self.disk_before)
        self.error_dialog.assert_not_called()

    def test_base_stats_flow_into_game_stats_preview(self):
        self.choose_added(9006)
        self.app.base_var.set(self.base_label("Chevrolet Corvette"))
        self.app.physics_vars["Mass"].set("")
        self.app.notebook.select(self.app.tabs["stats"])
        self.root.update()
        self.assertEqual(self.row("CarBody", "Mass")[2], "1500")
        self.assertEqual(self.row("CarBody", "BrakeTorque")[2], "7500.0")
        self.assertEqual(self.row("CarBody", "WheelRadius")[2], "0.33")
        self.assertEqual(self.row("CarBody", "TorqueScale")[2], "420")
        self.assertIn("Chevrolet Corvette base stats", self.app.stats_view.source_var.get())
        self.assertEqual(self.path.read_bytes(), self.disk_before)
        self.error_dialog.assert_not_called()

    def test_saved_base_keeps_its_spelling_and_browsing_changes_nothing(self):
        self.assertEqual(self.app.model.config["cars"][0]["driving_base"], "corvette")
        self.assertTrue(self.app.base_var.get().startswith("Chevrolet Corvette"))
        self.assertIn("come from Chevrolet Corvette", self.app.picker_hint.cget("text"))
        self.app._collect()
        self.choose_added(9006)
        self.choose_original(18)
        self.choose_added(9004)
        self.app._collect()
        self.assertEqual(self.app.model.config, self.baseline)
        self.assertEqual(self.path.read_bytes(), self.disk_before)
        self.error_dialog.assert_not_called()

    def test_added_cars_with_a_base_compare_with_their_base_values(self):
        self.choose_added(9006)
        self.app.compare_var.set("Panoz GTR-1 (added)")
        self.root.update()
        self.assertEqual(self.app.setting_rows["BrakeTorque"].cell_text("compare"), "7500")
        self.assertEqual(self.app.setting_rows["Mass"].cell_text("compare"), "980")

    def test_read_only_cars_hide_the_base_picker(self):
        self.assertEqual(self.app.base_combo.winfo_manager(), "grid")
        self.choose_original(18)
        self.assertEqual(self.app.base_combo.winfo_manager(), "")
        self.assertIn("Read-only reference", self.app.picker_hint.cget("text"))
        self.choose_dlc(201)
        self.assertEqual(self.app.base_combo.winfo_manager(), "")
        self.choose_added(9006)
        self.assertEqual(self.app.base_combo.winfo_manager(), "grid")

    def test_base_stats_and_factory_engine_exclude_each_other(self):
        self.choose_added(9006)
        self.assertEqual(str(self.app.base_combo.cget("state")), "readonly")
        self.app.factory_engine.set(True)
        self.assertEqual(str(self.app.base_combo.cget("state")), "disabled")
        self.assertIn("factory engine profile", self.app.picker_hint.cget("text"))
        self.app.factory_engine.set(False)
        self.app.base_var.set(self.base_label("Chevrolet Corvette"))
        self.root.update()
        self.assertTrue(self.app.factory_check.instate(["disabled"]))
        self.app.base_var.set(self.app.base_combo.cget("values")[0])
        self.root.update()
        self.assertFalse(self.app.factory_check.instate(["disabled"]))

    def test_restore_shipped_tuning_returns_base_to_the_body_donor(self):
        self.choose_added(9006)
        self.app.base_var.set(self.base_label("Chevrolet Corvette"))
        self.app._collect()
        self.app.restore_default_driving()
        self.root.update()
        car = self.app.model.config["cars"][self.app.current]
        self.assertNotIn("driving_base", car)
        self.assertEqual(car["physics"], {"Mass": 600, "TorqueScale": 420})
        self.assertEqual(self.app.base_var.get(), self.app.base_combo.cget("values")[0])
        self.error_dialog.assert_not_called()


class HudStyleUITest(WorkshopFixture):
    def fixture_originals(self):
        cars = [full_original(18, "Lotus", "Lotus Esprit V8"),
                full_original(11, "Viper", "Dodge Viper"),
                full_original(13, "Koenigsegg", "Koenigsegg")]
        for car, gauge in zip(cars, (1, 1, 4)):
            car["raw_text"] = car["raw_text"].replace("End(CarInfo)", f"SPEEDOMETERTYPE {gauge}\r\nEnd(CarInfo)", 1)
            car["sections"] = editor.original_stats.parse_sections(car["raw_text"])
        return cars

    def fixture_manifest(self, config):
        formula = config["cars"][2]
        formula["driving_base"] = "Koenigsegg"
        formula["speedometer"] = {"max_kmh": 380, "max_mph": 240}
        formula["physics"]["AirKZZ"] = .001
        config["original_speedometers"] = {"Koenigsegg": {"max_kmh": 380, "max_mph": 600}}
        return config

    def choose_hud(self, alias):
        text = next(text for text, value in self.app.hud_choices.items() if value == alias)
        self.app.hud_var.set(text)
        self.root.update()
        self.app.flush_validation()

    def test_hud_selection_updates_artwork_preview_stats_and_saved_config(self):
        self.choose_added(9006)
        before = copy.deepcopy(self.app.model.config)
        self.app.hud_photos.clear()
        with patch.object(editor.hud_preview, "load_dial", return_value=None) as artwork:
            self.choose_hud("Koenigsegg")
            self.assertTrue(self.app.is_dirty())
            self.assertIn("Koenigsegg HUD style", self.app.hud_info.cget("text"))
            self.assertIn("380 km/h", self.app.hud_captions[True].cget("text"))
            self.assertIn("240 mph", self.app.hud_captions[False].cget("text"))
            self.assertEqual([call.args[1] for call in artwork.call_args_list], [4, 4])
        self.app._show_game_stats()
        self.assertEqual(editor.reference.native_hud_markings(self.app.stats_view.car)["type"], 4)
        self.assertTrue(self.app.save())
        expected = copy.deepcopy(before)
        expected["cars"][2]["hud_style"] = "Koenigsegg"
        self.assertEqual(json.loads(self.path.read_text(encoding="utf-8")), expected)
        self.choose_added(9004)
        self.choose_added(9006)
        self.assertEqual(self.app.hud_choices[self.app.hud_var.get()], "Koenigsegg")
        self.assertFalse(self.app.is_dirty())
        self.error_dialog.assert_not_called()

    def test_donor_reset_restores_lotus_hud_without_resetting_driving_or_camera(self):
        self.choose_added(9006)
        before = copy.deepcopy(self.app.model.config)
        self.choose_hud("Koenigsegg")
        self.app._collect()
        self.choose_hud("")
        self.app._show_game_stats()
        self.assertEqual(editor.reference.native_hud_markings(self.app.stats_view.car)["type"], 1)
        self.assertIn("380 km/h", self.app.hud_captions[True].cget("text"))
        self.assertIn("custom", self.app.hud_captions[True].cget("text"))
        self.app._collect()
        self.assertEqual(self.app.model.config, before)
        self.assertFalse(self.app.is_dirty())
        self.error_dialog.assert_not_called()

    def test_restore_driving_preserves_live_hud_selection(self):
        self.choose_added(9006)
        self.choose_hud("Koenigsegg")
        self.app.restore_default_driving()
        self.root.update()
        car = self.app.model.config["cars"][2]
        self.assertEqual(car["hud_style"], "Koenigsegg")
        self.assertNotIn("driving_base", car)
        self.assertIn("380 km/h", self.app.hud_captions[True].cget("text"))
        self.error_dialog.assert_not_called()

    def test_increased_dial_max_updates_both_live_previews_and_installer_ranges(self):
        self.choose_added(9006)
        self.choose_hud("Koenigsegg")
        before = copy.deepcopy(self.app._draft_car())
        self.app.speed_vars["max_kmh"].set("450")
        self.app.speed_vars["max_mph"].set("300")
        self.root.update()
        self.assertIn("450 km/h", self.app.hud_captions[True].cget("text"))
        self.assertIn("300 mph", self.app.hud_captions[False].cget("text"))
        self.assertIn("custom", self.app.hud_captions[True].cget("text"))
        self.assertTrue(self.app.save())
        car = self.app.model.config["cars"][2]
        self.assertEqual(car, {**before, "speedometer": {"max_kmh": 450, "max_mph": 300}})
        self.assertIn(b"added Form1 450 300\n", editor.prepare.speedometer_settings(self.app.model.config))
        self.app._show_game_stats()
        self.assertEqual(editor.reference.car_values(self.app.stats_view.car)["dial"],
                         {"max_kmh": 450, "max_mph": 300})
        self.error_dialog.assert_not_called()

    def test_blank_maxima_use_selected_native_style_and_invalid_draft_is_clear(self):
        self.choose_added(9006)
        self.choose_hud("Koenigsegg")
        self.app.speed_vars["max_kmh"].set("")
        self.assertIn("Complete both valid dial maxima", self.app.hud_info.cget("text"))
        self.app.speed_vars["max_mph"].set("")
        self.root.update()
        self.app.flush_validation()
        self.assertIn("380 km/h", self.app.hud_captions[True].cget("text"))
        self.assertIn("280 mph", self.app.hud_captions[False].cget("text"))
        self.assertIn("native", self.app.hud_captions[True].cget("text"))
        self.assertEqual(self.app.placeholders["max_kmh"].text, "380")
        self.assertNotIn("speedometer", self.app._draft_car())
        self.assertEqual(self.app.dial_problem.get(), "")
        self.error_dialog.assert_not_called()


if __name__ == "__main__":
    unittest.main()
