"""Offline fixtures for the workshop's read-only original car reference."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("car_original_stats", ROOT / "tools/car-mods/original_stats.py")
MOD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MOD)


def tweak(ident=18, torque=700):
    return ("// native tweak fixture\r\n"
            "Begin(CarInfo)\r\n"
            f"\tID\t{ident}\r\n"
            "\tTOPSPEED\t8\r\n\tACCELERATION\t8\r\n"
            "\tCOLOR\t0 0xa3a9af 0xa3a9af\r\n"
            "\tCOLOR\t1 0x031aa8 0x031aa8\r\n"
            "\tSKIN\t1 0xdeebef 0x5a718c\r\n"
            "\tLOCKED\r\n\tPAINTJOBNAME\tLotus\r\n"
            "End(CarInfo)\r\n"
            "Begin(CarBody)\r\n"
            "\tMass\t1380\r\n"
            f"\tTorqueScale\t{torque}\r\n"
            "\tGearRatioVector\t-3.3 2.90 1.77 1.21 0.92 0.81\r\n"
            "\tvInertia\t3.25 2.45 1.5\r\n"
            "\tUseAbsSystem\tfalse\r\n"
            "\tModelName\tLotus body\r\n"
            "End(CarBody)\r\n"
            "Begin(CarWheel0)\r\n"
            "\tWheelBasePos\t-0.765 0.23 1.39\r\n"
            "\tSpringK\t70000.0\r\nEnd(CarWheel0)\r\n"
            "Begin(CarCamera)\r\nHoodPos\t0 0.727769 0.547450\r\n"
            "SpeedometerMaxKmh\t620.0\r\nEnd(CarCamera)\r\n"
            "Begin(OtherSection)\r\n\tFutureValue\t1 2 3\r\n"
            "\tFutureValue\t4 5 6\r\nEnd(OtherSection)\r\n")


def archive(path, entries):
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as target:
        for name, text in entries.items():
            target.writestr(name, text.encode("cp1252"))


def dlc_pack(game_dir, name="4d53002a00000001", installed=True):
    folder = game_dir / "TDATA/4d53002a/$C" / name
    folder.mkdir(parents=True, exist_ok=True)
    if installed:
        (folder / "contentmeta.xbx").write_bytes(b"installed fixture metadata")
    return folder


def loose_dlc(pack, ident, torque=700, folder="Cars/Tweak"):
    path = pack / folder / f"car{ident}.txt"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(tweak(ident, torque).encode("cp1252"))
    return path


class OriginalStatsTest(unittest.TestCase):
    def test_all_raw_values_duplicates_unknown_sections_and_vectors_survive(self):
        raw = tweak()
        sections = MOD.parse_sections(raw)
        self.assertEqual([section["name"] for section in sections],
                         ["CarInfo", "CarBody", "CarWheel0", "CarCamera", "OtherSection"])
        colors = [(key, value) for key, value in sections[0]["entries"] if key == "COLOR"]
        self.assertEqual(colors, [("COLOR", "0 0xa3a9af 0xa3a9af"), ("COLOR", "1 0x031aa8 0x031aa8")])
        self.assertIn(("LOCKED", ""), sections[0]["entries"])
        self.assertEqual(sections[-1]["entries"], [("FutureValue", "1 2 3"), ("FutureValue", "4 5 6")])
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            archive(folder / "Data/Data_hd.zip", {"Cars/Tweak/Lotus.txt": raw})
            car, = MOD.load_original_cars(folder)
        self.assertEqual(car["raw_text"], raw)
        self.assertEqual(car["sections"], sections)
        self.assertEqual(car["display_name"], "Lotus Esprit V8")
        self.assertEqual(car["ratings"], {"TOPSPEED": 8, "ACCELERATION": 8})
        self.assertEqual(car["physics"]["Mass"], 1380)
        self.assertEqual(car["physics"]["GearRatioVector"], [-3.3, 2.90, 1.77, 1.21, 0.92, 0.81])
        self.assertEqual(car["physics"]["vInertia"], [3.25, 2.45, 1.5])
        self.assertIs(car["physics"]["UseAbsSystem"], False)
        self.assertEqual(car["physics"]["ModelName"], "Lotus body")
        self.assertIn("Data_hd.zip :: Cars/Tweak/Lotus.txt", car["source"])

    def test_typed_values_preserve_strings_and_convert_numeric_and_flag_forms(self):
        for raw, expected in [("", True), ("false", False), ("TRUE", True),
                              ("003", 3), ("-2", -2), ("0x22", 34),
                              ("1e3", 1000.0), ("-.5", -.5),
                              ("1 2.5 -3", [1, 2.5, -3]),
                              ("620.0 // dial", 620.0),
                              ("Lotus Esprit V8", "Lotus Esprit V8"),
                              ("1e999", "1e999"), ("NaN", "NaN")]:
            with self.subTest(raw=raw):
                self.assertEqual(MOD.typed_value(raw), expected)

    def test_repeated_sections_and_outside_values_are_not_dropped(self):
        sections = MOD.parse_sections("\ufeffVersion 2\nBegin(Custom)\nValue 1\nEnd(Custom)\n"
                                      "Begin(Custom)\nValue 2\nEnd(Custom)\nTail yes\n")
        self.assertEqual(sections, [
            {"name": "Global", "entries": [("Version", "2")]},
            {"name": "Custom", "entries": [("Value", "1")]},
            {"name": "Custom", "entries": [("Value", "2")]},
            {"name": "Global", "entries": [("Tail", "yes")]},
        ])

    def test_every_retail_id_is_visible_in_retail_order_while_extras_are_filtered(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            # Deliberately reverse ZIP order and omit SortOrder. DLC and model
            # tweaks have valid IDs but cannot enter the reference catalog.
            entries = {f"Cars/Tweak/Car{ident}.txt": tweak(ident)
                       for ident in reversed(MOD.ORIGINAL_CAR_IDS)}
            entries.update({"Cars/Tweak/Dlc.txt": tweak(208), "Cars/Tweak/Panoz.txt": tweak(9004),
                            "Cars/Tweak/SortOrder": "9004\n208\n",
                            "Cars/Elsewhere/NotATweak.txt": tweak(18)})
            archive(folder / "Data/Data_hd.zip", entries)
            cars = MOD.load_original_cars(folder)
        self.assertEqual([car["id"] for car in cars], list(MOD.ORIGINAL_CAR_IDS))
        self.assertEqual(len(cars), 35)

    def test_backup_is_preferred_without_consulting_installer_state(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            archive(folder / "Data/Data_hd.zip", {"Cars/Tweak/Lotus.txt": tweak(torque=4000)})
            archive(folder / ".mm3-car-mods/Data_hd.zip", {"Cars/Tweak/Lotus.txt": tweak(torque=700)})
            # Other archive backups / integrity metadata are not necessary to
            # view original references, and an invalid state file is harmless.
            (folder / ".mm3-car-mods/state.json").write_text("invalid state")
            car, = MOD.load_original_cars(folder)
            self.assertEqual(car["physics"]["TorqueScale"], 700)
            self.assertIn(".mm3-car-mods", car["source"])

    def test_reading_is_completely_read_only(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            archive(folder / "Data/Data_hd.zip", {"Cars/Tweak/Lotus.txt": tweak()})
            (folder / "settings.json").write_text("keep me")
            before = {str(path.relative_to(folder)): (path.read_bytes(), path.stat().st_mtime_ns)
                      for path in folder.rglob("*") if path.is_file()}
            directories = {str(path.relative_to(folder)) for path in folder.rglob("*") if path.is_dir()}
            MOD.load_original_cars(folder)
            after = {str(path.relative_to(folder)): (path.read_bytes(), path.stat().st_mtime_ns)
                     for path in folder.rglob("*") if path.is_file()}
            self.assertEqual(before, after)
            self.assertEqual(directories, {str(path.relative_to(folder)) for path in folder.rglob("*") if path.is_dir()})
            self.assertFalse((folder / ".mm3-car-mods").exists())

    def test_missing_archive_has_actionable_message(self):
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaisesRegex(MOD.OriginalStatsError, "Choose an extracted MM3 game folder"):
                MOD.load_original_cars(temporary)

    def test_corrupt_backup_does_not_fall_back_to_modified_install(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            archive(folder / "Data/Data_hd.zip", {"Cars/Tweak/Lotus.txt": tweak(torque=4000)})
            backup = folder / ".mm3-car-mods/Data_hd.zip"
            backup.parent.mkdir()
            backup.write_bytes(b"not a ZIP")
            with self.assertRaisesRegex(MOD.OriginalStatsError, r"Could not read.*\.mm3-car-mods"):
                MOD.load_original_cars(folder)

    def test_malformed_sections_and_duplicate_original_ids_have_clear_errors(self):
        for raw in ["Begin(CarBody)\nMass 1", "End(CarBody)",
                    "Begin(CarInfo)\nEnd(CarBody)", "Begin(CarInfo)\nBegin(CarBody)"]:
            with self.subTest(raw=raw), self.assertRaises(MOD.OriginalStatsError):
                MOD.parse_sections(raw)
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            archive(folder / "Data/Data_hd.zip", {"Cars/Tweak/Lotus.txt": tweak(), "Cars/Tweak/Other.txt": tweak()})
            with self.assertRaisesRegex(MOD.OriginalStatsError, "Duplicate original car ID 18"):
                MOD.load_original_cars(folder)

    def test_empty_catalog_is_reported_and_aliases_are_case_insensitive(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            archive(folder / "Data/Data_hd.zip", {"Cars/Tweak/Dlc.txt": tweak(208)})
            with self.assertRaisesRegex(MOD.OriginalStatsError, "No original retail car tweaks"):
                MOD.load_original_cars(folder)
            archive(folder / "Data/Data_hd.zip", {"cars/tweak/LOTUS.TXT": tweak()})
            car, = MOD.load_original_cars(folder)
            self.assertEqual(car["alias"], "LOTUS")
            self.assertEqual(car["display_name"], "Lotus Esprit V8")

    def test_original_id_list_stays_aligned_with_installer(self):
        prepare_spec = importlib.util.spec_from_file_location("car_prepare_reference_test", ROOT / "tools/car-mods/prepare.py")
        prepare = importlib.util.module_from_spec(prepare_spec)
        prepare_spec.loader.exec_module(prepare)
        self.assertEqual(MOD.ORIGINAL_CAR_IDS, prepare.ORIGINAL_CAR_IDS)

    def test_original_aliases_align_with_ids_and_names(self):
        self.assertEqual(tuple(MOD.ORIGINAL_ALIASES), MOD.ORIGINAL_CAR_IDS)
        aliases = list(MOD.ORIGINAL_ALIASES.values())
        self.assertEqual(len({alias.casefold() for alias in aliases}), len(aliases))
        self.assertEqual({alias.casefold() for alias in aliases}, set(MOD.ORIGINAL_NAMES))
        for alias in aliases:
            with self.subTest(alias=alias):
                self.assertRegex(alias, r"^[A-Za-z][A-Za-z0-9_]{1,31}$")

    def test_public_decoder_matches_reader_for_bom_and_legacy_text(self):
        self.assertEqual(MOD.decode_text(b"\xff\xfeI\x00D\x00"), "ID")
        self.assertEqual(MOD.decode_text("ID\t\u00e9".encode("cp1252")), "ID\t\u00e9")
        self.assertEqual(MOD.decode_text(b"\xef\xbb\xbfID"), "\ufeffID")

    def test_dlc_loads_only_installed_official_ids_and_uses_localized_names(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            pack = dlc_pack(folder)
            # Mixed-case Xbox member paths are legal on every supported host.
            loose_dlc(pack, 208, folder="cars/tWEAK")
            loose_dlc(pack, 201, folder="cars/tWEAK")
            loose_dlc(pack, 18, folder="cars/tWEAK")
            loose_dlc(pack, 9004, folder="cars/tWEAK")
            names = "201\r\n\r\nCampione\r\nCampione\r\n208\r\n\r\nPython\r\nPython\r\n"
            (pack / "cars/tWEAK/language.dat").write_bytes(names.encode("utf-16"))
            inactive = dlc_pack(folder, "downloaded but not installed", installed=False)
            loose_dlc(inactive, 213)
            cars = MOD.load_dlc_cars(folder)
            self.assertEqual([(car["id"], car["alias"], car["display_name"]) for car in cars],
                             [(201, "car201", "Campione"), (208, "car208", "Python")])
            python = cars[1]
            self.assertEqual(python["physics"]["TorqueScale"], 700)
            self.assertEqual(python["raw_text"], tweak(208))
            self.assertIn("car208.txt", python["source"])
            self.assertEqual(len([entry for entry in python["sections"][0]["entries"] if entry[0] == "COLOR"]), 2)

    def test_dlc_returns_empty_without_installed_packs_and_does_not_scan_retail_zip(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            self.assertEqual(MOD.load_dlc_cars(folder), [])
            archive(folder / "Data/Data_hd.zip", {"Cars/Tweak/car208.txt": tweak(208)})
            self.assertEqual(MOD.load_dlc_cars(folder), [])
            dlc_pack(folder)
            self.assertEqual(MOD.load_dlc_cars(folder), [])

    def test_dlc_archive_support_and_loose_member_precedence(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            pack = dlc_pack(folder)
            with zipfile.ZipFile(pack / "content.zip", "w") as target:
                target.writestr("Cars/Tweak/car208.txt", tweak(208, 4000))
                target.writestr("Cars/Tweak/car213.txt", tweak(213, 900))
                target.writestr("Cars/Tweak/language.dat", "208\nArchived name\n213\nFormula Midtown\n".encode("utf-16"))
                target.writestr("NotInstalled/Cars/Tweak/car201.txt", tweak(201))
            loose_dlc(pack, 208, torque=350)
            (pack / "Cars/Tweak/language.dat").write_bytes("208\nPython\n213\nFormula Midtown\n".encode("utf-16"))
            cars = MOD.load_dlc_cars(folder)
            self.assertEqual([car["id"] for car in cars], [208, 213])
            self.assertEqual(cars[0]["physics"]["TorqueScale"], 350)
            self.assertEqual(cars[0]["display_name"], "Python")
            self.assertNotIn(" :: ", cars[0]["source"])
            self.assertEqual(cars[1]["display_name"], "Formula Midtown")
            self.assertIn("content.zip :: Cars/Tweak/car213.txt", cars[1]["source"])

    def test_dlc_duplicate_copies_are_deduplicated_but_conflicting_stats_are_reported(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            first, second = dlc_pack(folder), dlc_pack(folder, "4d53002a00000002")
            loose_dlc(first, 208)
            loose_dlc(second, 208)
            cars = MOD.load_dlc_cars(folder)
            self.assertEqual(len(cars), 1)
            self.assertEqual(cars[0]["display_name"], "DLC car 208")
            loose_dlc(second, 208, torque=999)
            with self.assertRaisesRegex(MOD.OriginalStatsError, "Conflicting installed DLC car ID 208"):
                MOD.load_dlc_cars(folder)

    def test_dlc_reading_preserves_all_files_and_does_not_create_backups(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            pack = dlc_pack(folder)
            loose_dlc(pack, 208)
            before = {str(path.relative_to(folder)): (path.read_bytes(), path.stat().st_mtime_ns)
                      for path in folder.rglob("*") if path.is_file()}
            MOD.load_dlc_cars(folder)
            after = {str(path.relative_to(folder)): (path.read_bytes(), path.stat().st_mtime_ns)
                     for path in folder.rglob("*") if path.is_file()}
            self.assertEqual(before, after)
            self.assertFalse((folder / ".mm3-car-mods").exists())

    def test_dlc_bad_archives_and_malformed_tweaks_have_ui_safe_errors(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            pack = dlc_pack(folder)
            path = loose_dlc(pack, 208)
            path.write_text("Begin(CarInfo)\nID 208\n", encoding="ascii")
            with self.assertRaisesRegex(MOD.OriginalStatsError, r"Could not read installed DLC.*car208.txt.*Missing End"):
                MOD.load_dlc_cars(folder)
            path.write_text(tweak(208), encoding="ascii")
            (pack / "content.zip").write_bytes(b"broken archive")
            with self.assertRaisesRegex(MOD.OriginalStatsError, r"Could not read installed DLC.*content.zip"):
                MOD.load_dlc_cars(folder)


if __name__ == "__main__":
    unittest.main()
