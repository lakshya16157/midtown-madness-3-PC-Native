"""Help coverage and context safety for native Workshop reference rows."""
import importlib.util
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


HELP = load_module("car_help_test", ROOT / "tools/car-mods/field_help.py")
PREPARE = load_module("car_help_prepare_test", ROOT / "tools/car-mods/prepare.py")
ORIGINAL = load_module("car_help_original_test", ROOT / "tools/car-mods/original_stats.py")
BROWSER = load_module("car_help_browser_test", ROOT / "tools/car-mods/stats_browser.py")


class FieldHelpTest(unittest.TestCase):
    def test_every_editable_value_has_specific_help(self):
        for key in {*PREPARE.PHYSICS, "GearRatioVector"}:
            with self.subTest(section="physics", key=key):
                self.assertIn(key, HELP.PHYSICS_HELP)
                self.assertNotIn("have not been established", HELP.tip_for("CarBody", key))
        for key in PREPARE.RATINGS:
            with self.subTest(section="ratings", key=key):
                self.assertIn(key, HELP.RATING_HELP)
                self.assertNotIn("have not been established", HELP.tip_for("CarInfo", key))
        for key in ("model_package", "engine_profile", "max_kmh", "max_mph", "colors", "driving_base"):
            self.assertIn(key, HELP.OTHER_HELP)
        self.assertEqual(HELP.LABELS["driving_base"], "Base stats")
        self.assertIn("menu ratings do not change", HELP.OTHER_HELP["driving_base"])

    def test_raw_native_sections_keep_rating_and_physics_mass_distinct(self):
        sections = ORIGINAL.parse_sections(
            "Begin(CarInfo)\nMASS 3\nEnd(CarInfo)\n"
            "Begin(CarBody)\nMass 1380\nWheelRadius 0.34\nEnd(CarBody)\n"
            "Begin(CarWheel0)\nWheelBasePos -0.765 0.23 1.39\nSpringK 70000\nEnd(CarWheel0)\n"
            "Begin(CarCameraSS)\nHoodPos 0 0.7 0.5\nOutNearStartY 1\nEnd(CarCameraSS)\n")
        tips = {(section["name"], key): HELP.tip_for(section["name"], key)
                for section in sections for key, _value in section["entries"]}
        self.assertIn("menu bar", tips["CarInfo", "MASS"])
        self.assertIn("kg", tips["CarBody", "Mass"])
        self.assertNotEqual(tips["CarInfo", "MASS"], tips["CarBody", "Mass"])
        self.assertIn("actual rolling radius", tips["CarBody", "WheelRadius"])
        for identity, tip in tips.items():
            with self.subTest(identity=identity):
                self.assertNotIn("have not been established", tip)

    def test_unknown_fields_identify_source_without_inventing_an_effect(self):
        for section, key in (("CarBody", "FutureTorqueControl"),
                             ("FutureSection", "Mass"), ("CarInfo", "Mass")):
            with self.subTest(section=section, key=key):
                tip = HELP.tip_for(section, key)
                self.assertIn("Underlying MM3 config field", tip)
                self.assertIn(section, tip)
                self.assertIn(key, tip)
                self.assertIn("have not been established", tip)
                self.assertNotIn("Higher", tip)

    def test_browser_without_retail_assets_still_explains_saved_overrides(self):
        car = {"ratings": {"MASS": 3}, "physics": {"Mass": 1380, "WheelDrive": 1}}
        tips = {key: HELP.tip_for(section, key)
                for _identity, section, key, _value in BROWSER._entries(car)}
        self.assertIn("menu bar", tips["MASS"])
        self.assertIn("kg", tips["Mass"])
        self.assertIn("rear", tips["WheelDrive"])
        for tip in tips.values():
            self.assertNotIn("have not been established", tip)

    def test_installed_dlc_metadata_explains_vehicle_and_physics_id_separately(self):
        sections = ORIGINAL.parse_sections(
            "Begin(CarInfo)\nID 201\nPHYSICSID 18\n"
            "TOPSPEED 8\nPAINTJOBNAME Lotus\nEnd(CarInfo)\n")
        tips = {key: HELP.tip_for(section["name"], key)
                for section in sections for key, _value in section["entries"]}
        self.assertNotEqual(tips["ID"], tips["PHYSICSID"])
        self.assertIn("separate from the vehicle ID", tips["PHYSICSID"])
        self.assertIn("not a performance rating", tips["PHYSICSID"])
        for tip in tips.values():
            self.assertNotIn("have not been established", tip)


if __name__ == "__main__":
    unittest.main()
