"""Game-wide maxima and per-car values used by the Car Workshop tables."""
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("car_tuning_reference_test", ROOT / "tools/car-mods/tuning_reference.py")
REF = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(REF)


def car(name, body, ratings=None, dial=None):
    """A reference car in original_stats' raw-section shape."""
    sections = [{"name": "CarInfo", "entries": [(key, str(value)) for key, value in (ratings or {}).items()]},
                {"name": "CarBody", "entries": [(key, value) for key, value in body.items()]}]
    if dial:
        sections.append({"name": "CarCamera", "entries": [("SpeedometerMaxKmh", str(dial[0])),
                                                          ("SpeedometerMaxMph", str(dial[1]))]})
    return {"display_name": name, "alias": name.replace(" ", ""), "sections": sections}


CARS = [
    car("Lotus Esprit V8", {"Mass": "1380", "TorqueScale": "700", "BrakeTorque": "4000.0",
                            "WheelDrive": "1", "GearRatioVector": "-3.3 2.90 1.77 1.21 0.92 0.81\t",
                            "LatFrictionFactor": "2.0"},
        {"TOPSPEED": 8, "MASS": 3}, (620, 500)),
    car("Paris Bus", {"Mass": "15000", "TorqueScale": "1000", "BrakeTorque": "20000.0",
                      "WheelDrive": "2", "GearRatioVector": "-6.0 3.60 2.64 2.14 1.87 1.65 1.66"},
        {"TOPSPEED": 2, "MASS": 9}),
    car("Dodge Viper", {"Mass": "1600", "TorqueScale": "1000", "BrakeTorque": "4000.0",
                        "WheelDrive": "1", "GearRatioVector": "-3.3 2.66 1.78 1.30 1.00 0.74 0.50",
                        "LatFrictionFactor": "2.0"},
        {"TOPSPEED": 8, "MASS": 4}, (620, 500)),
    car("Microcar", {"Mass": "900", "TorqueScale": "150", "BrakeTorque": "4000.0",
                     "WheelDrive": "0", "GearRatioVector": "-3.3 7.52 4.0 2.5 1.5 1.0",
                     "LatFrictionFactor": "1.0"},
        {"TOPSPEED": 1, "MASS": 1}),
]


class TuningReferenceTest(unittest.TestCase):
    def setUp(self):
        self.extremes = REF.game_extremes(CARS)

    def test_highest_value_names_first_car_and_counts_ties(self):
        self.assertEqual(REF.short_high(self.extremes["Mass"]), "15000 · Paris Bus")
        self.assertEqual(REF.short_high(self.extremes["TorqueScale"]), "1000 · Paris Bus +1")
        self.assertEqual(self.extremes["TorqueScale"].high_names, ["Paris Bus", "Dodge Viper"])
        self.assertEqual(REF.short_high(self.extremes["rating.TOPSPEED"]), "8 · Lotus Esprit V8 +1")
        self.assertEqual(REF.short_high(None), "—")

    def test_identical_values_and_missing_keys_are_described(self):
        brakes = self.extremes["BrakeTorque"]
        self.assertEqual((brakes.low, brakes.high), (4000.0, 20000.0))
        grip = self.extremes["LatFrictionFactor"]
        self.assertEqual((grip.present, grip.total), (3, 4))
        self.assertIn("1 car leaves it at the game default", REF.long_range(grip))
        same = REF.game_extremes([CARS[0], CARS[2]])
        self.assertEqual(REF.short_high(same["BrakeTorque"]), "4000 · all 2 cars")
        self.assertIn("Every game car that stores it uses 4000", REF.long_range(same["BrakeTorque"]))
        self.assertIn("lowest 900 (Microcar), highest 15000 (Paris Bus)", REF.long_range(self.extremes["Mass"]))

    def test_gears_track_reverse_magnitude_first_gear_and_count(self):
        self.assertEqual(self.extremes["gear.R"].high, 6.0)
        self.assertEqual(REF.short_high(self.extremes["gear.R"], "gear.R"), "-6 · Paris Bus")
        self.assertEqual(self.extremes["gear.1"].high, 7.52)
        self.assertEqual(self.extremes["gear.1"].high_names, ["Microcar"])
        self.assertEqual(self.extremes["gear.count"].high, 6)
        self.assertEqual(self.extremes["gear.count"].low, 5)

    def test_driven_wheels_report_distribution(self):
        self.assertEqual(REF.short_high(self.extremes["WheelDrive"], "WheelDrive"), "F 1 · R 2 · AWD 1")
        self.assertIn("2 rear", REF.long_range(self.extremes["WheelDrive"], "WheelDrive"))

    def test_car_values_read_sections_or_plain_dictionaries(self):
        values = REF.car_values(CARS[0])
        self.assertEqual(values["physics"]["Mass"], 1380)
        self.assertEqual(values["physics"]["GearRatioVector"], [-3.3, 2.9, 1.77, 1.21, 0.92, 0.81])
        self.assertEqual(values["ratings"], {"TOPSPEED": 8, "MASS": 3})
        self.assertEqual(values["dial"], {"max_kmh": 620, "max_mph": 500})
        plain = REF.car_values({"physics": {"Mass": 980}, "ratings": {"HANDLING": 8}})
        self.assertEqual(plain, {"physics": {"Mass": 980}, "ratings": {"HANDLING": 8}, "dial": {}})
        self.assertEqual(REF.car_values(None), {"physics": {}, "ratings": {}, "dial": {}})
        self.assertEqual(self.extremes["dial.max_kmh"].high_names, ["Lotus Esprit V8", "Dodge Viper"])

    def test_number_formatting_flags_and_limits(self):
        self.assertEqual([REF.format_number(value) for value in (6500.0, 0.01, 1.125, 15000, 2)],
                         ["6500", "0.01", "1.125", "15000", "2"])
        self.assertEqual(REF.outside_flag(16000, self.extremes["Mass"]), " ▲")
        self.assertEqual(REF.outside_flag(500, self.extremes["Mass"]), " ▼")
        self.assertEqual(REF.outside_flag(1500, self.extremes["Mass"]), "")
        self.assertEqual(REF.outside_flag(None, self.extremes["Mass"]), "")
        limits = {"MaxTorqueRPM": (500, 20000), "Mass": (100, 10000)}
        self.assertEqual(REF.allowed_range(limits, "MaxTorqueRPM"), (500, 8000))
        self.assertEqual(REF.allowed_range(limits, "MaxTorqueRPM", factory=True), (500, 20000))
        self.assertEqual(REF.allowed_range(limits, "Mass"), (100, 10000))

    def test_dlc_twins_require_physics_id_and_identical_body(self):
        lotus = {**CARS[0], "id": 18, "alias": "Lotus"}
        viper = {**CARS[2], "id": 11, "alias": "Viper"}

        def dlc(name, physics_id, body_source, change=None):
            body = [(key, value) for section in body_source["sections"] if section["name"] == "CarBody"
                    for key, value in section["entries"]]
            if change:
                body = [(key, change.get(key, value)) for key, value in body]
            return {"display_name": name, "id": 201, "alias": "car201", "sections": [
                {"name": "CarInfo", "entries": [("ID", "201"), ("PHYSICSID", str(physics_id))]},
                {"name": "CarBody", "entries": body}]}

        twins = REF.dlc_twins([lotus, viper], [
            dlc("Campione", 18, lotus), dlc("Furious", 18, lotus),
            dlc("Tuned Copy", 18, lotus, {"Mass": "1200"}), dlc("Orphan", 99, viper)])
        self.assertEqual(twins, {"Lotus": ["Campione", "Furious"]})
        self.assertEqual(REF.dlc_twins([], None), {})

    def test_spin_value_starts_from_inherited_and_clamps(self):
        self.assertEqual(REF.spin_value("", 4000, 1, 250, 100, 30000, "%.0f"), "4250")
        self.assertEqual(REF.spin_value("", 4000, -1, 250, 100, 30000, "%.0f"), "3750")
        self.assertEqual(REF.spin_value("", 15000, 1, 10, 100, 10000, "%.0f"), "10000")
        self.assertEqual(REF.spin_value("1.125", None, 1, 0.025, 0.1, 5, "%.3f"), "1.150")
        self.assertEqual(REF.spin_value("", None, 1, 10, 100, 10000, "%.0f"), "110")
        self.assertEqual(REF.spin_value("abc", 500, 1, 10, 100, 10000, "%.0f"), "510")
        for key, (step, fmt) in REF.STEPS.items():
            self.assertGreater(step, 0, key)
            self.assertTrue(fmt.startswith("%."), key)

    def test_gear_text_and_tokens_roundtrip_and_mark_holes(self):
        self.assertEqual(REF.gear_tokens("[-3.3, 2.9,1.8  1.2]"), ["-3.3", "2.9", "1.8", "1.2"])
        self.assertEqual(REF.gear_tokens("  "), [])
        self.assertEqual(REF.gear_text(["-3.3", "2.9", "1.8", "", ""]), "-3.3, 2.9, 1.8")
        self.assertEqual(REF.gear_text(["-3.3", "", "1.8"]), "-3.3, ?, 1.8")
        self.assertEqual(REF.gear_text(["", ""]), "")

    def test_native_extremes_cover_any_numeric_section_value(self):
        mass = REF.native_extremes(CARS, "CarBody", "Mass")
        self.assertEqual((mass.low, mass.high, mass.total), (900, 15000, 4))
        self.assertIsNone(REF.native_extremes(CARS, "CarBody", "GearRatioVector"))
        self.assertIsNone(REF.native_extremes(CARS, "CarBody", "NoSuchKey"))


if __name__ == "__main__":
    unittest.main()
