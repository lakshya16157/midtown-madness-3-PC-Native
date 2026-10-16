"""Native Game stats edits keep donor structure and installer/preview parity."""
import copy
import importlib.util
import io
import json
from pathlib import Path
import unittest
import zipfile


ROOT = Path(__file__).resolve().parents[1]


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


prepare = load_module("native_override_prepare_test", ROOT / "tools/car-mods/prepare.py")
native = prepare.native_overrides
fixtures = load_module("native_override_fixtures", ROOT / "tests/test_car_mods.py")
defaults = load_module("native_override_defaults_test", ROOT / "tools/car-mods/driving_defaults.py")


def override(section, key, value, occurrence=0):
    return {"section": section, "key": key, "occurrence": occurrence, "value": value}


def tweak():
    return fixtures.retail_tweak(11, "Viper").replace(
        b"End(CarBody)", b"\tbUseSpinTorque\tfalse\r\nEnd(CarBody)")


class NativeValueValidationTest(unittest.TestCase):
    def test_accepts_tuning_numbers_vectors_and_booleans_with_original_types(self):
        self.assertEqual(native.edit_reason("CarWheel0", "SpringK", "70000.0"), "")
        self.assertEqual(native.validate_value("70000.0", "85000"), "85000")
        self.assertEqual(native.validate_value("3.25 2.45 1.5", "3 2.2 -1e-1"), "3 2.2 -1e-1")
        self.assertEqual(native.validate_value("0 1 2", "-1 +2 0x3"), "-1 +2 0x3")
        self.assertEqual(native.validate_value("false", "TRUE"), "true")
        self.assertEqual(native.validate_value("1.0 // donor comment", "2"), "2")

    def test_rejects_changed_scalar_types_and_vector_arity(self):
        for original, value in (("1.0", "true"), ("false", "1"),
                                ("1 2 3", "1 2"), ("1", "1 2")):
            with self.subTest(original=original, value=value), self.assertRaises(ValueError):
                native.validate_value(original, value)

    def test_rejects_nonfinite_out_of_native_range_and_parser_injection(self):
        for value in ("NaN", "inf", "-inf", "1e999", "3.5e38", "",
                      "1\nEnd(CarBody)", "1\rMass 3", "1\x00", "1 // comment", "1;",
                      "1 # comment", "Begin(CarBody)", "1\v2", "1\u20282", "1, 2", True, 1, None):
            with self.subTest(value=value), self.assertRaises(ValueError):
                native.validate_value("1.0", value)

    def test_integer_spelled_native_float_fields_accept_fractional_tuning(self):
        self.assertEqual(native.validate_value("0 0.727769 0.547450", "0.1 0.7 0.5"),
                         "0.1 0.7 0.5")
        self.assertEqual(native.validate_value("70000", "70000.5"), "70000.5")
        self.assertEqual(native.validate_value("1.0", "3000000000"), "3000000000")
        self.assertEqual(native.validate_value("1.0", "3e9"), "3e9")
        text = "Begin(CarWheel0)\nSpringK 70000\nEnd(CarWheel0)\n"
        self.assertIn("SpringK 70000.5", native.apply_overrides(
            text, [override("CarWheel0", "SpringK", "70000.5")]))

    def test_native_zero_one_boolean_fields_keep_boolean_domain(self):
        text = "Begin(CarBody)\nUseAbsSystem 0\nEnd(CarBody)\n"
        self.assertIn("UseAbsSystem 1", native.apply_overrides(
            text, [override("CarBody", "UseAbsSystem", "1")]))
        for value in ("0.5", "2", "1.0", "true"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                native.apply_overrides(text, [override("CarBody", "UseAbsSystem", value)])

    def test_metadata_counts_references_and_existing_managed_fields_are_protected(self):
        targets = [("CarInfo", "ID"), ("CarInfo", "TOPSPEED"), ("CarInfo", "COLOR"),
                   ("CarBody", "NWheels"), ("CarBody", "NColPoints"), ("CarBody", "NGears"),
                   ("CarBody", "GearRatioVector"), ("CarBody", "TorqueScale"),
                   ("CarBody", "TurboSpinup"), ("CarBody", "TyreType"),
                   ("CarBody", "Transmission"), ("CarBody", "RpmTable"),
                   ("CarWheel0", "MeshName"), ("CarWheel0", "ModelIndex"),
                   ("CarCamera", "SpeedometerMaxKmh"), ("CarCameraSS", "SpeedometerMaxMph"),
                   ("Global", "Anything")]
        for section, key in targets:
            with self.subTest(section=section, key=key):
                self.assertTrue(native.edit_reason(section, key, "1"))
                with self.assertRaises(ValueError):
                    native.validate_overrides([override(section, key, "2")])
        self.assertTrue(native.edit_reason("CarWheel0", "Sound", "engine.wav"))
        self.assertTrue(native.edit_reason("CarWheel0", "PresenceFlag", ""))

    def test_all_managed_driving_fields_share_the_edit_policy(self):
        # Prevent a future new validated driving field silently acquiring a
        # second, unvalidated native edit path.
        self.assertTrue(set(prepare.PHYSICS) <= native.MANAGED_BODY)
        for key in prepare.PHYSICS:
            with self.subTest(key=key):
                self.assertTrue(native.edit_reason("CarBody", key, "1"))

    def test_manifest_shape_and_duplicate_identity_validation(self):
        valid = override("CarWheel0", "SpringK", "80000")
        cases = [None, {}, "value", [1], [{**valid, "extra": 1}],
                 [{key: value for key, value in valid.items() if key != "value"}],
                 [{**valid, "occurrence": True}], [{**valid, "occurrence": -1}],
                 [{**valid, "section": "CarBody)\n"}], [{**valid, "key": "Spring K"}],
                 [valid, copy.deepcopy(valid)], [{**valid, "value": "1\n2"}]]
        for value in cases:
            with self.subTest(value=value), self.assertRaises(ValueError):
                native.validate_overrides(value)
        native.validate_overrides([valid, {**valid, "occurrence": 1}])


class NativeOverrideScopeTest(unittest.TestCase):
    def test_repeated_keys_and_repeated_sections_match_browser_occurrence_identity(self):
        text = ("Begin(CarWheel0)\r\n\tSpringK\t70000.0\r\n"
                "\tSpringK  71000.0  // retain comment\r\nEnd(CarWheel0)\r\n"
                "Begin(CarWheel1)\r\n\tSpringK\t72000.0\r\nEnd(CarWheel1)\r\n"
                "Begin(CarWheel0)\r\n\tSpringK\t73000.0\r\nEnd(CarWheel0)\r\n")
        result = native.apply_overrides(text, [override("CarWheel0", "SpringK", "81000", 1),
                                                override("CarWheel0", "SpringK", "83000", 2)])
        expected = text.replace("71000.0", "81000").replace("73000.0", "83000")
        self.assertEqual(result, expected)
        sections = prepare.original_stats.parse_sections(result)
        values = [raw for section in sections if section["name"] == "CarWheel0"
                  for key, raw in section["entries"] if key == "SpringK"]
        self.assertEqual(values, ["70000.0", "81000  // retain comment", "83000"])

    def test_matching_is_exact_and_never_adds_missing_entries(self):
        text = tweak().decode("ascii")
        for edit in (override("CarWheel0", "MissingSetting", "1"),
                     override("carwheel0", "SpringK", "1"),
                     override("CarWheel0", "springk", "1"),
                     override("CarWheel0", "SpringK", "1", 1)):
            with self.subTest(edit=edit), self.assertRaisesRegex(ValueError, "not found"):
                native.apply_overrides(text, [edit])

    def test_keeps_donor_structure_unedited_values_comments_and_line_endings(self):
        for ending in ("\n", "\r\n"):
            text = ("; header" + ending + "Begin(CarBody) # body" + ending
                    + "  bUseSpinTorque\tFALSE  ; factory" + ending
                    + "\tBrakeBalance\t1.0\t" + ending + "End(CarBody)" + ending)
            result = native.apply_overrides(text, [override("CarBody", "bUseSpinTorque", "true")])
            self.assertEqual(result, text.replace("FALSE", "true"))
        self.assertEqual(native.apply_overrides("even malformed untouched text", []),
                         "even malformed untouched text")

    def test_empty_override_list_preserves_existing_tweak_bytes(self):
        config = prepare.load_config(ROOT / "tools/car-mods/cars.json")
        for car in config["cars"]:
            edited = copy.deepcopy(car)
            edited["native_overrides"] = []
            absent = copy.deepcopy(edited)
            absent.pop("native_overrides", None)
            self.assertEqual(prepare.tweak_variant(tweak(), absent), prepare.tweak_variant(tweak(), edited))

    def test_existing_nonnumeric_field_cannot_be_changed_to_number(self):
        text = "Begin(CarBody)\n\tSound\tengine.wav\nEnd(CarBody)\n"
        with self.assertRaisesRegex(ValueError, "Only numeric"):
            native.apply_overrides(text, [override("CarBody", "Sound", "1")])

    def test_rejects_malformed_section_structure(self):
        for text in ("Begin(CarBody)\nBegin(CarBody)\nBrakeBalance 1.0\n",
                     "Begin(CarBody)\nBrakeBalance 1.0\nEnd(CarWheel0)\n",
                     "Begin(CarBody)\nBrakeBalance 1.0\n"):
            with self.subTest(text=text), self.assertRaises(ValueError):
                native.apply_overrides(text, [override("CarBody", "BrakeBalance", "2")])


class NativeOverrideIntegrationTest(unittest.TestCase):
    def setUp(self):
        self.fixture = fixtures.CarModsTest(methodName="test_default_manifest_uses_local_file_and_falls_back_to_tracked_defaults")
        self.fixture.setUp()
        self.addCleanup(self.fixture.tearDown)
        self.config = {**self.fixture.config, "cars": [copy.deepcopy(self.fixture.config["cars"][0])]}
        self.originals = self.fixture.with_retail_bases({"Viper": tweak()})
        self.car = self.config["cars"][0]

    def test_config_roundtrip_preview_and_installed_archive_have_the_same_edits(self):
        self.car["native_overrides"] = [override("CarBody", "vInertia", "3.1 2.2 1.3"),
                                         override("CarBody", "bUseSpinTorque", "true"),
                                         override("CarWheel0", "SpringK", "85000"),
                                         override("CarWheel0", "WheelBasePos", "-0.7 0.3 1.4"),
                                         override("CarCamera", "HoodPos", "1 2 3")]
        path = self.fixture.game / "draft.json"
        path.write_text(json.dumps(self.config), encoding="utf-8")
        loaded = prepare.load_config(path)
        self.assertEqual(loaded, self.config)
        preview = prepare.tweak_variant(tweak(), loaded["cars"][0])
        output, _counts = prepare.build_plan(self.fixture.game, self.originals, loaded)
        prepare.apply_plan(self.fixture.game, None, self.originals, output, loaded)
        with zipfile.ZipFile(self.fixture.game / prepare.ARCHIVES[0]) as archive:
            installed = archive.read("Cars/Tweak/Panoz.txt")
            self.assertEqual(archive.read("Cars/Tweak/Viper.txt"), tweak())
        self.assertEqual(installed, preview)
        self.assertIn(b"SpringK\t85000", installed)
        self.assertIn(b"bUseSpinTorque\ttrue", installed)
        state, original = prepare.read_install(self.fixture.game)
        self.assertEqual(state["config"], loaded)
        self.assertEqual(original, self.originals)
        prepare.restore(self.fixture.game, state, original)
        self.assertEqual((self.fixture.game / prepare.ARCHIVES[0]).read_bytes(), self.originals[prepare.ARCHIVES[0]])

    def test_missing_donor_setting_fails_plan_before_any_game_file_is_changed(self):
        self.car["native_overrides"] = [override("CarWheel0", "NoSuchSetting", "1")]
        prepare.validate_config(self.config)
        before = {relative: (self.fixture.game / relative).read_bytes() for relative in prepare.ARCHIVES}
        with self.assertRaisesRegex(ValueError, "not found"):
            prepare.build_plan(self.fixture.game, self.originals, self.config)
        self.assertEqual(before, {relative: (self.fixture.game / relative).read_bytes() for relative in prepare.ARCHIVES})
        self.assertFalse((self.fixture.game / prepare.STATE_DIR).exists())

    def test_removing_override_reverts_to_donor_on_next_plan(self):
        self.car["native_overrides"] = [override("CarWheel0", "SpringK", "80000")]
        edited, _ = prepare.build_plan(self.fixture.game, self.originals, self.config)
        self.car.pop("native_overrides")
        reverted, _ = prepare.build_plan(self.fixture.game, self.originals, self.config)
        with zipfile.ZipFile(io.BytesIO(edited[prepare.ARCHIVES[0]])) as archive:
            self.assertIn(b"SpringK\t80000", archive.read("Cars/Tweak/Panoz.txt"))
        with zipfile.ZipFile(io.BytesIO(reverted[prepare.ARCHIVES[0]])) as archive:
            self.assertIn(b"SpringK\t70000.0", archive.read("Cars/Tweak/Panoz.txt"))

    def test_shipped_driving_reset_removes_body_and_wheel_tuning_only(self):
        self.car["native_overrides"] = [override("CarBody", "vInertia", "3.1 2.2 1.3"),
                                         override("CarWheel0", "SpringK", "85000"),
                                         override("CarCamera", "HoodPos", "1 2 3"),
                                         override("CarCameraSS", "OutFarStartZ", "7")]
        before = copy.deepcopy(self.car)
        expected = self.car["native_overrides"][2:]
        reset = defaults.apply_preset(self.car)
        self.assertEqual(reset["native_overrides"], expected + defaults.stability.profile(self.car["alias"]))
        self.assertEqual(native.without_driving(self.car["native_overrides"]), expected)
        self.assertEqual(self.car, before)
        reset["native_overrides"][0]["value"] = "2 3 4"
        self.assertEqual(self.car, before)
        self.car["native_overrides"] = self.car["native_overrides"][:2]
        self.assertEqual(defaults.apply_preset(self.car)["native_overrides"],
                         defaults.stability.profile(self.car["alias"]))


if __name__ == "__main__":
    unittest.main()
