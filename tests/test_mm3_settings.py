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
            self.assertIn("MM3_SETTINGS_REVISION=2", path.read_text())

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

    def test_system_link_peers_stay_comma_separated_as_the_game_and_the_loader_read_them(self):
        spaced = "127.0.0.1:3076, example.local:3077   other:1"
        self.assertEqual(settings.normalise({"RECOMP_NET_PEERS": spaced})["RECOMP_NET_PEERS"],
                         "127.0.0.1:3076,example.local:3077,other:1")
        self.assertEqual(settings.normalise({"RECOMP_NET_PEERS": "  "})["RECOMP_NET_PEERS"], "")
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "launcher.ini"
            settings.write_ini(settings.normalise({"RECOMP_NET_PEERS": spaced}), [], path)
            self.assertIn("RECOMP_NET_PEERS=127.0.0.1:3076,example.local:3077,other:1\n", path.read_text(encoding="utf-8"))

    def test_the_resolution_and_frame_rate_lists_are_the_windows_launchers(self):
        names = [name for name, _value in settings.SCALES]
        self.assertEqual(names, ["Profile's own", "240p", "360p", "480p (Xbox)", "720p (HD)", "960p", "1080p (Full HD)",
                                 "1440p (QHD)", "2160p (4K)"])
        self.assertEqual(dict(settings.SCALES)["1080p (Full HD)"], "2.25")
        self.assertEqual(settings.FPS_LIMITS[:3], ["Profile's own", "15", "30"])
        self.assertIn("40", settings.FPS_LIMITS)                       # the Steam Deck's 40 Hz
        source = (ROOT / "src/mm3_display_choices.h").read_text(encoding="utf-8")
        scales = re.search(r"mm3_scale_choices\[\] = \{([^}]*)\}", source).group(1)
        self.assertEqual([float(v.strip().rstrip("f")) for v in scales.split(",")][1:],
                         [float(v) for v in settings.SCALE_VALUES])
        rates = re.search(r"mm3_fps_choices\[\] = \{([^}]*)\}", source).group(1)
        self.assertEqual([v.strip() for v in rates.split(",")], settings.FPS_LIMITS[1:])

    def test_a_saved_value_the_lists_lack_stays_chosen_in_order(self):
        custom = settings.scale_choices("2.7")
        self.assertEqual([n for n, _v in custom][-4:-1], ["1080p (Full HD)", "1296p (custom)", "1440p (QHD)"])
        self.assertEqual(settings.scale_choices("1.5"), settings.SCALES)
        self.assertEqual(settings.scale_choices("9"), settings.SCALES)              # the loader refuses it anyway
        self.assertEqual(settings.scale_choices("garbage"), settings.SCALES)
        self.assertEqual(settings.fps_choices("50")[4:7], ["45", "50", "60"])
        self.assertEqual(settings.fps_choices("60"), settings.FPS_LIMITS)
        self.assertEqual(settings.fps_choices("abc"), settings.FPS_LIMITS)

    def test_saving_works_on_python_3_9_where_path_write_text_has_no_newline_argument(self):
        with tempfile.TemporaryDirectory() as folder, \
                patch.object(Path, "write_text", side_effect=TypeError("no newline argument")):
            ini, extra = Path(folder) / "launcher.ini", Path(folder) / "launcher-display.ini"
            settings.write_ini(settings.normalise({}), [], ini)
            settings.write_display({"RECOMP_FPS_LIMIT": "", "RECOMP_FULLSCREEN": "0"}, extra)
            self.assertTrue(ini.read_text(encoding="utf-8").startswith("# Midtown Madness 3 launcher preferences\n"))
            self.assertEqual(extra.read_text(encoding="utf-8"), "RECOMP_FPS_LIMIT=\nRECOMP_FULLSCREEN=0\n")
            self.assertNotIn(b"\r", ini.read_bytes())

    def test_untouched_launcher_choices_do_not_override_the_in_game_settings(self):
        display = {"MM3_PERFORMANCE_PROFILE": "Xbox", "RECOMP_RENDER_SCALE": "", "RECOMP_FPS_LIMIT": "",
                   "RECOMP_FULLSCREEN": "0"}
        with patch.dict(settings.os.environ, {}, clear=True), patch.object(settings.os, "execvpe") as execute:
            settings.play(display)
        env = execute.call_args.args[2]
        self.assertEqual(env["MM3_PERFORMANCE_PROFILE"], "Xbox")
        for key in ("RECOMP_RENDER_SCALE", "RECOMP_FPS_LIMIT", "RECOMP_FULLSCREEN"):
            self.assertNotIn(key, env)                       # display.ini (F10, the Graphics menu) keeps deciding
        with patch.dict(settings.os.environ, {}, clear=True), patch.object(settings.os, "execvpe") as execute:
            settings.play(display | {"RECOMP_FULLSCREEN": "1", "RECOMP_FPS_LIMIT": "40"})
        self.assertEqual((execute.call_args.args[2]["RECOMP_FULLSCREEN"], execute.call_args.args[2]["RECOMP_FPS_LIMIT"]),
                         ("1", "40"))                         # a choice the player made is passed on
        with patch.dict(settings.os.environ, {}, clear=True), patch.object(settings.os, "execvpe") as execute:
            settings.play({"RECOMP_FULLSCREEN": "0"}, override=True)
        self.assertEqual(execute.call_args.args[2]["RECOMP_FULLSCREEN"], "0")      # --windowed still wins

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
