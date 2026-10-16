"""Game stats editing uses the workshop draft and preserves native row identity."""

import copy
import json

from test_car_workshop_ui import WorkshopFixture, editor, full_original


class GameStatsEditingTests(WorkshopFixture):
    def fixture_originals(self):
        cars = [full_original(18, "Lotus", "Lotus Esprit V8"),
                full_original(11, "Viper", "Dodge Viper", Mass=1600, TorqueScale=1000)]
        # Repeated keys and sections must remain independently addressable.
        extra = ("Begin(CarWheel1)\r\nSpringK 71000\r\nEnd(CarWheel1)\r\n"
                 "Begin(CarWheel0)\r\nSpringK 72000\r\nEnd(CarWheel0)\r\n"
                 "Begin(CarCameraSS)\r\nSpeedometerMaxKmh 620.0\r\n"
                 "SpeedometerMaxMph 500.0\r\nEnd(CarCameraSS)\r\n")
        for car in cars:
            car["raw_text"] = car["raw_text"].replace(
                "Begin(CarInfo)\r\n", "Begin(CarInfo)\r\nSPEEDOMETERTYPE 0\r\n").replace(
                "Begin(CarBody)\r\n", "Begin(CarBody)\r\nRollLevel 1.0\r\n")
            car["raw_text"] += extra
            car["sections"] = editor.original_stats.parse_sections(car["raw_text"])
        return cars

    def setUp(self):
        super().setUp()
        self.choose_added(9006)
        self.app.notebook.select(self.app.tabs["stats"])
        self.root.update()

    def stat(self, section, key, occurrence=0):
        identity = (section, key, occurrence)
        rows = [row for row in self.app.stats_view._visible_rows.values()
                if row["identity"] == identity]
        self.assertEqual(len(rows), 1, f"Expected a row for {identity!r}")
        return rows[0]

    def select_stat(self, section, key, occurrence=0):
        row = self.stat(section, key, occurrence)
        self.app.stats_view.tree.selection_set(row["iid"])
        self.app.stats_view.tree.focus(row["iid"])
        self.root.update()
        return row

    def test_physics_edit_updates_draft_and_saves_only_when_requested(self):
        row = self.stat("CarBody", "Mass")
        self.assertEqual(self.app._stat_edit_reason(row), "")
        self.app._edit_stat(row, "999")
        self.assertEqual(self.app.physics_vars["Mass"].get(), "999")
        self.assertEqual(self.stat("CarBody", "Mass")["value"], "999")
        self.assertTrue(self.app.is_dirty())
        self.assertEqual(self.path.read_bytes(), self.disk_before)
        self.assertTrue(self.app.save())
        saved = json.loads(self.path.read_text(encoding="utf-8"))
        car = next(car for car in saved["cars"] if car["id"] == 9006)
        self.assertEqual(car["physics"]["Mass"], 999)
        self.assertFalse(self.app.is_dirty())
        self.error_dialog.assert_not_called()

    def test_menu_mass_and_physics_mass_are_separate_and_reset_uses_base(self):
        self.app._edit_stat(self.stat("CarBody", "Mass"), "1500")
        self.app._edit_stat(self.stat("CarInfo", "MASS"), "5")
        self.assertEqual(self.app.rating_vars["MASS"].get(), "5")
        self.assertEqual(self.app.physics_vars["Mass"].get(), "1500")
        self.assertEqual(self.stat("CarInfo", "MASS")["value"], "5")
        with self.assertRaises(ValueError):
            self.app._edit_stat(self.stat("CarInfo", "MASS"), "4.5")
        self.assertEqual(self.app.rating_vars["MASS"].get(), "5")
        self.app._edit_stat(self.stat("CarBody", "Mass"), None)
        self.assertEqual(self.app.physics_vars["Mass"].get(), "")
        self.assertEqual(self.stat("CarBody", "Mass")["value"], "1380")
        self.assertEqual(self.path.read_bytes(), self.disk_before)

    def test_invalid_numeric_and_rpm_edits_leave_the_existing_draft_intact(self):
        before = self.app._draft_car()
        for section, key, text in (("CarBody", "Mass", "50000"),
                                   ("CarBody", "Mass", "unfinished"),
                                   ("CarBody", "CutRPM", "12000")):
            with self.subTest(key=key, text=text):
                with self.assertRaises(ValueError):
                    self.app._edit_stat(self.stat(section, key), text)
                self.assertEqual(self.app._draft_car(), before)
                self.assertEqual(self.path.read_bytes(), self.disk_before)
        self.error_dialog.assert_not_called()

    def test_unfinished_edit_in_another_tab_is_preserved_when_stat_edit_is_rejected(self):
        self.app.gear_var.set("unfinished")
        before = copy.deepcopy(self.app.model.config)
        with self.assertRaises(ValueError):
            self.app._edit_stat(self.stat("CarBody", "Mass"), "999")
        self.assertEqual(self.app.physics_vars["Mass"].get(), "600")
        self.assertEqual(self.app.gear_var.get(), "unfinished")
        self.assertEqual(self.app.model.config, before)
        self.assertEqual(self.path.read_bytes(), self.disk_before)
        self.error_dialog.assert_not_called()

    def test_gearbox_and_driven_wheels_use_the_existing_form_rules(self):
        self.app._edit_stat(self.stat("CarBody", "WheelDrive"), "2")
        self.assertEqual(self.app.physics_vars["WheelDrive"].get(), editor.DRIVE_OPTIONS[3])
        self.app._edit_stat(self.stat("CarBody", "GearRatioVector"), "-3.3 3 2 1")
        self.assertEqual(self.app._draft_car()["physics"]["GearRatioVector"], [-3.3, 3, 2, 1])
        self.assertEqual(self.stat("CarBody", "NGears")["value"], "3")
        before = self.app._draft_car()
        for key, text in (("WheelDrive", "3"), ("WheelDrive", "1.5"),
                          ("GearRatioVector", "-3.3 1 2 3")):
            with self.subTest(key=key, text=text):
                with self.assertRaises(ValueError):
                    self.app._edit_stat(self.stat("CarBody", key), text)
                self.assertEqual(self.app._draft_car(), before)

    def test_native_suspension_edit_scopes_duplicate_occurrences_and_persists(self):
        baseline_native = copy.deepcopy(self.app._draft_car().get("native_overrides", []))
        target = self.stat("CarWheel0", "SpringK", 1)
        self.assertEqual(self.app._stat_edit_reason(target), "")
        self.app._edit_stat(target, "80000")
        self.assertEqual(self.stat("CarWheel0", "SpringK", 0)["value"], "70000")
        self.assertEqual(self.stat("CarWheel1", "SpringK")["value"], "71000")
        self.assertEqual(float(self.stat("CarWheel0", "SpringK", 1)["value"]), 80000)
        self.assertTrue(self.app.is_dirty())
        self.assertEqual(self.path.read_bytes(), self.disk_before)
        self.assertTrue(self.app.save())
        saved = json.loads(self.path.read_text(encoding="utf-8"))
        car = next(car for car in saved["cars"] if car["id"] == 9006)
        self.assertEqual(len(car["native_overrides"]), len(baseline_native) + 1)
        native = next(item for item in car["native_overrides"] if item["key"] == "SpringK")
        self.assertEqual({key: value for key, value in native.items() if key != "value"},
                         {"section": "CarWheel0", "key": "SpringK", "occurrence": 1})
        self.assertEqual(float(native["value"]), 80000)
        self.app.model.reload()
        self.app._load_car(self.app.current)
        self.assertEqual(float(self.stat("CarWheel0", "SpringK", 1)["value"]), 80000)
        self.app._edit_stat(self.stat("CarWheel0", "SpringK", 1), None)
        self.assertEqual(self.stat("CarWheel0", "SpringK", 1)["value"], "72000")
        self.assertEqual(self.app._draft_car().get("native_overrides"), baseline_native)
        self.error_dialog.assert_not_called()

    def test_native_vector_edit_preserves_shape_and_rejects_invalid_components(self):
        # The donor spells its first coordinate as integer 0; it is still a float.
        self.app._edit_stat(self.stat("CarCamera", "HoodPos"), "0.1 2 3")
        self.assertEqual([float(token) for token in self.stat("CarCamera", "HoodPos")["value"].split()],
                         [0.1, 2, 3])
        before = self.app._draft_car()
        for text in ("1 2", "1 2 nan", "1 2 words"):
            with self.subTest(text=text):
                with self.assertRaises(ValueError):
                    self.app._edit_stat(self.stat("CarCamera", "HoodPos"), text)
                self.assertEqual(self.app._draft_car(), before)

    def test_original_dlc_and_vehicle_identity_cannot_be_edited(self):
        identity = self.stat("CarInfo", "ID")
        self.assertTrue(self.app._stat_edit_reason(identity))
        with self.assertRaises(ValueError):
            self.app._edit_stat(identity, "123")
        before = copy.deepcopy(self.app.model.config)
        for choose, ident in ((self.choose_original, 18), (self.choose_dlc, 201)):
            with self.subTest(ident=ident):
                choose(ident)
                row = self.stat("CarBody", "Mass")
                self.assertTrue(self.app._stat_edit_reason(row))
                with self.assertRaises(ValueError):
                    self.app._edit_stat(row, "999")
                self.assertEqual(self.app.model.config, before)
                self.assertEqual(self.path.read_bytes(), self.disk_before)
        self.error_dialog.assert_not_called()

    def test_factory_profile_keeps_its_managed_fields_locked(self):
        self.app.factory_engine.set(True)
        self.app._show_game_stats()
        for key in ("MaxTorqueRPM", "MaxTurboTorqueRPM", "TurboSize"):
            with self.subTest(key=key):
                row = self.stat("CarBody", key)
                self.assertTrue(self.app._stat_edit_reason(row))
                with self.assertRaises(ValueError):
                    self.app._edit_stat(row, "1000")
        self.assertEqual(self.app._stat_edit_reason(self.stat("CarBody", "TorqueScale")), "")

    def test_missing_donor_preview_still_edits_supported_physics_and_ratings(self):
        car = next(car for car in self.app.model.config["cars"]
                   if car["donor"] not in self.app.original_by_alias)
        self.choose_added(car["id"])
        self.app._edit_stat(self.stat("Simulation", "Mass"), "999")
        self.app._edit_stat(self.stat("Ratings", "TOPSPEED"), "9")
        self.assertEqual(self.app.physics_vars["Mass"].get(), "999")
        self.assertEqual(self.app.rating_vars["TOPSPEED"].get(), "9")
        self.assertEqual(self.stat("Simulation", "Mass")["value"], "999")
        self.assertEqual(self.stat("Ratings", "TOPSPEED")["value"], "9")
        self.assertEqual(self.path.read_bytes(), self.disk_before)

    def test_dial_edit_keeps_both_units_and_camera_modes_in_sync(self):
        previous_mph = self.app._draft_car()["speedometer"]["max_mph"]
        self.app._edit_stat(self.stat("CarCamera", "SpeedometerMaxKmh"), "500")
        self.assertEqual(self.app._draft_car()["speedometer"], {"max_kmh": 500, "max_mph": previous_mph})
        self.assertEqual(self.stat("CarCamera", "SpeedometerMaxKmh")["value"], "500")
        self.assertEqual(self.stat("CarCameraSS", "SpeedometerMaxKmh")["value"], "500")
        self.app._edit_stat(self.stat("CarCameraSS", "SpeedometerMaxMph"), "310")
        self.assertEqual(self.stat("CarCamera", "SpeedometerMaxMph")["value"], "310")
        self.assertEqual(self.stat("CarCameraSS", "SpeedometerMaxMph")["value"], "310")
        before = self.app._draft_car()
        with self.assertRaises(ValueError):
            self.app._edit_stat(self.stat("CarCamera", "SpeedometerMaxKmh"), "99999")
        self.assertEqual(self.app._draft_car(), before)
        self.app._edit_stat(self.stat("CarCameraSS", "SpeedometerMaxMph"), None)
        self.assertNotIn("speedometer", self.app._draft_car())
        self.assertEqual([var.get() for var in self.app.speed_vars.values()], ["", ""])

    def test_first_native_dial_edit_preserves_the_other_displayed_camera_limit(self):
        self.app._edit_stat(self.stat("CarCamera", "SpeedometerMaxKmh"), None)
        self.assertNotIn("speedometer", self.app._draft_car())
        # HUD type 0 has 300/150 artwork, whereas these camera values are 620/500.
        self.assertEqual(self.stat("CarInfo", "SPEEDOMETERTYPE")["value"], "0")
        self.assertEqual(float(self.stat("CarCamera", "SpeedometerMaxMph")["value"]), 500)
        self.app._edit_stat(self.stat("CarCamera", "SpeedometerMaxKmh"), "650")
        self.assertEqual(self.app._draft_car()["speedometer"], {"max_kmh": 650, "max_mph": 500})
        for section in ("CarCamera", "CarCameraSS"):
            self.assertEqual(float(self.stat(section, "SpeedometerMaxKmh")["value"]), 650)
            self.assertEqual(float(self.stat(section, "SpeedometerMaxMph")["value"]), 500)

    def test_clear_overrides_removes_native_driving_edits_and_keeps_camera_edits(self):
        self.app.clear_overrides()
        self.assertTrue(self.app.save())
        saved_bytes = self.path.read_bytes()
        self.app._edit_stat(self.stat("CarBody", "RollLevel"), "1.5")
        self.app._edit_stat(self.stat("CarWheel0", "SpringK"), "80000.5")
        self.app._edit_stat(self.stat("CarCamera", "HoodPos"), "0.1 2 3")
        self.assertTrue(self.app.override_summary.cget("text").startswith("2 values changed"))
        self.assertTrue(self.app.is_dirty())
        self.app.clear_overrides()
        self.assertTrue(self.app.override_summary.cget("text").startswith("0 values changed"))
        self.assertEqual(float(self.stat("CarBody", "RollLevel")["value"]), 1)
        self.assertEqual(float(self.stat("CarWheel0", "SpringK")["value"]), 70000)
        self.assertEqual([float(token) for token in self.stat("CarCamera", "HoodPos")["value"].split()],
                         [0.1, 2, 3])
        native = self.app._draft_car()["native_overrides"]
        self.assertEqual([(item["section"], item["key"]) for item in native], [("CarCamera", "HoodPos")])
        self.assertTrue(self.app.is_dirty())
        self.assertEqual(self.path.read_bytes(), saved_bytes)
        self.app._edit_stat(self.stat("CarCamera", "HoodPos"), None)
        self.assertFalse(self.app.is_dirty())
        self.assertEqual(self.path.read_bytes(), saved_bytes)

    def test_editor_dialog_keeps_invalid_value_for_correction_and_cancel_discards_it(self):
        browser = self.app.stats_view
        before = self.app._draft_car()
        self.select_stat("CarBody", "Mass")
        browser.edit_selected_value()
        window = browser._edit_window
        self.assertTrue(window.winfo_exists())
        browser.edit_var.set("50000")
        browser._commit_edit()
        self.assertTrue(window.winfo_exists())
        self.assertTrue(browser.edit_error_var.get())
        self.assertEqual(self.app._draft_car(), before)
        browser.edit_var.set("999")
        browser._commit_edit()
        self.assertFalse(window.winfo_exists())
        self.assertEqual(self.app.physics_vars["Mass"].get(), "999")
        self.select_stat("CarBody", "Mass")
        browser.edit_selected_value()
        browser.edit_var.set("1234")
        browser._close_edit()
        self.assertIsNone(browser._edit_window)
        self.assertEqual(self.app.physics_vars["Mass"].get(), "999")
        self.assertEqual(self.path.read_bytes(), self.disk_before)
        self.error_dialog.assert_not_called()

    def test_changing_car_closes_the_editor_without_applying_pending_text(self):
        browser = self.app.stats_view
        self.select_stat("CarBody", "Mass")
        browser.edit_selected_value()
        browser.edit_var.set("1234")
        self.choose_added(9004)
        self.assertIsNone(browser._edit_window)
        self.assertEqual(self.app.physics_vars["Mass"].get(), "980")
        self.choose_added(9006)
        self.assertEqual(self.app.physics_vars["Mass"].get(), "600")
        self.assertEqual(self.path.read_bytes(), self.disk_before)


if __name__ == "__main__":
    import unittest
    unittest.main()
