"""Offline tests of writable catalog assets and reversible archive installation."""

import copy
from contextlib import redirect_stdout
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import re
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("car_mods_prepare", ROOT / "tools/car-mods/prepare.py")
MOD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MOD)


def zip_bytes(entries):
    result = io.BytesIO()
    with zipfile.ZipFile(result, "w", compression=zipfile.ZIP_STORED) as archive:
        for name, data in entries.items():
            archive.writestr(name, data)
    # Match the retail archives: locals have no extra field, while each central
    # entry carries a 36-byte NTFS field. Standard zipfile.writestr puts it in
    # both locations, so attach it to only the central directory here.
    original = result.getvalue()
    with zipfile.ZipFile(io.BytesIO(original)) as archive:
        start = archive.start_dir
        count = len(archive.infolist())
    ntfs = bytes.fromhex("0a0020000000000001001800") + bytes(24)
    central, pos = bytearray(), start
    for _ in range(count):
        name, extra, comment = struct.unpack_from("<HHH", original, pos + 28)
        end = pos + 46 + name + extra + comment
        record = bytearray(original[pos:pos + 46 + name + extra])
        struct.pack_into("<H", record, 30, extra + len(ntfs))
        central.extend(record + ntfs + original[pos + 46 + name + extra:end])
        pos = end
    eocd = struct.pack("<4s4H2IH", b"PK\x05\x06", 0, 0, count, count, len(central), start, 0)
    return original[:start] + bytes(central) + eocd


# Retail-format driving values, raw as stored in Cars/Tweak/*.txt. Quirks are
# deliberate: a trailing tab after the gear vector, a single space before
# GearStatusDownGearFactor and a space-indented LatFrictionFactor.
RETAIL_DRIVING = {
    "NGears": "6", "GearRatioVector": "-3.3 2.53 1.48 0.99 0.79 0.66 0.61\t",
    "WheelDrive": "1", "Mass": "1526", "TorqueScale": "1000", "WheelRatio": "4.0",
    "WheelRadius": "0.34", "TurboSize": "1.0", "TurboSpinup": "1.0", "AirKZY": "1.5",
    "AirKZZ": "0.01", "MaxTorqueRPM": "6500.0", "MaxTurboTorqueRPM": "6750.0",
    "MinRPMGearChange": "4000.0", "BrakeTorque": "18000.0", "MinRPM": "1000.0",
    "MaxRPM": "10000.0", "CutRPM": "8000.0", "TimeChangeGear": "0.6",
    "TimeChangeGearFR": "0.15", "GearStatusDownGearFactor": "0.75",
    "HandBrakeTorque": "19500.0", "TorqueScaleFactor": "1.125",
    "LatFrictionFactor": "2.0", "SteeringEndSpeed": "340.0",
}
# CarBody values that belong to the body and never come from a base car.
BODY_ONLY = {"NWheels": "4", "NColPoints": "12", "vInertia": "3.25 2.45 1.5", "BrakeBalance": "1.0"}


def retail_tweak(ident, alias, driving=None, omit=()):
    """A complete retail-style tweak with every driving key and its quirks."""
    values = {**BODY_ONLY, **RETAIL_DRIVING, **(driving or {})}
    lines = ["Begin(CarInfo)", f"\tID\t{ident}", "\tCOLOR\t0 0x443322 0x443322", "\tLOCKED",
             *[f"\t{key}\t1" for key in sorted(MOD.RATINGS)], f"\tPAINTJOBNAME\t{alias}",
             "End(CarInfo)", "Begin(CarBody)"]
    for key, value in values.items():
        if key in omit:
            continue
        if key == "GearStatusDownGearFactor":
            lines.append(f"\t{key} {value}")
        elif key == "LatFrictionFactor":
            lines.append(f"    {key}\t{value}")
        else:
            lines.append(f"\t{key}\t{value}")
    lines += ["End(CarBody)", "Begin(CarWheel0)", "\tWheelBasePos\t-0.765 0.23 1.39",
              "\tSpringK\t70000.0", "End(CarWheel0)", "Begin(CarCamera)", "\tHoodPos\t0 1 2",
              "\tSpeedometerMaxKmh\t620", "\tSpeedometerMaxMph\t500", "End(CarCamera)",
              "Begin(CarCameraSS)", "\tOutFarStartZ\t6", "End(CarCameraSS)"]
    return ("\r\n".join(lines) + "\r\n").encode("ascii")


def retail_hud_tweak(ident, alias, gauge_type, driving=None):
    """Retail-style tweak with an explicit native HUD artwork selector."""
    return retail_tweak(ident, alias, driving).replace(
        b"End(CarInfo)", f"\tSPEEDOMETERTYPE\t{gauge_type}\r\nEnd(CarInfo)".encode("ascii"), 1)


def carbody(tweak):
    """Ordered CarBody (key, raw value) pairs of tweak bytes or text."""
    text = tweak.decode("ascii") if isinstance(tweak, bytes) else tweak
    body = re.search(r"Begin\(CarBody\)\r?\n(.*?)End\(CarBody\)", text, re.S).group(1)
    return [tuple(line.strip().split(None, 1)) if len(line.split()) > 1 else (line.strip(), "")
            for line in body.splitlines() if line.strip()]


# Recorded from tweak_variant before driving_base existed. Cars without a base
# stats car must keep producing exactly these bytes.
NO_BASE_TWEAK_DIGEST = "b13843f59a3c924e27338d45d9ec6ee0975db8ac79eac8f892e7ed78405ae105"


def material(names, donor, lod=False):
    data = b"\x00" + struct.pack("<I", len(names))
    for name in sorted(names):
        data += name.encode() + b"\x00+Cars\\" + donor.encode()
        data += (b"_lod1" if lod else b"") + b"\\texture.dds\x00"
        data += b"\xcc\x22\x33\x44\xff" + struct.pack("<I", 3)
    return data


class CarModsTest(unittest.TestCase):
    def test_default_manifest_uses_local_file_and_falls_back_to_tracked_defaults(self):
        with tempfile.TemporaryDirectory(prefix="mm3-manifest-default-") as temporary:
            folder = Path(temporary)
            tracked = folder / "cars.json"
            local = folder / "cars.local.json"
            tracked.write_bytes((ROOT / "tools/car-mods/cars.json").read_bytes())
            self.assertEqual(MOD.default_config_path(folder), tracked)
            baseline = MOD.load_config(tracked)
            custom = copy.deepcopy(baseline)
            custom["cars"][0]["physics"]["TorqueScale"] = 777
            custom["cars"][0]["model_package"] = "models/Panoz"
            local.write_text(json.dumps(custom), encoding="utf-8")
            self.assertEqual(MOD.default_config_path(folder), local)
            self.assertEqual(MOD.load_config(MOD.default_config_path(folder)), custom)
            self.assertEqual(MOD.load_config(tracked), baseline)
            local.unlink()
            self.assertEqual(MOD.default_config_path(folder), tracked)
            local.mkdir()
            self.assertEqual(MOD.default_config_path(folder), tracked)

    def test_cli_explicit_manifest_wins_over_existing_local_settings(self):
        with tempfile.TemporaryDirectory(prefix="mm3-manifest-cli-") as temporary:
            folder = Path(temporary)
            tracked, local = folder / "cars.json", folder / "cars.local.json"
            for path in (tracked, local):
                path.write_bytes((ROOT / "tools/car-mods/cars.json").read_bytes())
            selector = MOD.default_config_path
            with patch.object(MOD, "default_config_path", lambda: selector(folder)):
                self.assertEqual(MOD.parse_args([]).config, local)
                args = MOD.parse_args(["--config", str(tracked)])
                self.assertEqual(args.config, tracked)
                self.assertFalse(args.apply)
                self.assertFalse(args.restore)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.game = Path(self.temp.name)
        self.config = json.loads((ROOT / "tools/car-mods/cars.json").read_text())
        # Keep donor fixtures independent of locally selected real model packs;
        # package tests below construct their own isolated prepared sources.
        for car in self.config["cars"]:
            car.pop("model_package", None)
        hd, dvd = {"Cars/Tweak/SortOrder": b"12\r\n8\r\n11\r\n", "unrelated.bin": bytes(range(256))}, {"unrelated.texture": b"preserve me"}
        donor_ids = {"mini": 12, "MFastBack": 8, "Viper": 11, "Koenigsegg": 15, "Lotus": 18}
        for donor, stock_id in donor_ids.items():
            text = "Begin(CarInfo)\r\n\tID\t" + str(stock_id) + "\r\n"
            text += "\tCOLOR\t0 0x443322 0x443322\r\n\tSKIN\t1 0xff0000 0xff0000\r\n"
            text += "\tLOCKED\r\n\tHIDDEN\r\n\tADMITTANCE\t0 0 2\r\n"
            text += "".join(f"\t{k}\t1\r\n" for k in MOD.RATINGS)
            text += f"\tVEHICLECLASS\t1\r\n\tSPEEDOMETERTYPE\t{stock_id % 3}\r\nEnd(CarInfo)\r\nBegin(CarBody)\r\n"
            text += "\tMass\t1500\r\n\tTorqueScale\t300\r\n\tNWheels\t4\r\nEnd(CarBody)\r\n"
            text += "Begin(CarWheel0)\r\n\tMass\t9\r\nEnd(CarWheel0)\r\n"
            text += "Begin(CarCamera)\r\n\tHoodPos\t0 1 2\r\n\tSpeedometerMaxKmh\t620\r\n\tSpeedometerMaxMph\t500\r\nEnd(CarCamera)\r\n"
            text += "Begin(CarCameraSS)\r\n\tOutFarStartZ\t6\r\nEnd(CarCameraSS)\r\n"
            hd[f"Cars/Tweak/{donor}.txt"] = text.encode()
            for suffix in ("", "_lod1"):
                base = f"Cars/{donor}{suffix}"
                names = MOD.PAINT_MATERIALS[donor.lower()][int(bool(suffix))]
                hd[f"{base}/materialSet00.omb"] = material(names, donor, bool(suffix))
                hd[f"{base}/data.ccol"] = b"collision unchanged"
                dvd[f"{base}/texture.cdds"] = b"texture unchanged"
                loose = self.game / f"Data/Shared/{base}"
                loose.mkdir(parents=True)
                (loose / "body.cmp").write_bytes(b"binary\x00+Cars\\" + (donor + suffix).encode() + b"\\materialSet00.oms\x00")
            hd[f"Cars/{donor}/Camera.txt"] = b"fov=40"
            for scope in ("Cars", "RaceAi"):
                hd[f"SoundScripts/{scope}/{donor}/Vehicle.mcs"] = b"native engine script"
            for suffix in ("", "_Small"):
                dvd[f"Menu/Gfx/Vehicles/{donor}{suffix}.cdds"] = b"menu image"
            dvd[f"LoadingScreens/Default_{donor}.tga"] = b"donor loading image " + donor.encode()
        self.originals = dict(zip(MOD.ARCHIVES, (zip_bytes(hd), zip_bytes(dvd))))
        for name, data in self.originals.items():
            target = self.game / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        save = self.game / "UDATA/profile"
        save.parent.mkdir()
        save.write_bytes(b"do not change player save")

    def tearDown(self):
        self.temp.cleanup()

    def plan(self, config=None):
        return MOD.build_plan(self.game, self.originals, config or self.config)[0]

    def with_retail_bases(self, tweaks, install=True):
        """Originals whose HD archive also holds complete retail base tweaks."""
        with zipfile.ZipFile(io.BytesIO(self.originals[MOD.ARCHIVES[0]])) as archive:
            files = {name: archive.read(name) for name in archive.namelist()}
        files.update({f"Cars/Tweak/{alias}.txt": data for alias, data in tweaks.items()})
        originals = {**self.originals, MOD.ARCHIVES[0]: zip_bytes(files)}
        if install:
            for name, data in originals.items():
                (self.game / name).write_bytes(data)
        return originals

    def model_package(self):
        folder = self.game / "prepared models" / "panoz"
        folder.mkdir(parents=True)
        assets = [
            ("loose", "Data/Shared/Cars/Panoz/body.cmp", "body.cmp", b"custom panoz body"),
            ("loose", "Data/Shared/Cars/Panoz_lod1/body.cmp", "lod.cmp", b"custom panoz lod"),
            ("Data_hd.zip", "Cars/Panoz/materialSet00.omb", "paint.omb", b"custom panoz material"),
            ("Data_dvd.zip", "Cars/Panoz/panoz.cdds", "texture.cdds", b"custom panoz texture"),
        ]
        package = {"format": "mm3-car-model-v1", "alias": "Panoz", "car_id": 9004,
                   "files": [{"archive": archive, "path": path, "source": source}
                             for archive, path, source, _ in assets]}
        for _, _, source, data in assets:
            (folder / source).write_bytes(data)
        (folder / "package.json").write_text(json.dumps(package), encoding="utf-8")
        config = copy.deepcopy(self.config)
        config["cars"][0]["model_package"] = "prepared models/panoz"
        return folder, package, config, assets

    def test_prepared_model_survives_stats_reapply_and_restore_is_exact(self):
        folder, package, config, assets = self.model_package()
        manifest = self.game / "cars.json"
        manifest.write_text(json.dumps(config), encoding="utf-8")
        self.assertEqual(MOD.load_config(manifest), config)
        output, _ = MOD.build_plan(self.game, self.originals, config, manifest.parent)
        with zipfile.ZipFile(io.BytesIO(output[MOD.ARCHIVES[0]])) as hd:
            self.assertEqual(hd.read("Cars/Panoz/materialSet00.omb"), b"custom panoz material")
            self.assertEqual(hd.read("Cars/Panoz/data.ccol"), b"collision unchanged")
            self.assertEqual(hd.read("SoundScripts/Cars/Panoz/Vehicle.mcs"), b"native engine script")
        MOD.apply_plan(self.game, None, self.originals, output, config)
        state, originals = MOD.read_install(self.game)
        config["cars"][0]["physics"]["TorqueScale"] += 1
        changed, _ = MOD.build_plan(self.game, originals, config, manifest.parent)
        MOD.apply_plan(self.game, state, originals, changed, config)
        for archive, path, _, data in assets:
            if archive == "loose":
                self.assertEqual((self.game / path).read_bytes(), data)
            else:
                with zipfile.ZipFile(self.game / "Data" / archive) as installed:
                    self.assertEqual(installed.read(path), data)
        with zipfile.ZipFile(self.game / MOD.ARCHIVES[0]) as hd:
            self.assertIn(f"TorqueScale\t\t{config['cars'][0]['physics']['TorqueScale']}\r\n".encode(), hd.read("Cars/Tweak/Panoz.txt"))
        state, originals = MOD.read_install(self.game)
        MOD.restore(self.game, state, originals)
        for relative, original in self.originals.items():
            self.assertEqual((self.game / relative).read_bytes(), original)
        self.assertFalse((self.game / assets[0][1]).exists())
        self.assertEqual((self.game / "UDATA/profile").read_bytes(), b"do not change player save")
        self.assertTrue((folder / "body.cmp").is_file())

    def test_clear_package_restores_donor_visuals_and_removes_package_only_assets(self):
        folder, package, config, assets = self.model_package()
        new_source = folder / "extra.cmp"
        new_source.write_bytes(b"extra model part")
        extra = {"archive": "loose", "path": "Data/Shared/Cars/Panoz/extra.cmp", "source": "extra.cmp"}
        package["files"].append(extra)
        (folder / "package.json").write_text(json.dumps(package))
        output, _ = MOD.build_plan(self.game, self.originals, config, self.game)
        MOD.apply_plan(self.game, None, self.originals, output, config)
        state, originals = MOD.read_install(self.game)
        config["cars"][0]["model_package"] = ""
        fallback, _ = MOD.build_plan(self.game, originals, config, self.game)
        MOD.apply_plan(self.game, state, originals, fallback, config)
        self.assertEqual((self.game / "Data/Shared/Cars/Panoz/body.cmp").read_bytes(), fallback["Data/Shared/Cars/Panoz/body.cmp"])
        self.assertIn(b"Cars\\Panoz\\materialSet00.oms", fallback["Data/Shared/Cars/Panoz/body.cmp"])
        self.assertFalse((self.game / extra["path"]).exists())
        MOD.read_install(self.game)

    def test_package_reapply_interruption_preserves_visuals_and_installed_state(self):
        folder, package, config, assets = self.model_package()
        output, _ = MOD.build_plan(self.game, self.originals, config, self.game)
        MOD.apply_plan(self.game, None, self.originals, output, config)
        state, originals = MOD.read_install(self.game)
        previous_state = (self.game / MOD.STATE_DIR / "state.json").read_bytes()
        (folder / "body.cmp").write_bytes(b"updated imported geometry")
        changed, _ = MOD.build_plan(self.game, originals, config, self.game)
        write = MOD.atomic_write
        interrupted = False

        def cancel(path, data):
            nonlocal interrupted
            if not interrupted and path == self.game / MOD.STATE_DIR / "state.json":
                interrupted = True
                raise KeyboardInterrupt("cancel after prepared visuals")
            return write(path, data)

        with patch.object(MOD, "atomic_write", side_effect=cancel), self.assertRaises(KeyboardInterrupt):
            MOD.apply_plan(self.game, state, originals, changed, config)
        self.assertTrue(interrupted)
        for relative, previous in output.items():
            self.assertEqual((self.game / relative).read_bytes(), previous)
        self.assertEqual((self.game / MOD.STATE_DIR / "state.json").read_bytes(), previous_state)
        self.assertEqual(MOD.read_install(self.game), (state, originals))

    def test_model_package_paths_identity_and_format_reject_before_game_writes(self):
        folder, package, config, _ = self.model_package()
        invalids = [
            ("alias", "Viper"), ("car_id", 9005), ("car_id", True), ("format", "mm2-pkg"),
            ("files", []), ("files", [{"archive": "Data_hd.zip", "path": "Cars/Tweak/Panoz.txt", "source": "body.cmp"}]),
            ("files", [{"archive": "Data_dvd.zip", "path": "Cars/Viper/body.cdds", "source": "body.cmp"}]),
            ("files", [{"archive": "loose", "path": "Data/Shared/Cars/Panoz/../Viper/body.cmp", "source": "body.cmp"}]),
            ("files", [{"archive": "loose", "path": "Data/Shared/Cars/Panoz/data.ccol", "source": "body.cmp"}]),
            ("files", [{"archive": "loose", "path": "Data/Shared/Cars/Panoz/body.cmp", "source": "../outside.cmp"}]),
            ("files", [{"archive": "loose", "path": "Data/Shared/Cars/Panoz/body.cmp", "source": "/body.cmp"}]),
            ("files", [{"archive": "loose", "path": "Data/Shared/Cars/Panoz/body.cmp", "source": "nested\\body.cmp"}]),
            ("files", [{"archive": "Data_hd.zip", "path": "Cars/Panoz/body.cmp", "source": "body.cmp"}]),
            ("files", [package["files"][0], package["files"][0]]),
        ]
        for key, value in invalids:
            rejected = copy.deepcopy(package)
            rejected[key] = value
            (folder / "package.json").write_text(json.dumps(rejected))
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                MOD.build_plan(self.game, self.originals, config, self.game)
        for relative, original in self.originals.items():
            self.assertEqual((self.game / relative).read_bytes(), original)
        self.assertFalse((self.game / MOD.STATE_DIR / "state.json").exists())

    def test_model_package_links_are_rejected(self):
        folder, package, config, _ = self.model_package()
        source = folder / "body.cmp"
        source.unlink()
        target = folder / "original.cmp"
        target.write_bytes(b"model")
        try:
            source.symlink_to(target)
        except OSError:
            self.skipTest("Creating filesystem symbolic links is unavailable")
        with self.assertRaisesRegex(ValueError, "links"):
            MOD.build_plan(self.game, self.originals, config, self.game)

    def test_package_can_replace_own_menu_icons_and_loading_screen_only(self):
        folder, package, config, _ = self.model_package()
        for target in ("Menu/Gfx/Vehicles/Panoz.cdds", "Menu/Gfx/Vehicles/Panoz_Small.cdds",
                       "LoadingScreens/Default_Panoz.tga"):
            package["files"].append({"archive": "Data_dvd.zip", "path": target, "source": "texture.cdds"})
        (folder / "package.json").write_text(json.dumps(package))
        output, _ = MOD.build_plan(self.game, self.originals, config, self.game)
        with zipfile.ZipFile(io.BytesIO(output[MOD.ARCHIVES[1]])) as dvd:
            for target in ("Menu/Gfx/Vehicles/Panoz.cdds", "Menu/Gfx/Vehicles/Panoz_Small.cdds",
                           "LoadingScreens/Default_Panoz.tga"):
                self.assertEqual(dvd.read(target), b"custom panoz texture")
            self.assertEqual(dvd.read("Menu/Gfx/Vehicles/Viper.cdds"), b"menu image")
            self.assertEqual(dvd.read("LoadingScreens/Default_Viper.tga"), b"donor loading image Viper")
        for target in ("Menu/Gfx/Vehicles/Viper.cdds", "Menu/Gfx/Vehicles/Panoz_Extra.cdds",
                       "Menu/Gfx/HUD/Panoz.cdds", "LoadingScreens/Default_Viper.tga",
                       "LoadingScreens/Default_Panoz_Extra.tga", "LoadingScreens/Panoz.tga"):
            rejected = copy.deepcopy(package)
            rejected["files"][-1]["path"] = target
            (folder / "package.json").write_text(json.dumps(rejected))
            with self.subTest(target=target), self.assertRaisesRegex(ValueError, "destination"):
                MOD.build_plan(self.game, self.originals, config, self.game)
        rejected = copy.deepcopy(package)
        rejected["files"][-1] = {"archive": "loose", "path": "Menu/Gfx/Vehicles/Panoz.cdds", "source": "texture.cdds"}
        (folder / "package.json").write_text(json.dumps(rejected))
        with self.assertRaisesRegex(ValueError, "destination"):
            MOD.build_plan(self.game, self.originals, config, self.game)

    def test_package_selection_background_is_owned_reapplied_and_restored(self):
        folder, package, config, _ = self.model_package()
        target = "Data/Shared/Cars/Panoz/bg.cdds"
        background = {"archive": "loose", "path": target, "source": "texture.cdds"}
        package["files"].append(background)
        (folder / "package.json").write_text(json.dumps(package))
        # A background cloned from the donor must be replaced by the package.
        (self.game / "Data/Shared/Cars/Viper/bg.cdds").write_bytes(b"donor backdrop")
        output, _ = MOD.build_plan(self.game, self.originals, config, self.game)
        self.assertEqual(output[target], b"custom panoz texture")
        MOD.apply_plan(self.game, None, self.originals, output, config)
        state, originals = MOD.read_install(self.game)
        config["cars"][0]["physics"]["TorqueScale"] += 1
        reapplied, _ = MOD.build_plan(self.game, originals, config, self.game)
        MOD.apply_plan(self.game, state, originals, reapplied, config)
        self.assertEqual((self.game / target).read_bytes(), b"custom panoz texture")
        state, originals = MOD.read_install(self.game)
        MOD.restore(self.game, state, originals)
        self.assertFalse((self.game / target).exists())
        self.assertEqual((self.game / "Data/Shared/Cars/Viper/bg.cdds").read_bytes(), b"donor backdrop")
        for forbidden in ("Data/Shared/Cars/Viper/bg.cdds",
                          "Data/Shared/Cars/Panoz/texture.cdds",
                          "Data/Shared/Cars/Panoz_lod1/bg.cdds"):
            background["path"] = forbidden
            (folder / "package.json").write_text(json.dumps(package))
            with self.subTest(target=forbidden), self.assertRaises(ValueError):
                MOD.load_model_package(config["cars"][0], self.game)

    def test_optional_model_package_accepts_old_manifest_and_absolute_folder(self):
        folder, package, config, _ = self.model_package()
        config["cars"][0]["model_package"] = str(folder)
        output, _ = MOD.build_plan(self.game, self.originals, config, self.game / "unrelated")
        self.assertEqual(output["Data/Shared/Cars/Panoz/body.cmp"], b"custom panoz body")
        path = self.game / "model-path.json"
        for value in (None, {}, 42, "bad\x00path"):
            config["cars"][0]["model_package"] = value
            path.write_text(json.dumps(config))
            with self.subTest(value=value), self.assertRaises(ValueError):
                MOD.load_config(path)

    def test_archive_prefix_offsets_payload_and_catalog(self):
        output = self.plan()
        for name, original in self.originals.items():
            self.assertTrue(output[name].startswith(original))
            with zipfile.ZipFile(io.BytesIO(original)) as before, zipfile.ZipFile(io.BytesIO(output[name])) as after:
                self.assertIsNone(after.testzip())
                self.assertEqual(len(after.namelist()), len(set(after.namelist())))
                for old in before.infolist():
                    if old.filename == "Cars/Tweak/SortOrder":
                        continue
                    new = after.getinfo(old.filename)
                    self.assertEqual(new.header_offset, old.header_offset)
                    self.assertEqual(after.read(new), before.read(old))
                for info in after.infolist():
                    self.assertEqual(info.compress_type, zipfile.ZIP_STORED)
                    name_length, extra_length = struct.unpack_from("<HH", output[name], info.header_offset + 26)
                    self.assertEqual(extra_length, 0)
                    payload = info.header_offset + 30 + name_length
                    # This is how the game's reader at 0x001BCD7E
                    # obtains resources; test real-reader bytes, not just ZIP CRC.
                    self.assertEqual(output[name][payload:payload + info.file_size], after.read(info))
                self.assertTrue(all(len(i.extra) == 36 for i in before.infolist()))
        with zipfile.ZipFile(io.BytesIO(output[MOD.ARCHIVES[0]])) as hd:
            expected = [12, 8, 11, 15, 18]
            expected += [car["id"] for car in sorted(self.config["cars"],
                key=lambda car: (car["ratings"]["TOPSPEED"], car["id"]))]
            self.assertEqual([int(line) for line in hd.read("Cars/Tweak/SortOrder").splitlines()], expected)
            text = hd.read("Cars/Tweak/Panoz.txt").decode()
            self.assertIn("ID\t\t9004", text)
            panoz = next(car for car in self.config["cars"] if car["alias"] == "Panoz")
            self.assertIn(f"Mass\t\t{panoz['physics']['Mass']}", text)
            self.assertIn(f"HANDLING\t\t{panoz['ratings']['HANDLING']}", text)
            self.assertIn("\tMass\t9\r\nEnd(CarWheel0)", text)
            self.assertNotIn("LOCKED", text)
            self.assertNotIn("SKIN", text)
            paint = hd.read("Cars/Panoz/materialSet00.omb")
            self.assertIn(b"Cars\\Panoz\\texture.dds", paint)
            self.assertIn(bytes.fromhex("2ba5c7"), paint)
            self.assertIn(b"Cars\\Panoz_lod1\\texture.dds", hd.read("Cars/Panoz_lod1/materialSet00.omb"))
        mesh = output["Data/Shared/Cars/Panoz/body.cmp"]
        self.assertIn(b"Cars\\Panoz\\materialSet00.oms", mesh)
        self.assertEqual(len(mesh), len((self.game / "Data/Shared/Cars/Viper/body.cmp").read_bytes()))
        with zipfile.ZipFile(io.BytesIO(output[MOD.ARCHIVES[1]])) as dvd:
            for car in self.config["cars"]:
                self.assertEqual(dvd.read(f"LoadingScreens/Default_{car['alias']}.tga"), dvd.read(f"LoadingScreens/Default_{car['donor']}.tga"))

    def test_requested_cars_are_independent_variants_of_shared_donors(self):
        expected = ["Panoz GTR-1", "McLaren F1", "Formula 1", "Ford GT"]
        self.assertEqual([car["display_name"] for car in self.config["cars"][:4]], expected)
        ids = set(car["id"] for car in self.config["cars"])
        self.assertEqual(ids, {9004, 9005, 9006, 9009, 9010, 9011, 9012, 9013, 9014})
        self.assertEqual(ids, set(MOD.IDENTITIES))
        output = self.plan()
        with zipfile.ZipFile(io.BytesIO(output[MOD.ARCHIVES[0]])) as hd:
            materials = set()
            for car in self.config["cars"]:
                alias = car["alias"]
                tweak = hd.read(f"Cars/Tweak/{alias}.txt").decode()
                self.assertIn(f"ID\t\t{car['id']}\r\n", tweak)
                self.assertIn(f"TorqueScale\t\t{car['physics']['TorqueScale']}\r\n", tweak)
                self.assertNotIn("LOCKED", tweak)
                for suffix in ("", "_lod1"):
                    material_name = f"Cars/{alias}{suffix}/materialSet00.omb"
                    self.assertNotIn(material_name.lower(), materials)
                    materials.add(material_name.lower())
                    self.assertIn(bytes.fromhex(car["colors"][0][1:])[::-1], hd.read(material_name))
                    mesh = output[f"Data/Shared/Cars/{alias}{suffix}/body.cmp"]
                    self.assertIn(f"Cars\\{alias}{suffix}\\materialSet00.oms".encode(), mesh)
                    self.assertEqual(len(mesh), len((self.game / f"Data/Shared/Cars/{car['donor']}{suffix}/body.cmp").read_bytes()))
            self.assertNotEqual(hd.read("Cars/McLaren_F1/materialSet00.omb"), hd.read("Cars/Ford_GTcar/materialSet00.omb"))

    def test_catalog_order_keeps_originals_first_and_sorts_extras_by_speed(self):
        # Originals retain retail order regardless of their ratings. DLC from
        # the archive and added cars share the final speed-sorted group.
        ratings = {"mini": (9, 0, 0, 0), "MFastBack": (0, 9, 0, 0),
                   "Viper": (0, 0, 9, 0), "Koenigsegg": (0, 0, 0, 9),
                   "Lotus": (3, 3, 3, 3)}
        with zipfile.ZipFile(io.BytesIO(self.originals[MOD.ARCHIVES[0]])) as archive:
            files = {name: archive.read(name) for name in archive.namelist()}
        for donor, values in ratings.items():
            path = f"Cars/Tweak/{donor}.txt"
            files[path] = MOD.update_section(files[path].decode(), "CarInfo",
                dict(zip(MOD.CATALOG_RATINGS, values))).encode()
        files["Cars/Tweak/car208.txt"] = MOD.update_section(
            files["Cars/Tweak/mini.txt"].decode(), "CarInfo",
            {"ID": 208, "TOPSPEED": 6}).encode()
        originals = {**self.originals, MOD.ARCHIVES[0]: zip_bytes(files)}
        config = copy.deepcopy(self.config)
        for i, car in enumerate(config["cars"]):
            car["ratings"]["TOPSPEED"] = (9, 3, 6)[i % 3]
        config["cars"].reverse()  # ID breaks ties independently of manifest order.
        before = copy.deepcopy(config)
        with zipfile.ZipFile(io.BytesIO(originals[MOD.ARCHIVES[0]])) as archive:
            ranked = MOD.catalog_ranking(archive, config)
            self.assertEqual([car["id"] for car in ranked[:5]], [12, 8, 11, 15, 18])
            expected_extras = sorted([*config["cars"], {"id": 208, "ratings": {"TOPSPEED": 6}}],
                                     key=lambda car: (car["ratings"]["TOPSPEED"], car["id"]))
            self.assertEqual([car["id"] for car in ranked[5:]], [car["id"] for car in expected_extras])
            self.assertEqual(config, before)  # Planning must not mutate settings.
            for car in config["cars"]:
                for key in MOD.RATINGS - {"TOPSPEED"}:
                    car["ratings"][key] = 9 - car["ratings"][key]
                car["physics"]["TorqueScale"] = 1
                car["speedometer"] = {"max_kmh": 800, "max_mph": 500}
            self.assertEqual([car["id"] for car in MOD.catalog_ranking(archive, config)],
                             [car["id"] for car in ranked])
        output, _ = MOD.build_plan(self.game, originals, before)
        with zipfile.ZipFile(io.BytesIO(output[MOD.ARCHIVES[0]])) as archive:
            self.assertEqual([int(line) for line in archive.read("Cars/Tweak/SortOrder").splitlines()],
                             [car["id"] for car in ranked])
            for donor in ratings:
                path = f"Cars/Tweak/{donor}.txt"
                self.assertEqual(archive.read(path), files[path])
                for rule in (b"LOCKED", b"HIDDEN", b"ADMITTANCE\t0 0 2"):
                    self.assertIn(rule, archive.read(path))
            self.assertEqual(archive.read("Cars/Tweak/car208.txt"), files["Cars/Tweak/car208.txt"])
            for car in before["cars"]:
                text = archive.read(f"Cars/Tweak/{car['alias']}.txt")
                for rule in (b"LOCKED", b"HIDDEN", b"ADMITTANCE"):
                    self.assertNotIn(rule, text)

    def test_extra_speed_edits_reorder_only_the_extra_group(self):
        config = copy.deepcopy(self.config)
        with zipfile.ZipFile(io.BytesIO(self.originals[MOD.ARCHIVES[0]])) as archive:
            original = MOD.catalog_ranking(archive, config)
            selected = config["cars"][-1]
            selected["ratings"]["TOPSPEED"] = 0
            slower = MOD.catalog_ranking(archive, config)
            self.assertEqual([car["id"] for car in slower[:5]], [car["id"] for car in original[:5]])
            self.assertEqual(slower[5]["id"], selected["id"])
            selected["ratings"]["TOPSPEED"] = 9
            faster = MOD.catalog_ranking(archive, config)
            self.assertEqual(faster[-1]["id"], selected["id"])

    def test_cli_dry_run_explains_speed_order_and_keeps_game_files_unchanged(self):
        config_path = self.game / "preview.json"
        config_path.write_text(json.dumps(self.config), encoding="utf-8")
        stdout = io.StringIO()
        with patch.object(sys, "argv", ["prepare.py", "--game-dir", str(self.game),
                                      "--config", str(config_path)]), redirect_stdout(stdout):
            self.assertEqual(MOD.main(), 0)
        text = stdout.getvalue()
        self.assertIn("original cars first; extras slow-to-fast by TOPSPEED (0-9), then ID", text)
        self.assertIn("[12] speed 1/9", text)
        self.assertNotIn("/36", text)
        for relative, original in self.originals.items():
            self.assertEqual((self.game / relative).read_bytes(), original)
        self.assertFalse((self.game / MOD.STATE_DIR / "state.json").exists())

    def test_catalog_ranking_rejects_unknown_order_ids_and_missing_ratings(self):
        with zipfile.ZipFile(io.BytesIO(self.originals[MOD.ARCHIVES[0]])) as archive:
            original = {name: archive.read(name) for name in archive.namelist()}
        for order in (b"12\n12\n", b"12\n999\n"):
            files = {**original, "Cars/Tweak/SortOrder": order}
            with zipfile.ZipFile(io.BytesIO(zip_bytes(files))) as archive:
                with self.assertRaisesRegex(ValueError, "duplicate or unknown"):
                    MOD.catalog_ranking(archive, self.config)
        files = dict(original)
        files["Cars/Tweak/mini.txt"] = files["Cars/Tweak/mini.txt"].replace(b"\tHANDLING\t1\r\n", b"")
        with zipfile.ZipFile(io.BytesIO(zip_bytes(files))) as archive:
            with self.assertRaisesRegex(ValueError, "numeric HANDLING"):
                MOD.catalog_ranking(archive, self.config)

    def test_expanded_catalog_installs_all_nine_without_changing_old_settings(self):
        config = copy.deepcopy(self.config)
        config["cars"] = [car for car in config["cars"] if car["id"] <= 9009]
        original_cars = copy.deepcopy(config["cars"])
        template = next(car for car in config["cars"] if car["id"] == 9005)
        for ident in range(9010, 9015):
            alias, donor, name = MOD.IDENTITIES[ident]
            self.assertEqual(len(alias), len(donor))
            car = copy.deepcopy(template)
            car.update(id=ident, alias=alias, donor=donor, display_name=name)
            car.pop("engine_profile", None)  # This tests legacy cloned tuning, not factory presets.
            config["cars"].append(car)
        path = self.game / "nine-cars.json"
        path.write_text(json.dumps(config))
        self.assertEqual(MOD.load_config(path), config)
        output, counts = MOD.build_plan(self.game, self.originals, config, self.game)
        self.assertEqual(counts["cars"], 9)
        self.assertEqual(config["cars"][:len(original_cars)], original_cars)
        with zipfile.ZipFile(io.BytesIO(output[MOD.ARCHIVES[0]])) as hd:
            order = hd.read("Cars/Tweak/SortOrder").splitlines()
            for car in config["cars"]:
                self.assertEqual(order.count(str(car["id"]).encode()), 1)
                self.assertIn(f"ID\t\t{car['id']}\r\n".encode(), hd.read(f"Cars/Tweak/{car['alias']}.txt"))
                for suffix in ("", "_lod1"):
                    mesh = output[f"Data/Shared/Cars/{car['alias']}{suffix}/body.cmp"]
                    self.assertIn(f"Cars\\{car['alias']}{suffix}\\materialSet00.oms".encode(), mesh)
                    self.assertEqual(len(mesh), len((self.game / f"Data/Shared/Cars/{car['donor']}{suffix}/body.cmp").read_bytes()))
        MOD.apply_plan(self.game, None, self.originals, output, config)
        state, originals = MOD.read_install(self.game)
        self.assertEqual(state["config"]["cars"][:len(original_cars)], original_cars)
        MOD.restore(self.game, state, originals)
        for relative, original in self.originals.items():
            self.assertEqual((self.game / relative).read_bytes(), original)

    def test_speedometer_validation_and_camera_serialization(self):
        path = self.game / "speedometer.json"
        output = self.plan()
        with zipfile.ZipFile(io.BytesIO(output[MOD.ARCHIVES[0]])) as hd:
            tweak = hd.read("Cars/Tweak/Senna_Road.txt").decode()
            gauges = next(car for car in self.config["cars"] if car["alias"] == "Senna_Road")["speedometer"]
            self.assertEqual(tweak.count(f"SpeedometerMaxKmh\t\t{gauges['max_kmh']}\r\n"), 2)
            self.assertEqual(tweak.count(f"SpeedometerMaxMph\t\t{gauges['max_mph']}\r\n"), 2)
            self.assertIn("SPEEDOMETERTYPE\t0\r\n", tweak)
            self.assertIn("HoodPos\t0 1 2", tweak)
            self.assertIn("OutFarStartZ\t6", tweak)
        legacy = copy.deepcopy(self.config)
        del legacy["cars"][0]["speedometer"]
        path.write_text(json.dumps(legacy))
        MOD.load_config(path)
        legacy_output = self.plan(legacy)
        self.assertNotIn(b"added Panoz ", legacy_output[MOD.SPEEDOMETER_FILE])
        with zipfile.ZipFile(io.BytesIO(legacy_output[MOD.ARCHIVES[0]])) as hd:
            self.assertIn(b"SpeedometerMaxKmh\t620\r\n", hd.read("Cars/Tweak/Panoz.txt"))
        for value in [{"max_kmh": 800}, {"max_kmh": 800, "max_mph": 500, "bogus": 1},
                      {"max_kmh": 1201, "max_mph": 500}, {"max_kmh": 800, "max_mph": 0},
                      {"max_kmh": float("nan"), "max_mph": 500}, {"max_kmh": 800, "max_mph": True}, None]:
            config = copy.deepcopy(self.config)
            config["cars"][0]["speedometer"] = value
            path.write_text(json.dumps(config))
            with self.subTest(value=value), self.assertRaises(ValueError):
                MOD.load_config(path)

    def test_speedometer_sidecar_separates_original_overrides_and_tagged_added_ranges(self):
        config = copy.deepcopy(self.config)
        # Original overrides remain explicit workshop edits; defaults are retail.
        config["original_speedometers"] = {"Koenigsegg": {"max_kmh": 800, "max_mph": 500}}
        config["cars"][0]["speedometer"] = {"max_kmh": 650, "max_mph": 404}
        del config["cars"][1]["speedometer"]
        output = self.plan(config)
        settings = output[MOD.SPEEDOMETER_FILE].decode("ascii")
        rows = [line.split() for line in settings.splitlines() if not line.startswith("#")]
        original_rows = [row for row in rows if row[0] != "added"]
        added_rows = [row for row in rows if row[0] == "added"]
        actual = {alias: (float(kmh), float(mph)) for alias, kmh, mph in original_rows}
        expected = {alias: (limits["max_kmh"], limits["max_mph"])
                    for alias, limits in config["original_speedometers"].items()}
        self.assertEqual(actual, expected)
        self.assertEqual(len(original_rows), len(actual))
        self.assertFalse({car["alias"] for car in config["cars"]} & set(actual))
        self.assertEqual(actual["Koenigsegg"], (800, 500))
        added = {alias: (float(kmh), float(mph)) for marker, alias, kmh, mph in added_rows}
        self.assertEqual(added, {car["alias"]: (car["speedometer"]["max_kmh"], car["speedometer"]["max_mph"])
                                 for car in config["cars"] if "speedometer" in car})
        self.assertEqual(len(added_rows), len(added))
        self.assertEqual(added["Panoz"], (650, 404))
        self.assertEqual(added["Form1"], (380, 240))
        self.assertNotIn("McLaren_F1", added)
        with zipfile.ZipFile(io.BytesIO(output[MOD.ARCHIVES[0]])) as hd, \
                zipfile.ZipFile(io.BytesIO(self.originals[MOD.ARCHIVES[0]])) as before:
            for alias in config["original_speedometers"]:
                self.assertEqual(hd.read(f"Cars/Tweak/{alias}.txt"), before.read(f"Cars/Tweak/{alias}.txt"))
            self.assertEqual(hd.read("Cars/Tweak/Panoz.txt").count(b"SpeedometerMaxKmh\t\t650\r\n"), 2)
            for car in config["cars"]:
                donor = before.read(f"Cars/Tweak/{car['donor']}.txt")
                variant = hd.read(f"Cars/Tweak/{car['alias']}.txt")
                original_style = next(line for line in donor.splitlines() if b"SPEEDOMETERTYPE" in line)
                self.assertEqual([line for line in variant.splitlines() if b"SPEEDOMETERTYPE" in line], [original_style])

    def test_default_original_speedometers_remain_retail_and_added_ranges_are_tagged(self):
        for name in ("cars.json", "cars.imported.json"):
            shipped = MOD.load_config(ROOT / "tools/car-mods" / name)
            self.assertFalse(shipped.get("original_speedometers"))
            settings = MOD.speedometer_settings(shipped).decode("ascii")
            rows = [row.split() for row in settings.splitlines() if row and not row.startswith("#")]
            self.assertTrue(all(row[0] == "added" and len(row) == 4 for row in rows))
            self.assertEqual({alias: (float(kmh), float(mph)) for marker, alias, kmh, mph in rows},
                             {car["alias"]: (car["speedometer"]["max_kmh"], car["speedometer"]["max_mph"])
                              for car in shipped["cars"] if "speedometer" in car})
        output = self.plan()
        with zipfile.ZipFile(io.BytesIO(output[MOD.ARCHIVES[0]])) as hd, \
                zipfile.ZipFile(io.BytesIO(self.originals[MOD.ARCHIVES[0]])) as before:
            for name in before.namelist():
                if name.startswith("Cars/Tweak/") and name.endswith(".txt"):
                    self.assertEqual(hd.read(name), before.read(name))

    def test_original_speedometer_validation_and_version_one_compatibility(self):
        path = self.game / "original-speedometers.json"
        legacy = copy.deepcopy(self.config)
        legacy.pop("original_speedometers")
        path.write_text(json.dumps(legacy))
        self.assertEqual(MOD.load_config(path), legacy)
        settings = self.plan(legacy)[MOD.SPEEDOMETER_FILE]
        self.assertIn(b"added Form1 380 240\n", settings)
        self.assertTrue(all(line.startswith(b"added ") for line in settings.splitlines() if not line.startswith(b"#")))
        without_maxima = copy.deepcopy(legacy)
        for car in without_maxima["cars"]:
            car.pop("speedometer", None)
        self.assertEqual(self.plan(without_maxima)[MOD.SPEEDOMETER_FILE].count(b"\n"), 1)
        for value in (None, [], {"UnknownCar": {"max_kmh": 800, "max_mph": 500}},
                      {"Koenigsegg": {"max_kmh": 800}},
                      {"Koenigsegg": {"max_kmh": 800, "max_mph": float("inf")}},
                      {"Koenigsegg": {"max_kmh": True, "max_mph": 500}},
                      {"Viper": {"max_kmh": 800, "max_mph": 751}}):
            config = copy.deepcopy(self.config)
            config["original_speedometers"] = value
            path.write_text(json.dumps(config))
            with self.subTest(value=value), self.assertRaises(ValueError):
                MOD.load_config(path)

    def test_speedometer_sidecar_is_owned_reapplied_and_removed_on_restore(self):
        output = self.plan()
        MOD.apply_plan(self.game, None, self.originals, output, self.config)
        path = self.game / MOD.SPEEDOMETER_FILE
        state, stock = MOD.read_install(self.game)
        self.assertIn(MOD.SPEEDOMETER_FILE, state["installed"])
        self.assertEqual(path.read_bytes(), output[MOD.SPEEDOMETER_FILE])
        config = copy.deepcopy(self.config)
        config["original_speedometers"]["Koenigsegg"] = {"max_kmh": 1000, "max_mph": 621}
        changed = self.plan(config)
        MOD.apply_plan(self.game, state, stock, changed, config)
        self.assertIn(b"Koenigsegg 1000 621\n", path.read_bytes())
        state, stock = MOD.read_install(self.game)
        MOD.restore(self.game, state, stock)
        self.assertFalse(path.exists())
        self.assertEqual((self.game / "UDATA/profile").read_bytes(), b"do not change player save")

    def test_added_speedometer_installs_hud_and_interior_range_then_clears_to_native(self):
        originals = self.with_retail_bases({
            "Lotus": retail_hud_tweak(18, "Lotus", 1),
            "Koenigsegg": retail_hud_tweak(15, "Koenigsegg", 4)})
        fallback_config = copy.deepcopy(self.config)
        for car in fallback_config["cars"]:
            car.pop("speedometer", None)
        fallback_config["original_speedometers"] = {"Lotus": {"max_kmh": 800, "max_mph": 500}}
        formula = fallback_config["cars"][2]
        formula["hud_style"] = "Koenigsegg"
        formula["driving_base"] = "Koenigsegg"
        fallback = MOD.build_plan(self.game, originals, fallback_config)[0]
        config = copy.deepcopy(fallback_config)
        config["cars"][2]["speedometer"] = {"max_kmh": 480, "max_mph": 300}
        MOD.validate_config(config)
        output = MOD.build_plan(self.game, originals, config)[0]
        self.assertEqual([name for name in output if output[name] != fallback[name]],
                         [MOD.ARCHIVES[0], MOD.SPEEDOMETER_FILE])
        rows = [line for line in output[MOD.SPEEDOMETER_FILE].splitlines() if not line.startswith(b"#")]
        self.assertEqual(rows, [b"Lotus 800 500", b"added Form1 480 300"])
        self.assertNotIn(b"\nForm1 ", output[MOD.SPEEDOMETER_FILE])
        self.assertGreater(MOD.apply_plan(self.game, None, originals, output, config), 0)
        state, stock = MOD.read_install(self.game)
        self.assertEqual(state["config"], config)
        self.assertEqual((self.game / MOD.SPEEDOMETER_FILE).read_bytes(), output[MOD.SPEEDOMETER_FILE])
        with zipfile.ZipFile(self.game / MOD.ARCHIVES[0]) as installed, \
                zipfile.ZipFile(io.BytesIO(originals[MOD.ARCHIVES[0]])) as before:
            actual = installed.read("Cars/Tweak/Form1.txt")
            self.assertEqual(actual.count(b"SpeedometerMaxKmh\t\t480\r\n"), 2)
            self.assertEqual(actual.count(b"SpeedometerMaxMph\t\t300\r\n"), 2)
            self.assertEqual(MOD.hud_speedometer_type(actual.decode(), "Form1"), 4)
            for name in before.namelist():
                if name.startswith("Cars/Tweak/") and name.endswith(".txt"):
                    self.assertEqual(installed.read(name), before.read(name))
        self.assertEqual(MOD.apply_plan(self.game, state, stock,
                                       MOD.build_plan(self.game, stock, config)[0], config), 0)
        cleared = MOD.build_plan(self.game, stock, fallback_config)[0]
        self.assertEqual(cleared, fallback)
        self.assertEqual(MOD.apply_plan(self.game, state, stock, cleared, fallback_config), 2)
        self.assertEqual((self.game / MOD.SPEEDOMETER_FILE).read_bytes(),
                         b"# MM3 speedometer maxima: alias max_kmh max_mph\nLotus 800 500\n")
        with zipfile.ZipFile(self.game / MOD.ARCHIVES[0]) as installed:
            actual = installed.read("Cars/Tweak/Form1.txt")
            self.assertIn(b"SpeedometerMaxKmh\t620\r\n", actual)
            self.assertIn(b"SpeedometerMaxMph\t500\r\n", actual)
            self.assertEqual(MOD.hud_speedometer_type(actual.decode(), "Form1"), 4)
        state, stock = MOD.read_install(self.game)
        MOD.restore(self.game, state, stock)
        self.assertFalse((self.game / MOD.SPEEDOMETER_FILE).exists())
        for name, expected in originals.items():
            self.assertEqual((self.game / name).read_bytes(), expected)
        self.assertEqual((self.game / "UDATA/profile").read_bytes(), b"do not change player save")

    def test_reapply_stats_is_idempotent_and_restore_is_exact(self):
        output = self.plan()
        self.assertGreater(MOD.apply_plan(self.game, None, self.originals, output, self.config), 0)
        state, stock = MOD.read_install(self.game)
        self.assertEqual(stock, self.originals)
        self.assertEqual(MOD.apply_plan(self.game, state, stock, output, self.config), 0)
        changed_config = copy.deepcopy(self.config)
        changed_config["cars"][0]["physics"]["Mass"] = 1050
        changed_config["cars"][0]["ratings"]["HANDLING"] = 8
        changed, _ = MOD.build_plan(self.game, stock, changed_config)
        self.assertGreater(MOD.apply_plan(self.game, state, stock, changed, changed_config), 0)
        with zipfile.ZipFile(self.game / MOD.ARCHIVES[0]) as archive:
            text = archive.read("Cars/Tweak/Panoz.txt")
            self.assertIn(b"Mass\t\t1050", text)
            self.assertIn(b"HANDLING\t\t8", text)
        state, stock = MOD.read_install(self.game)
        MOD.restore(self.game, state, stock)
        for name, original in self.originals.items():
            self.assertEqual((self.game / name).read_bytes(), original)
        self.assertFalse((self.game / "Data/Shared/Cars/Panoz/body.cmp").exists())
        self.assertEqual((self.game / "UDATA/profile").read_bytes(), b"do not change player save")

    def test_reapply_removes_retired_cars_without_changing_survivors_or_backups(self):
        retired = {9001: ("MRly", "mini", "Mini Rally"),
                   9002: ("MustangGT", "MFastBack", "Mustang GT"),
                   9003: ("VRace", "Viper", "Viper Race"),
                   9007: ("BChiron_GT", "Koenigsegg", "Bugatti Chiron"),
                   9008: ("BBolide_GT", "Koenigsegg", "Bugatti Bolide")}
        legacy = copy.deepcopy(self.config)
        for ident, (alias, donor, name) in retired.items():
            car = copy.deepcopy(self.config["cars"][0])
            car.update(id=ident, alias=alias, donor=donor, display_name=name)
            car.pop("engine_profile", None)  # Retired variants predate explicit factory profiles.
            legacy["cars"].append(car)
            manifest = self.game / "retired.json"
            manifest.write_text(json.dumps({"version": 1, "cars": [car]}))
            with self.assertRaisesRegex(ValueError, "fixed identities"):
                MOD.load_config(manifest)
        native_mini = {path: path.read_bytes() for suffix in ("", "_lod1")
                       for path in (self.game / f"Data/Shared/Cars/mini{suffix}").iterdir()}
        # Recreate the previous installation, whose state can contain identities
        # that are no longer accepted by the current editable catalog.
        previous = self.plan(legacy)
        MOD.apply_plan(self.game, None, self.originals, previous, legacy)
        package = self.game / "prepared models" / "retired-source.cmp"
        package.parent.mkdir()
        package.write_bytes(b"keep downloaded source")
        state, stock = MOD.read_install(self.game)
        output = self.plan()
        MOD.apply_plan(self.game, state, stock, output, self.config)
        installed, originals = MOD.read_install(self.game)
        self.assertEqual(installed["config"], self.config)
        self.assertEqual(originals, self.originals)
        for relative in set(previous) - set(output):
            self.assertFalse((self.game / relative).exists())
        for relative, data in output.items():
            if relative not in MOD.ARCHIVES:
                if relative != MOD.SPEEDOMETER_FILE:
                    self.assertEqual(data, previous[relative])
                self.assertEqual((self.game / relative).read_bytes(), data)
        for relative in MOD.ARCHIVES:
            self.assertEqual((self.game / MOD.STATE_DIR / Path(relative).name).read_bytes(), self.originals[relative])
            with zipfile.ZipFile(self.game / relative) as archive, zipfile.ZipFile(io.BytesIO(previous[relative])) as old:
                for name in archive.namelist():
                    if name != "Cars/Tweak/SortOrder":
                        self.assertEqual(archive.read(name), old.read(name))
                self.assertFalse(any(alias.casefold() in name.casefold()
                                     for alias, _, _ in retired.values() for name in archive.namelist()))
                if relative == MOD.ARCHIVES[0]:
                    order = archive.read("Cars/Tweak/SortOrder").splitlines()
                    self.assertFalse(set(str(ident).encode() for ident in retired) & set(order))
        self.assertEqual(package.read_bytes(), b"keep downloaded source")
        for path, original in native_mini.items():
            self.assertEqual(path.read_bytes(), original)
        self.assertEqual((self.game / "UDATA/profile").read_bytes(), b"do not change player save")

    def test_conflicting_edits_and_alias_collisions_fail_before_writes(self):
        output = self.plan()
        target = self.game / "Data/Shared/Cars/Panoz/body.cmp"
        target.parent.mkdir()
        target.write_bytes(b"unrelated existing mod")
        with self.assertRaisesRegex(ValueError, "already exists"):
            MOD.apply_plan(self.game, None, self.originals, output, self.config)
        self.assertEqual((self.game / MOD.ARCHIVES[0]).read_bytes(), self.originals[MOD.ARCHIVES[0]])
        target.unlink()
        MOD.apply_plan(self.game, None, self.originals, output, self.config)
        target.write_bytes(b"later manual edit")
        with self.assertRaisesRegex(ValueError, "changed outside"):
            MOD.read_install(self.game)

    def test_invalid_stats_unknown_fields_and_identities(self):
        path = self.game / "cars.json"
        for key, value, parent in [("HANDLING", 11, "ratings"), ("ImaginaryTorque", 20, "physics"), ("Mass", float("nan"), "physics"), ("id", 9008, None), ("display_name", "Different car", None)]:
            config = copy.deepcopy(self.config)
            target = config["cars"][0][parent] if parent else config["cars"][0]
            target[key] = value
            path.write_text(json.dumps(config))
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                MOD.load_config(path)
        MOD.load_config(ROOT / "tools/car-mods/cars.json")

    def test_gearing_serialization_and_validation(self):
        config = copy.deepcopy(self.config)
        panoz = config["cars"][0]
        ratios = [-3.3, 2.5, 1.72, 1.28, 1, 0.82, 0.7]
        panoz["physics"].update({"GearRatioVector": ratios, "WheelRatio": 3.8, "TimeChangeGear": 0.3})
        path = self.game / "gearing.json"
        path.write_text(json.dumps(config))
        MOD.load_config(path)
        output = self.plan(config)
        with zipfile.ZipFile(io.BytesIO(output[MOD.ARCHIVES[0]])) as archive:
            tweak = archive.read("Cars/Tweak/Panoz.txt")
            self.assertIn(b"NGears\t\t6\r\n", tweak)
            self.assertLess(tweak.index(b"NGears"), tweak.index(b"GearRatioVector"))
            self.assertIn(b"GearRatioVector\t\t-3.3 2.5 1.72 1.28 1 0.82 0.7\r\n", tweak)
            self.assertIn(b"WheelRatio\t\t3.8\r\n", tweak)
        fewer_gears = copy.deepcopy(config)
        fewer_gears["cars"][0]["physics"]["GearRatioVector"] = [-3.3, 2.5, 1.5, 0.8]
        fewer_output = self.plan(fewer_gears)
        with zipfile.ZipFile(io.BytesIO(fewer_output[MOD.ARCHIVES[0]])) as archive:
            tweak = archive.read("Cars/Tweak/Panoz.txt")
            self.assertIn(b"NGears\t\t3\r\n", tweak)
            self.assertIn(b"GearRatioVector\t\t-3.3 2.5 1.5 0.8\r\n", tweak)
            self.assertLess(tweak.index(b"NGears"), tweak.index(b"GearRatioVector"))
        for key, invalid in [("GearRatioVector", [3.3, 2, 1]), ("GearRatioVector", [-3.3, 2, 2]),
                             ("GearRatioVector", [-3.3]), ("GearRatioVector", [-3.3, 2, float("inf")]),
                             ("GearRatioVector", [-3.3, 4, 3, 2, 1.7, 1.4, 1.1, 0.8, 0.6]),
                             ("GearRatioVector", [-3.3, 2, True]), ("NGears", 6),
                             ("WheelRatio", 0), ("MinRPMGearChange", 9000),
                             ("CutRPM", 11000), ("GearStatusDownGearFactor", 1)]:
            rejected = copy.deepcopy(config)
            rejected["cars"][0]["physics"][key] = invalid
            path.write_text(json.dumps(rejected))
            with self.subTest(key=key, invalid=invalid), self.assertRaises(ValueError):
                MOD.load_config(path)

    def test_shipped_tuning_changes_only_weight_power_and_keeps_every_donor_drivetrain_field(self):
        # Native donor settings exercise cloning rather than adding a custom
        # gearbox to every variant. Reapplication starts from these pristine
        # fields, so a previous single-speed/high-RPM setup cannot leak back.
        donor_values = {
            "NGears": "5", "GearRatioVector": "-3.3 2.9 1.77 1.21 0.92 0.81",
            "WheelRatio": "4.0", "WheelRadius": "0.34", "MinRPM": "1000.0",
            "MinRPMGearChange": "4000.0", "CutRPM": "8000.0", "MaxRPM": "10000.0",
            "MaxTorqueRPM": "6500.0", "MaxTurboTorqueRPM": "6750.0",
            "TimeChangeGear": "0.4", "TimeChangeGearFR": "0.15",
            "GearStatusDownGearFactor": "0.75", "TurboSize": "0.5",
            "TorqueScaleFactor": "1.125", "BrakeTorque": "4000.0",
            "LatFrictionFactor": "2.0", "AirKZZ": "0.03", "AirKZY": "2.5",
        }
        with zipfile.ZipFile(io.BytesIO(self.originals[MOD.ARCHIVES[0]])) as source:
            donor = source.read("Cars/Tweak/Lotus.txt")
        donor = donor.replace(b"End(CarBody)",
            "".join(f"\t{key}\t{value}\r\n" for key, value in donor_values.items()).encode()
            + b"End(CarBody)")
        for car in self.config["cars"]:
            with self.subTest(car=car["alias"]):
                self.assertEqual(set(car["physics"]), {"Mass", "TorqueScale"})
                self.assertNotIn("engine_profile", car)
                self.assertNotIn("driving_base", car)
                output = MOD.tweak_variant(donor, car).decode()
                for key, value in donor_values.items():
                    line = re.search(r"^\s*" + key + r"\s+([^\r\n]+)", output, re.M)
                    self.assertIsNotNone(line, key)
                    self.assertEqual(line.group(1), value, key)
                for key, value in car["physics"].items():
                    self.assertRegex(output, r"\b" + key + r"\s+" + str(value) + r"\s")
        output = self.plan()
        self.assertEqual(len(output[MOD.FACTORY_PHYSICS_FILE].splitlines()), 1)

    def test_tweak_variant_without_driving_base_is_byte_identical(self):
        # Shipped and factory-preset cars on realistic retail donors; the digest
        # was recorded before base stats support was added.
        ids = {"Lotus": 18, "Viper": 11, "Koenigsegg": 15}
        cars = self.config["cars"] + MOD.factory_specs.apply_presets(self.config)["cars"]
        digest = hashlib.sha256()
        for car in cars:
            digest.update(MOD.tweak_variant(retail_tweak(ids[car["donor"]], car["donor"]), car))
        self.assertEqual(len(cars), 18)
        self.assertEqual(digest.hexdigest(), NO_BASE_TWEAK_DIGEST)

    def test_validate_config_matches_load_config_and_reads_no_files(self):
        path = self.game / "cars.json"
        path.write_text(json.dumps(self.config), encoding="utf-8")
        loaded = MOD.load_config(path)
        bad = copy.deepcopy(self.config)
        bad["cars"][0]["ratings"]["HANDLING"] = 11
        with patch.object(Path, "read_text", side_effect=AssertionError("validation read a file")), \
                patch.object(Path, "read_bytes", side_effect=AssertionError("validation read a file")):
            self.assertEqual(MOD.validate_config(copy.deepcopy(self.config)), loaded)
            with self.assertRaisesRegex(ValueError, "ratings"):
                MOD.validate_config(bad)

    def test_driving_base_validation_accepts_any_case_and_rejects_invalid_values(self):
        # Panoz uses a Viper body, so the Viper itself is not a separate base.
        for name in ("Corvette", "corvette", "DCBUS", "Mv_Badguy", "koenigsegg"):
            config = copy.deepcopy(self.config)
            config["cars"][0]["driving_base"] = name
            with self.subTest(name=name):
                self.assertEqual(MOD.validate_config(copy.deepcopy(config)), config)
        for name in (None, 42, "", " Corvette", "Viper", "viper", "Panoz", "car213", "Corvette.txt"):
            config = copy.deepcopy(self.config)
            config["cars"][0]["driving_base"] = name
            with self.subTest(name=name), self.assertRaises(ValueError):
                MOD.validate_config(config)
        config = copy.deepcopy(self.config)
        config["cars"][0] = MOD.factory_specs.apply_preset(config["cars"][0])
        config["cars"][0]["driving_base"] = "Corvette"
        with self.assertRaisesRegex(ValueError, "factory engine profile"):
            MOD.validate_config(config)

    def test_hud_style_validation_uses_original_aliases_independently_of_driving(self):
        for alias in MOD.original_stats.ORIGINAL_ALIASES.values():
            config = copy.deepcopy(self.config)
            config["cars"][2]["hud_style"] = alias.swapcase()
            with self.subTest(alias=alias):
                self.assertEqual(MOD.hud_style_alias(alias.swapcase()), alias)
                self.assertEqual(MOD.validate_config(copy.deepcopy(config)), config)
        for value in (None, True, 4, "", " Koenigsegg", "Koenigsegg ", "Form1", "car213", "Koenigsegg.txt"):
            config = copy.deepcopy(self.config)
            config["cars"][2]["hud_style"] = value
            with self.subTest(value=value), self.assertRaisesRegex(ValueError, "hud_style"):
                MOD.validate_config(config)
        config = copy.deepcopy(self.config)
        car = config["cars"][2]
        car["hud_style"] = "Lotus"  # Explicitly choosing its donor is valid.
        car["driving_base"] = "Koenigsegg"
        self.assertEqual(MOD.validate_config(copy.deepcopy(config)), config)
        car.pop("driving_base")
        config["cars"][2] = MOD.factory_specs.apply_preset(car)
        self.assertEqual(MOD.validate_config(copy.deepcopy(config)), config)

    def test_hud_style_changes_only_carinfo_gauge_on_an_independent_driving_base(self):
        car = copy.deepcopy(self.config["cars"][2])  # Formula 1 keeps the Lotus body.
        car["driving_base"] = "Koenigsegg"
        donor = retail_hud_tweak(18, "Lotus", 1, {"WheelRadius": "0.33", "BrakeTorque": "7500"})
        koenig = retail_hud_tweak(15, "Koenigsegg", 4).decode("ascii")
        plain = MOD.tweak_variant(donor, car, koenig)
        car["hud_style"] = "kOeNiGsEgG"
        styled = MOD.tweak_variant(donor, car, koenig, hud=koenig)
        before = MOD.original_stats.parse_sections(plain.decode("ascii"))
        after = MOD.original_stats.parse_sections(styled.decode("ascii"))
        self.assertEqual([s for s in before if s["name"] != "CarInfo"],
                         [s for s in after if s["name"] != "CarInfo"])
        info_before = next(s["entries"] for s in before if s["name"] == "CarInfo")
        info_after = next(s["entries"] for s in after if s["name"] == "CarInfo")
        self.assertEqual([entry for entry in info_before if entry[0] != "SPEEDOMETERTYPE"],
                         [entry for entry in info_after if entry[0] != "SPEEDOMETERTYPE"])
        self.assertEqual(dict(info_before)["SPEEDOMETERTYPE"], "1")
        self.assertEqual(dict(info_after)["SPEEDOMETERTYPE"], "4")
        self.assertEqual(sum(key == "SPEEDOMETERTYPE" for key, _ in info_after), 1)
        self.assertEqual((dict(info_after)["ID"], dict(info_after)["PAINTJOBNAME"]), ("9006", "Form1"))
        body = dict(carbody(styled))
        self.assertEqual((body["Mass"], body["TorqueScale"], body["BrakeTorque"], body["WheelRadius"]),
                         ("600", "420", "18000.0", "0.33"))
        with self.assertRaisesRegex(ValueError, "exactly when hud_style"):
            MOD.tweak_variant(donor, car, koenig)
        car.pop("hud_style")
        with self.assertRaisesRegex(ValueError, "exactly when hud_style"):
            MOD.tweak_variant(donor, car, koenig, hud=koenig)
        self.assertEqual(MOD.tweak_variant(donor, car, koenig), plain)

    def test_hud_style_source_requires_one_supported_native_gauge(self):
        source = retail_hud_tweak(15, "Koenigsegg", 4).decode("ascii")
        for gauge_type in range(5):
            self.assertEqual(MOD.hud_speedometer_type(
                source.replace("SPEEDOMETERTYPE\t4", f"SPEEDOMETERTYPE\t{gauge_type} // retail"), "Koenigsegg"),
                gauge_type)
        bad_sources = [source.replace("CarInfo", "OtherInfo"),
                       source + "Begin(CarInfo)\r\n\tSPEEDOMETERTYPE\t4\r\nEnd(CarInfo)\r\n",
                       source.replace("\tSPEEDOMETERTYPE\t4\r\n", ""),
                       source.replace("End(CarInfo)", "\tSPEEDOMETERTYPE\t1\r\nEnd(CarInfo)")]
        bad_sources.extend(source.replace("SPEEDOMETERTYPE\t4", "SPEEDOMETERTYPE\t" + value)
                           for value in ("-1", "5", "4.0", "0x4", "nan", "1 4", "Koenigsegg"))
        for text in bad_sources:
            with self.subTest(text=text), self.assertRaisesRegex(ValueError, "HUD style"):
                MOD.hud_speedometer_type(text, "Koenigsegg")

    def test_hud_style_installs_reapplies_and_clears_without_changing_other_assets(self):
        originals = self.with_retail_bases({
            "Lotus": retail_hud_tweak(18, "Lotus", 1, {"WheelRadius": "0.33"}),
            "Koenigsegg": retail_hud_tweak(15, "Koenigsegg", 4)})
        plain_config = copy.deepcopy(self.config)
        plain_config["cars"][2]["driving_base"] = "Koenigsegg"
        plain = MOD.build_plan(self.game, originals, plain_config)[0]
        config = copy.deepcopy(plain_config)
        config["cars"][2]["hud_style"] = "koenigsegg"
        MOD.validate_config(config)
        styled = MOD.build_plan(self.game, originals, config)[0]
        self.assertEqual(set(styled), set(plain))
        self.assertEqual([path for path in styled if styled[path] != plain[path]], [MOD.ARCHIVES[0]])
        with zipfile.ZipFile(io.BytesIO(plain[MOD.ARCHIVES[0]])) as before, \
                zipfile.ZipFile(io.BytesIO(styled[MOD.ARCHIVES[0]])) as after:
            self.assertEqual([name for name in before.namelist() if before.read(name) != after.read(name)],
                             ["Cars/Tweak/Form1.txt"])
            self.assertEqual(MOD.hud_speedometer_type(after.read("Cars/Tweak/Form1.txt").decode(), "Form1"), 4)
        self.assertGreater(MOD.apply_plan(self.game, None, originals, styled, config), 0)
        state, stock = MOD.read_install(self.game)
        self.assertEqual(state["config"]["cars"][2]["hud_style"], "koenigsegg")
        self.assertEqual(MOD.apply_plan(self.game, state, stock,
                                       MOD.build_plan(self.game, stock, config)[0], config), 0)
        cleared = MOD.build_plan(self.game, stock, plain_config)[0]
        self.assertEqual(cleared, plain)
        self.assertEqual(MOD.apply_plan(self.game, state, stock, cleared, plain_config), 1)
        with zipfile.ZipFile(self.game / MOD.ARCHIVES[0]) as installed, \
                zipfile.ZipFile(io.BytesIO(originals[MOD.ARCHIVES[0]])) as original:
            self.assertEqual(MOD.hud_speedometer_type(installed.read("Cars/Tweak/Form1.txt").decode(), "Form1"), 1)
            for name in original.namelist():
                if name != "Cars/Tweak/SortOrder":
                    self.assertEqual(installed.read(name), original.read(name))
        state, stock = MOD.read_install(self.game)
        MOD.restore(self.game, state, stock)
        for name, expected in originals.items():
            self.assertEqual((self.game / name).read_bytes(), expected)
        self.assertEqual((self.game / "UDATA/profile").read_bytes(), b"do not change player save")

    def test_hud_style_missing_or_invalid_original_source_fails_before_writes(self):
        config = copy.deepcopy(self.config)
        config["cars"][2]["hud_style"] = "Corvette"  # Valid original, absent in this fixture.
        with self.assertRaisesRegex(ValueError, "HUD style car Corvette was not found"):
            MOD.build_plan(self.game, self.originals, config)
        invalid = retail_hud_tweak(15, "Koenigsegg", 5)
        originals = self.with_retail_bases({"Koenigsegg": invalid})
        config["cars"][2]["hud_style"] = "Koenigsegg"
        with self.assertRaisesRegex(ValueError, "SPEEDOMETERTYPE from 0 to 4"):
            MOD.build_plan(self.game, originals, config)
        for name, expected in originals.items():
            self.assertEqual((self.game / name).read_bytes(), expected)
        self.assertFalse((self.game / MOD.STATE_DIR).exists())

    def test_driving_base_copies_driving_values_but_keeps_the_body(self):
        bus = retail_tweak(115, "DCBus", {
            "Mass": "15000", "TorqueScale": "900", "BrakeTorque": "20000.0", "WheelDrive": "2",
            "WheelRatio": "8.0", "WheelRadius": "0.6", "TurboSize": "0.0", "TurboSpinup": "0.0",
            "GearRatioVector": "-6.0 3.60 2.64 2.14 1.87 1.65 1.66\t"}).decode("ascii")
        car = copy.deepcopy(self.config["cars"][2])  # Formula 1 on a Lotus body.
        car["physics"] = {}
        car["driving_base"] = "dcbus"
        donor = retail_tweak(18, "Lotus", {"WheelRadius": "0.33", "TurboSize": "0.5", "TurboSpinup": "0.5"})
        output = MOD.tweak_variant(donor, car, bus)
        pairs = carbody(output)
        body = dict(pairs)
        keys = [key for key, _value in pairs]
        self.assertEqual(len(keys), len(set(keys)))
        self.assertEqual((body["Mass"], body["BrakeTorque"], body["WheelDrive"]), ("15000", "20000.0", "2"))
        self.assertEqual(body["GearRatioVector"], "-6.0 3.60 2.64 2.14 1.87 1.65 1.66")
        self.assertEqual(body["NGears"], "6")
        self.assertLess(keys.index("NGears"), keys.index("GearRatioVector"))
        self.assertEqual((body["TurboSize"], body["TurboSpinup"]), ("0.0", "0.0"))
        self.assertEqual(body["WheelRadius"], "0.33")  # Wheel size stays with the body.
        plain = MOD.tweak_variant(donor, {key: value for key, value in car.items() if key != "driving_base"})
        for key in BODY_ONLY:
            self.assertEqual(body[key], dict(carbody(plain))[key])
        for section in ("CarInfo", "CarWheel0", "CarCamera", "CarCameraSS"):
            pattern = rf"Begin\({section}\).*?End\({section}\)"
            self.assertEqual(re.search(pattern, output.decode(), re.S).group(0),
                             re.search(pattern, plain.decode(), re.S).group(0))

    def test_driving_base_overrides_win_and_override_gearbox_recomputes_ngears(self):
        base = retail_tweak(25, "Corvette", {"Mass": "1500", "TorqueScale": "800",
                                             "BrakeTorque": "7500.0"}).decode("ascii")
        car = copy.deepcopy(self.config["cars"][0])  # Panoz overrides Mass 980, TorqueScale 750.
        car["driving_base"] = "Corvette"
        car["physics"]["GearRatioVector"] = [-3.3, 2.5, 1.5, 0.8]
        body = dict(carbody(MOD.tweak_variant(retail_tweak(11, "Viper"), car, base)))
        self.assertEqual((body["Mass"], body["TorqueScale"]), ("980", "750"))
        self.assertEqual(body["BrakeTorque"], "7500.0")
        self.assertEqual((body["NGears"], body["GearRatioVector"]), ("3", "-3.3 2.5 1.5 0.8"))

    def test_driving_base_missing_optional_keys_remove_donor_lines(self):
        renault = retail_tweak(23, "Renault5", omit=("LatFrictionFactor", "SteeringEndSpeed")).decode("ascii")
        car = copy.deepcopy(self.config["cars"][0])
        car["driving_base"] = "Renault5"
        donor = retail_tweak(11, "Viper")
        body = dict(carbody(MOD.tweak_variant(donor, car, renault)))
        self.assertNotIn("LatFrictionFactor", body)
        self.assertNotIn("SteeringEndSpeed", body)
        car["physics"]["LatFrictionFactor"] = 1.4
        body = dict(carbody(MOD.tweak_variant(donor, car, renault)))
        self.assertEqual(body["LatFrictionFactor"], "1.4")
        broken = retail_tweak(23, "Renault5", omit=("BrakeTorque",)).decode("ascii")
        with self.assertRaisesRegex(ValueError, "lacks BrakeTorque"):
            MOD.tweak_variant(donor, car, broken)

    def test_driving_base_rejects_unusable_values_and_rechecks_rpm(self):
        car = copy.deepcopy(self.config["cars"][0])
        car["driving_base"] = "Corvette"
        donor = retail_tweak(11, "Viper")
        for driving in ({"Mass": "heavy"}, {"Mass": "0x5dc"}, {"Mass": "1e999"}, {"Mass": "1 2"},
                        {"NGears": "5"}, {"CutRPM": "6000.0"}, {"MaxTorqueRPM": "7900.0", "CutRPM": "9000.0",
                                                               "MaxTurboTorqueRPM": "8500.0"}):
            with self.subTest(driving=driving), self.assertRaises(ValueError):
                MOD.tweak_variant(donor, car, retail_tweak(25, "Corvette", driving).decode("ascii"))
        duplicate = retail_tweak(25, "Corvette").replace(b"End(CarBody)", b"\tMass\t1400\r\nEnd(CarBody)")
        with self.assertRaisesRegex(ValueError, "repeats Mass"):
            MOD.tweak_variant(donor, car, duplicate.decode("ascii"))
        with self.assertRaisesRegex(ValueError, "exactly when driving_base"):
            MOD.tweak_variant(donor, car)
        with self.assertRaisesRegex(ValueError, "exactly when driving_base"):
            MOD.tweak_variant(donor, self.config["cars"][0], retail_tweak(25, "Corvette").decode("ascii"))

    def test_build_plan_reads_base_from_pristine_archive_reapplies_and_restores(self):
        originals = self.with_retail_bases({
            "DCBus": retail_tweak(115, "DCBus", {"Mass": "15000", "BrakeTorque": "20000.0"}),
            "Corvette": retail_tweak(25, "Corvette", {"TorqueScale": "850"})})
        config = copy.deepcopy(self.config)
        config["cars"][2]["driving_base"] = "dcbus"  # Formula 1 on a Lotus body.
        config["cars"][2]["physics"] = {}
        MOD.validate_config(copy.deepcopy(config))
        output = MOD.build_plan(self.game, originals, config)[0]
        with zipfile.ZipFile(io.BytesIO(output[MOD.ARCHIVES[0]])) as hd:
            body = dict(carbody(hd.read("Cars/Tweak/Form1.txt")))
        self.assertEqual((body["Mass"], body["BrakeTorque"]), ("15000", "20000.0"))
        rejected = copy.deepcopy(config)
        rejected["cars"][2]["physics"]["Mass"] = 15000  # Typed values keep the override limit.
        with self.assertRaises(ValueError):
            MOD.validate_config(rejected)
        self.assertGreater(MOD.apply_plan(self.game, None, originals, output, config), 0)
        state, stock = MOD.read_install(self.game)
        self.assertEqual(state["config"]["cars"][2]["driving_base"], "dcbus")
        self.assertEqual(MOD.apply_plan(self.game, state, stock, MOD.build_plan(self.game, stock, config)[0], config), 0)
        switched = copy.deepcopy(config)
        switched["cars"][2]["driving_base"] = "Corvette"
        changed = MOD.build_plan(self.game, stock, switched)[0]
        self.assertEqual(changed[MOD.ARCHIVES[1]], output[MOD.ARCHIVES[1]])
        with zipfile.ZipFile(io.BytesIO(changed[MOD.ARCHIVES[0]])) as after, \
                zipfile.ZipFile(io.BytesIO(output[MOD.ARCHIVES[0]])) as before:
            differing = [name for name in after.namelist() if after.read(name) != before.read(name)]
            self.assertEqual(dict(carbody(after.read("Cars/Tweak/Form1.txt")))["TorqueScale"], "850")
        self.assertEqual(differing, ["Cars/Tweak/Form1.txt"])
        MOD.apply_plan(self.game, state, stock, changed, switched)
        state, stock = MOD.read_install(self.game)
        MOD.restore(self.game, state, stock)
        for name, original in originals.items():
            self.assertEqual((self.game / name).read_bytes(), original)

    def test_driving_base_missing_from_archive_fails_before_writes(self):
        config = copy.deepcopy(self.config)
        config["cars"][0]["driving_base"] = "Corvette"  # Valid name, absent from the fixture.
        with self.assertRaisesRegex(ValueError, "was not found in the original game data"):
            MOD.build_plan(self.game, self.originals, config)
        for name, original in self.originals.items():
            self.assertEqual((self.game / name).read_bytes(), original)
        self.assertFalse((self.game / MOD.STATE_DIR).exists())

    def test_catalog_ranking_ignores_driving_base(self):
        config = copy.deepcopy(self.config)
        with zipfile.ZipFile(io.BytesIO(self.originals[MOD.ARCHIVES[0]])) as archive:
            before = [car["id"] for car in MOD.catalog_ranking(archive, config)]
            for car in config["cars"]:
                car["driving_base"] = "Corvette"
            self.assertEqual([car["id"] for car in MOD.catalog_ranking(archive, config)], before)

    def test_cli_dry_run_names_the_base_stats_car(self):
        self.with_retail_bases({"Corvette": retail_tweak(25, "Corvette")})
        config = copy.deepcopy(self.config)
        config["cars"][0]["driving_base"] = "Corvette"
        path = self.game / "preview.json"
        path.write_text(json.dumps(config), encoding="utf-8")
        stdout = io.StringIO()
        with patch.object(sys, "argv", ["prepare.py", "--game-dir", str(self.game), "--config", str(path)]), \
                redirect_stdout(stdout):
            self.assertEqual(MOD.main(), 0)
        self.assertIn("9004: Panoz GTR-1 (Viper model, driving values from Corvette)", stdout.getvalue())

    def test_update_section_does_not_remove_longer_key_names(self):
        text = ("Begin(CarBody)\r\n\tTorqueScale\t1\r\n\tTorqueScaleFactor\t2\r\n\tMinRPM\t3\r\n"
                "\tMinRPMGearChange\t4\r\n\tTimeChangeGear\t5\r\n\tTimeChangeGearFR\t6\r\nEnd(CarBody)\r\n")
        result = MOD.update_section(text, "CarBody", {"TorqueScale": 9}, {"MinRPM", "TimeChangeGear"})
        self.assertEqual(dict(carbody(result)), {"TorqueScaleFactor": "2", "MinRPMGearChange": "4",
                                                 "TimeChangeGearFR": "6", "TorqueScale": "9"})

    def test_factory_profile_sidecar_opt_in_reapply_and_exact_restore(self):
        config = MOD.factory_specs.apply_presets(self.config)
        output = self.plan(config)
        profile_path = self.game / MOD.FACTORY_PHYSICS_FILE
        self.assertIn(b"Form1 18500 ", output[MOD.FACTORY_PHYSICS_FILE])
        self.assertIn(b"Regera_GT1 8000 ", output[MOD.FACTORY_PHYSICS_FILE])
        MOD.apply_plan(self.game, None, self.originals, output, config)
        state, originals = MOD.read_install(self.game)
        self.assertIn(MOD.FACTORY_PHYSICS_FILE, state["installed"])
        del config["cars"][0]["engine_profile"]
        changed = self.plan(config)
        self.assertNotIn(b"\nPanoz ", changed[MOD.FACTORY_PHYSICS_FILE])
        # The opt-in is metadata for the runtime hook, not a native tweak key.
        for archive in MOD.ARCHIVES:
            self.assertEqual(output[archive], changed[archive])
        MOD.apply_plan(self.game, state, originals, changed, config)
        self.assertEqual(profile_path.read_bytes(), changed[MOD.FACTORY_PHYSICS_FILE])
        state, originals = MOD.read_install(self.game)
        MOD.restore(self.game, state, originals)
        self.assertFalse(profile_path.exists())
        for name, data in self.originals.items():
            self.assertEqual((self.game / name).read_bytes(), data)

    def test_failed_write_rolls_back_existing_archives_and_new_assets(self):
        output = self.plan()
        write = MOD.atomic_write
        calls = 0

        def interrupted(path, data):
            nonlocal calls
            if MOD.STATE_DIR not in str(path):
                calls += 1
                if calls == 3:
                    raise OSError("simulated unavailable disk")
            return write(path, data)

        with patch.object(MOD, "atomic_write", side_effect=interrupted), self.assertRaises(OSError):
            MOD.apply_plan(self.game, None, self.originals, output, self.config)
        for name, original in self.originals.items():
            self.assertEqual((self.game / name).read_bytes(), original)
        self.assertFalse((self.game / MOD.STATE_DIR / "state.json").exists())
        self.assertFalse((self.game / "Data/Shared/Cars/Panoz/body.cmp").exists())

    def test_automatic_shifts_cannot_require_rpm_beyond_cutoff_or_native_torque_table(self):
        path = self.game / "rpm.json"
        retail = copy.deepcopy(self.config)
        retail["cars"][2].pop("engine_profile", None)
        retail["cars"][2]["physics"].update({"MinRPM": 1000, "MaxTorqueRPM": 6500,
            "MaxTurboTorqueRPM": 6750, "CutRPM": 8000, "MaxRPM": 10000})
        for key, value in (("CutRPM", 6000), ("MaxTorqueRPM", 8000),
                           ("MaxTurboTorqueRPM", 8000), ("MaxTorqueRPM", 8500),
                           ("MaxTurboTorqueRPM", 8500), ("MaxTurboTorqueRPM", 10500),
                           ("MaxTorqueRPM", 1000)):
            rejected = copy.deepcopy(retail)
            rejected["cars"][2]["physics"][key] = value
            path.write_text(json.dumps(rejected))
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                MOD.load_config(path)
        # A lower cutoff remains available when both automatic shift peaks are
        # lowered with it, rather than silently inheriting unusable donor peaks.
        valid = copy.deepcopy(retail)
        valid["cars"][2]["physics"].update({"MaxTorqueRPM": 5500, "MaxTurboTorqueRPM": 5700, "CutRPM": 6000})
        path.write_text(json.dumps(valid))
        MOD.load_config(path)

    def test_failed_restore_retains_valid_installed_state(self):
        output = self.plan()
        MOD.apply_plan(self.game, None, self.originals, output, self.config)
        state, stock = MOD.read_install(self.game)
        write = MOD.atomic_write
        calls = 0

        def interrupted(path, data):
            nonlocal calls
            calls += 1
            if calls == 2:
                raise OSError("simulated locked second archive")
            return write(path, data)

        with patch.object(MOD, "atomic_write", side_effect=interrupted), self.assertRaises(OSError):
            MOD.restore(self.game, state, stock)
        next_state, next_stock = MOD.read_install(self.game)
        self.assertEqual(next_state, state)
        self.assertEqual(next_stock, stock)
        self.assertEqual((self.game / MOD.ARCHIVES[0]).read_bytes(), output[MOD.ARCHIVES[0]])

    def test_cancelled_initial_install_restores_originals_and_removes_state(self):
        output = self.plan()
        state_path = self.game / MOD.STATE_DIR / "state.json"
        write = MOD.atomic_write
        for phase in ("before second archive", "after state commit"):
            interrupted = False

            def cancel(path, data):
                nonlocal interrupted
                if not interrupted and phase == "before second archive" and path == self.game / MOD.ARCHIVES[1]:
                    interrupted = True
                    raise KeyboardInterrupt("cancelled before second archive")
                result = write(path, data)
                if not interrupted and phase == "after state commit" and path == state_path:
                    interrupted = True
                    raise KeyboardInterrupt("cancelled after state commit")
                return result

            with self.subTest(phase=phase), patch.object(MOD, "atomic_write", side_effect=cancel):
                with self.assertRaises(KeyboardInterrupt):
                    MOD.apply_plan(self.game, None, self.originals, output, self.config)
            self.assertTrue(interrupted)
            self.assertFalse(state_path.exists())
            for relative, original in self.originals.items():
                self.assertEqual((self.game / relative).read_bytes(), original)
                self.assertEqual((self.game / MOD.STATE_DIR / Path(relative).name).read_bytes(), original)
            for relative in output:
                if relative not in MOD.ARCHIVES:
                    self.assertFalse((self.game / relative).exists())
            state, stock = MOD.read_install(self.game)
            self.assertIsNone(state)
            self.assertEqual(stock, self.originals)
            MOD.build_plan(self.game, stock, self.config)  # A retry can prepare cleanly.
            self.assertEqual((self.game / "UDATA/profile").read_bytes(), b"do not change player save")

    def test_cancelled_reapply_preserves_exact_installation_and_state(self):
        output = self.plan()
        MOD.apply_plan(self.game, None, self.originals, output, self.config)
        state, stock = MOD.read_install(self.game)
        state_path = self.game / MOD.STATE_DIR / "state.json"
        previous_state = state_path.read_bytes()
        config = copy.deepcopy(self.config)
        config["cars"][0]["physics"]["TorqueScale"] += 1
        changed = self.plan(config)
        write = MOD.atomic_write
        for phase in ("before state commit", "after state commit"):
            interrupted = False

            def cancel(path, data):
                nonlocal interrupted
                if not interrupted and path == state_path:
                    interrupted = True
                    if phase == "after state commit":
                        write(path, data)
                    raise KeyboardInterrupt("cancelled at state commit")
                return write(path, data)

            with self.subTest(phase=phase), patch.object(MOD, "atomic_write", side_effect=cancel):
                with self.assertRaises(KeyboardInterrupt):
                    MOD.apply_plan(self.game, state, stock, changed, config)
            self.assertTrue(interrupted)
            self.assertEqual(state_path.read_bytes(), previous_state)
            for relative, previous in output.items():
                self.assertEqual((self.game / relative).read_bytes(), previous)
            self.assertEqual(MOD.read_install(self.game), (state, stock))
            self.assertEqual((self.game / "UDATA/profile").read_bytes(), b"do not change player save")

    def test_cancelled_restore_preserves_exact_installation_and_state(self):
        output = self.plan()
        MOD.apply_plan(self.game, None, self.originals, output, self.config)
        state, stock = MOD.read_install(self.game)
        state_path = self.game / MOD.STATE_DIR / "state.json"
        previous_state = state_path.read_bytes()
        write, unlink = MOD.atomic_write, Path.unlink
        for phase in ("before second archive", "after state removal"):
            interrupted = False

            def cancel_write(path, data):
                nonlocal interrupted
                if not interrupted and phase == "before second archive" and path == self.game / MOD.ARCHIVES[1]:
                    interrupted = True
                    raise KeyboardInterrupt("cancelled before second archive")
                return write(path, data)

            def cancel_unlink(path, *args, **kwargs):
                nonlocal interrupted
                result = unlink(path, *args, **kwargs)
                if not interrupted and phase == "after state removal" and path == state_path:
                    interrupted = True
                    raise KeyboardInterrupt("cancelled after state removal")
                return result

            with self.subTest(phase=phase), patch.object(MOD, "atomic_write", side_effect=cancel_write), \
                    patch.object(Path, "unlink", autospec=True, side_effect=cancel_unlink):
                with self.assertRaises(KeyboardInterrupt):
                    MOD.restore(self.game, state, stock)
            self.assertTrue(interrupted)
            self.assertEqual(state_path.read_bytes(), previous_state)
            for relative, previous in output.items():
                self.assertEqual((self.game / relative).read_bytes(), previous)
            self.assertEqual(MOD.read_install(self.game), (state, stock))
            self.assertEqual((self.game / "UDATA/profile").read_bytes(), b"do not change player save")


if __name__ == "__main__":
    unittest.main()
