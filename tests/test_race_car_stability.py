"""Race-car handling repair survives installation and the Workshop reset."""
import copy
import importlib.util
import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


stability = load("race_stability_test", ROOT / "tools/car-mods/race_stability.py")
defaults = load("race_defaults_test", ROOT / "tools/car-mods/driving_defaults.py")
prepare = load("race_prepare_test", ROOT / "tools/car-mods/prepare.py")
fixtures = load("race_fixture_test", ROOT / "tests/test_car_mods.py")


class RaceCarStabilityTest(unittest.TestCase):
    def test_shipped_and_reset_profiles_fix_all_corners_without_changing_other_cars(self):
        for filename in ("cars.json", "cars.imported.json"):
            config = json.loads((ROOT / "tools/car-mods" / filename).read_text())
            for car in config["cars"]:
                profile = stability.profile(car["alias"])
                self.assertEqual(car.get("native_overrides", []), profile)
                self.assertEqual(defaults.apply_preset(car).get("native_overrides", []), profile)
                donor = fixtures.retail_tweak(car["id"], car["donor"])
                installed = prepare.tweak_variant(donor, car).decode()
                sections = prepare.original_stats.parse_sections(installed)
                for section in sections:
                    if section["name"].startswith("CarWheel"):
                        values = dict(section["entries"])
                        expected = stability.ROLL_STIFFNESS if profile else 14200
                        self.assertEqual(float(values["RollStiffness"]), expected)
                        if "SpringK" in values:
                            self.assertEqual(float(values["SpringK"]), 70000)

    def test_repair_preserves_local_engine_and_camera_and_is_idempotent(self):
        car = {"alias": "Form1", "physics": {"Mass": 600, "TorqueScale": 420,
               "LatFrictionFactor": 4.5, "GearRatioVector": [-3.3, 2, 1.4, .8, .6]},
               "native_overrides": [
                   {"section": "CarWheel0", "key": "RollStiffness", "occurrence": 0, "value": "1"},
                   {"section": "CarCamera", "key": "HoodPos", "occurrence": 0, "value": "0 1 2"}]}
        before = copy.deepcopy(car)
        repaired = stability.apply_profile(car, limit_excess_grip=True)
        self.assertEqual(car, before)
        self.assertEqual(repaired["physics"], {**car["physics"], "LatFrictionFactor": 2.2})
        self.assertEqual(repaired["native_overrides"], [car["native_overrides"][1]] + stability.profile("Form1"))
        self.assertEqual(stability.apply_profile(repaired, limit_excess_grip=True), repaired)
        ordinary = {**car, "alias": "McLaren_F1"}
        self.assertEqual(stability.apply_profile(ordinary, limit_excess_grip=True), ordinary)


if __name__ == "__main__":
    unittest.main()
