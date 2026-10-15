"""Configured speedometer preview checks without a game or native decoder."""
import builtins
import importlib.util
import io
import math
from pathlib import Path
import unittest
from unittest.mock import patch

try:
    from PIL import Image
except ImportError:
    Image = None


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("car_hud_preview_test", ROOT / "tools/car-mods/hud_preview.py")
HUD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(HUD)


class HudPreviewTest(unittest.TestCase):
    def test_labels_end_at_chosen_maximum_and_needle_uses_that_range(self):
        four_hundred = HUD.dial_geometry(1, True, 400, speed_kmh=400)
        eight_hundred = HUD.dial_geometry(1, True, 800, speed_kmh=400)
        self.assertEqual([value for value, point in four_hundred["labels"]],
                         [0, 50, 100, 150, 200, 250, 300, 350, 400])
        self.assertEqual([value for value, point in eight_hundred["labels"]],
                         [0, 100, 200, 300, 400, 500, 600, 700, 800])
        self.assertEqual(four_hundred["needle_angle"], 415)
        self.assertEqual(eight_hundred["needle_angle"], 270)
        self.assertEqual(eight_hundred["labels"][4][0], 400)
        self.assertAlmostEqual(eight_hundred["labels"][4][1][0], 0)
        self.assertAlmostEqual(eight_hundred["labels"][4][1][1], -42)
        self.assertEqual(HUD.dial_geometry(1, True, 100)["labels"][1][0], 13)

    def test_each_native_arc_is_supported_and_uses_display_units(self):
        for gauge_type in range(5):
            for metric in (True, False):
                geometry = HUD.dial_geometry(gauge_type, metric, 400)
                with self.subTest(gauge_type=gauge_type, metric=metric):
                    self.assertEqual(len(geometry["ticks"]), 17)
                    self.assertEqual(len(geometry["labels"]), 9)
                    self.assertEqual(geometry["labels"][-1][0], 400)
                    self.assertEqual(geometry["needle_angle"], geometry["start"])
        self.assertEqual((HUD.dial_geometry(4, True, 400)["start"],
                          HUD.dial_geometry(4, True, 400)["end"]), (106, 434))
        imperial = HUD.dial_geometry(1, False, 400, speed_kmh=321.8688)
        self.assertEqual((imperial["start"], imperial["end"]), (109, 417.5))
        self.assertAlmostEqual(imperial["value"], 200, places=4)
        self.assertAlmostEqual(imperial["needle_angle"], 263.25, places=4)
        self.assertEqual(HUD.dial_geometry(1, True, 400, speed_kmh=-100)["value"], 0)
        self.assertEqual(HUD.dial_geometry(1, True, 400, speed_kmh=1000)["value"], 400)

    def test_native_maximum_routes_to_retail_artwork(self):
        maxima = ((300, 150), (300, 200), (160, 100), (220, 180), (380, 280))
        for gauge_type, pair in enumerate(maxima):
            for metric, maximum in ((True, pair[0]), (False, pair[1])):
                with self.subTest(gauge_type=gauge_type, metric=metric), \
                        patch.object(HUD, "load_native_dial", return_value=b"retail") as native, \
                        patch.object(HUD, "_custom_dial_png") as custom:
                    self.assertEqual(HUD.load_dial(Path("no-game"), gauge_type, metric, maximum), b"retail")
                    native.assert_called_once_with(Path("no-game"), gauge_type, metric)
                    custom.assert_not_called()
        with patch.object(HUD, "load_native_dial", return_value=None):
            self.assertIsNone(HUD.load_dial(Path("no-game"), 1, True, 300))

    def test_invalid_ranges_and_types_return_none_without_loading_assets(self):
        cases = [(-1, True, 400), (5, True, 400), (True, True, 400),
                 (1, 1, 400), (1, None, 400), (1, True, None),
                 (1, True, "400"), (1, True, True), (1, True, 99),
                 (1, True, 1201), (1, False, 59), (1, False, 751),
                 (1, True, math.nan), (1, True, math.inf), (1, True, 10 ** 500)]
        with patch.object(HUD, "load_native_dial") as native, patch.object(HUD, "_custom_dial_png") as custom:
            for gauge_type, metric, maximum in cases:
                with self.subTest(gauge_type=gauge_type, metric=metric, maximum=maximum):
                    self.assertIsNone(HUD.load_dial(Path("no-game"), gauge_type, metric, maximum))
                    self.assertIsNone(HUD.dial_geometry(gauge_type, metric, maximum))
            self.assertIsNone(HUD.load_dial(None, 1, True, 400))
            native.assert_not_called()
            custom.assert_not_called()

    def test_missing_pillow_leaves_preview_optional_even_after_cache_use(self):
        if Image is not None:
            self.assertIsNotNone(HUD.load_dial(Path("no-game"), 1, True, 400))
        real_import = builtins.__import__

        def without_pillow(name, *args, **kwargs):
            if name == "PIL" or name.startswith("PIL."):
                raise ImportError("optional Pillow unavailable")
            return real_import(name, *args, **kwargs)

        with patch("builtins.__import__", side_effect=without_pillow):
            self.assertIsNone(HUD.load_dial(Path("no-game"), 1, True, 400))
            self.assertIsNone(HUD.load_dial(Path("no-game"), 1, True, 300))
            self.assertEqual(HUD.dial_geometry(1, True, 400)["labels"][-1][0], 400)

    @unittest.skipIf(Image is None, "Optional Pillow unavailable")
    def test_configured_preview_needs_no_archive_or_decoder_and_has_native_geometry(self):
        with patch.object(HUD, "load_native_dial", side_effect=AssertionError("native route used")), \
                patch.object(HUD.zipfile, "ZipFile", side_effect=AssertionError("archive opened")), \
                patch.object(HUD, "_decoder", side_effect=AssertionError("decoder loaded")):
            png = HUD.load_dial(Path("no-game"), 1, True, 400)
            larger = HUD.load_dial(Path("no-game"), 1, True, 800)
        with Image.open(io.BytesIO(png)) as image, Image.open(io.BytesIO(larger)) as larger_image:
            self.assertEqual((image.size, image.mode), ((160, 160), "RGBA"))
            self.assertEqual(image.getpixel((0, 0))[3], 0)
            hub = image.getpixel((80, 80))
            self.assertEqual(hub[3], 255)
            self.assertTrue(all(abs(actual - target) <= 15 for actual, target in zip(hub[:3], (222, 232, 245))))
            self.assertEqual(image.getpixel((110, 80)), (9, 11, 14, 255))
            needle = image.getpixel((59, 110))
            self.assertGreater(needle[0], 220)
            self.assertLess(needle[1], 100)
            self.assertLess(needle[2], 60)
            # Endpoint label is visibly different for 400 versus 800; its
            # location and all zero-speed needle geometry stay the same.
            self.assertNotEqual(image.crop((101, 114, 118, 129)).tobytes(),
                                larger_image.crop((101, 114, 118, 129)).tobytes())
            region = image.crop((101, 114, 118, 129))
            self.assertTrue(any(r > 200 and g > 200 and b > 200 and alpha > 200
                                for r, g, b, alpha in (region.getpixel((x, y))
                                                       for x in range(region.width) for y in range(region.height))))

    @unittest.skipIf(Image is None, "Optional Pillow unavailable")
    def test_all_styles_render_custom_pngs_with_bounded_cache(self):
        HUD._custom_dial_png.cache_clear()
        for gauge_type in range(5):
            for metric in (True, False):
                png = HUD.load_dial(Path("no-game"), gauge_type, metric, 400)
                self.assertTrue(png.startswith(b"\x89PNG\r\n\x1a\n"))
                self.assertIs(HUD.load_dial(Path("no-game"), gauge_type, metric, 400), png)
        for maximum in range(401, 435):
            self.assertIsNotNone(HUD.load_dial(Path("no-game"), 1, True, maximum))
        self.assertEqual(HUD._custom_dial_png.cache_info().maxsize, 32)
        self.assertEqual(HUD._custom_dial_png.cache_info().currsize, 32)


if __name__ == "__main__":
    unittest.main()
