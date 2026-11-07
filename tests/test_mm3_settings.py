"""The Linux Settings window writes the launcher.ini the game's loader reads, with the Windows launcher's options."""
import importlib.util
from pathlib import Path
import re
import tempfile
import unittest

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
            settings.write_ini({"MM3_AI_LEVEL": "3", "MM3_FOV_DEGREES": "30", "RECOMP_NET": 1}, ["RECOMP_KEYMAP=A:5A"], path)
            values, others = settings.read_ini(path)
            self.assertEqual(others, ["RECOMP_KEYMAP=A:5A"])
            self.assertEqual(values["MM3_AI_LEVEL"], "3")
            self.assertEqual(values["MM3_FOV_DEGREES"], "0")      # 30 is not a valid field of view
            self.assertEqual(values["RECOMP_NET"], "1")
            self.assertIn("MM3_SETTINGS_REVISION=2", path.read_text())

    def test_bad_values_fall_back_to_defaults(self):
        values = settings.normalise({"MM3_DAMAGE_PERCENT": "9999", "MM3_PLAYER1_INPUT": "77", "RECOMP_USB_PADS": "x"})
        self.assertEqual(values["MM3_DAMAGE_PERCENT"], 100)
        self.assertEqual(values["MM3_PLAYER1_INPUT"], 0)
        self.assertEqual(values["RECOMP_USB_PADS"], "1")


if __name__ == "__main__":
    unittest.main()
