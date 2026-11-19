"""The Linux Settings window writes the launcher.ini the game's loader reads, with the Windows launcher's options."""
import importlib.util
from pathlib import Path
import re
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("mm3_settings_test", ROOT / "installer/mm3_settings.py")
settings = importlib.util.module_from_spec(spec)
spec.loader.exec_module(settings)


class SettingsTests(unittest.TestCase):
    def test_options_match_the_launcher_source(self):
        source = (ROOT / "src/mm3_launcher_config.c").read_text(encoding="utf-8")
        table = [(key, label, int(default)) for key, label, default in
                 re.findall(r'\{"([A-Z0-9_]+)", "([^"]+)", (\d)\}', source)]
        self.assertEqual(table, settings.OPTIONS)

    def test_master_switch_lists_match_the_launcher_source(self):
        source = (ROOT / "src/mm3_launcher_config.c").read_text(encoding="utf-8")
        keys = [key for key, _l, _d in settings.OPTIONS]
        names = re.findall(r"MM3_LAUNCH_([A-Z0-9_]+)", source[source.index("original_look[]"):source.index("revision_defaults")])
        self.assertEqual(len(names), len(settings.ORIGINAL_LOOK) + len(settings.XBOX_GAMEPLAY))

    def test_round_trip_keeps_other_lines(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "launcher.ini"
            settings.write_ini({"MM3_AI_LEVEL": "3", "MM3_FOV_DEGREES": "30", "RECOMP_NET": 1,
                                "MM3_SKIP_LAUNCHER": 1}, ["RECOMP_KEYMAP=A:5A"], path)
            values, others = settings.read_ini(path)
            self.assertEqual(others, ["RECOMP_KEYMAP=A:5A"])
            self.assertEqual(values["MM3_AI_LEVEL"], "3")
            self.assertEqual(values["MM3_FOV_DEGREES"], "0")      # 30 is not a valid field of view
            self.assertEqual(values["RECOMP_NET"], "1")
            self.assertEqual(values["MM3_SKIP_LAUNCHER"], "1")
            self.assertIn("MM3_SETTINGS_REVISION=3", path.read_text())

    def test_bad_values_fall_back_to_defaults(self):
        values = settings.normalise({"MM3_DAMAGE_PERCENT": "9999", "MM3_PLAYER1_INPUT": "77", "RECOMP_USB_PADS": "x"})
        self.assertEqual(values["MM3_DAMAGE_PERCENT"], 100)
        self.assertEqual(values["MM3_PLAYER1_INPUT"], 0)
        self.assertEqual(values["RECOMP_USB_PADS"], "1")
        self.assertEqual(values["MM3_SKIP_LAUNCHER"], 0)

    def test_racing_wheel_fields_round_trip_with_other_settings(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "launcher.ini"
            settings.write_ini({"MM3_WHEEL_ENABLED": 1, "MM3_WHEEL_DEVICE": "046D:C24F",
                                "MM3_WHEEL_AXES": "-1, +4, -255", "MM3_WHEEL_FFB": 25,
                                "MM3_SKIP_LAUNCHER": 1}, ["RECOMP_KEYMAP=A:5A"], path)
            saved, others = settings.read_ini(path)
            self.assertEqual({key: saved[key] for key in settings.WHEEL_DEFAULTS},
                             {"MM3_WHEEL_ENABLED": "1", "MM3_WHEEL_DEVICE": "046d:c24f",
                              "MM3_WHEEL_AXES": "-1,4,-255", "MM3_WHEEL_FFB": "25"})
            self.assertEqual(saved["MM3_SKIP_LAUNCHER"], "1")
            self.assertEqual(others, ["RECOMP_KEYMAP=A:5A"])

    def test_invalid_wheel_input_reports_the_field_and_saved_errors_use_defaults(self):
        for key, invalid, message in (
                ("MM3_WHEEL_ENABLED", "on", "racing wheel on or off"),
                ("MM3_WHEEL_DEVICE", "0000:c24f", "Device ID"),
                ("MM3_WHEEL_DEVICE", "auto", "Device ID"),
                ("MM3_WHEEL_AXES", "1,-1,3", "different axis numbers"),
                ("MM3_WHEEL_AXES", "0,2,3", "different axis numbers"),
                ("MM3_WHEEL_AXES", "1,2,-256", "different axis numbers"),
                ("MM3_WHEEL_FFB", "26", "feedback strength")):
            with self.subTest(key=key, invalid=invalid):
                values = {key: invalid}
                with self.assertRaisesRegex(ValueError, message):
                    settings.wheel_values(values, strict=True)
                self.assertEqual(settings.normalise(values)[key], settings.WHEEL_DEFAULTS[key])

    def test_launch_flags_override_saved_preference_and_legacy_environment(self):
        cases = [
            ([], {}, {}, True),
            ([], {"MM3_SKIP_LAUNCHER": "1"}, {}, False),
            ([], {}, {"MM3_NO_SETTINGS": "1"}, False),
            (["--settings"], {"MM3_SKIP_LAUNCHER": "1"}, {"MM3_NO_SETTINGS": "1"}, True),
            (["--settings", "--play"], {}, {}, False),
            (["--play", "--settings"], {}, {}, True),
        ]
        for args, saved, environment, show in cases:
            with self.subTest(args=args, saved=saved, environment=environment), \
                    patch.dict(settings.os.environ, {"DISPLAY": ":0", **environment}, clear=True), \
                    patch.object(settings, "read_ini", return_value=(saved, [])), \
                    patch.object(settings, "read_display", return_value={"RECOMP_FULLSCREEN": "1"}), \
                    patch.object(settings, "window", return_value=None) as window, \
                    patch.object(settings, "play") as play:
                settings.main(args)
                if show:
                    window.assert_called_once_with()
                    play.assert_not_called()
                else:
                    window.assert_not_called()
                    play.assert_called_once_with({"RECOMP_FULLSCREEN": "1"})

    def test_headless_keeps_saved_display_without_opening_tk(self):
        with patch.dict(settings.os.environ, {}, clear=True), patch.object(settings.sys, "platform", "linux"), \
                patch.object(settings, "read_ini", return_value=({}, [])), \
                patch.object(settings, "read_display", return_value={"RECOMP_FULLSCREEN": "1"}), \
                patch.object(settings, "window") as window, patch.object(settings, "play") as play:
            settings.main(["--settings"])
        window.assert_not_called()
        play.assert_called_once_with({"RECOMP_FULLSCREEN": "1"})

    def test_direct_launch_only_uses_saved_display_overrides(self):
        with tempfile.TemporaryDirectory() as folder:
            display = Path(folder) / "launcher-display.ini"
            self.assertEqual(settings.read_display(display, use_defaults=False), {})
            display.write_bytes(b"RECOMP_FPS_LIMIT=45\nMM3_PERFORMANCE_PROFILE=\xff\n")
            self.assertEqual(settings.read_display(display, use_defaults=False), {"RECOMP_FPS_LIMIT": "45"})

    def test_direct_launch_keeps_explicit_environment_and_execs_guarded_shell(self):
        with patch.dict(settings.os.environ, {"RECOMP_FULLSCREEN": "0"}, clear=True), \
                patch.object(settings.os, "execvpe") as execute:
            settings.play({"RECOMP_FULLSCREEN": "1", "RECOMP_FPS_LIMIT": "45", "RECOMP_RENDER_SCALE": ""})
        program, argv, env = execute.call_args.args
        self.assertEqual(program, "sh")
        self.assertEqual(argv, ["sh", str(settings.START)])
        self.assertEqual(env, {"MM3_SETTINGS_APPLIED": "1", "MM3_NO_SETTINGS": "1",
                               "MM3_LAUNCHER_CONFIG": str(settings.INI),
                               "RECOMP_FULLSCREEN": "0", "RECOMP_FPS_LIMIT": "45"})


if __name__ == "__main__":
    unittest.main()
