"""Steam pictures (issue #47 follow-up): Setup draws the game's pictures and copies them for the Steam
shortcut that starts the install, never over a picture the user chose; uninstall takes back its own.

Run inside `python scripts/devenv.py --shell`: needs Pillow, like Setup's own header picture.
"""
import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("mm3_installer_steam_art_test", ROOT / "installer/installer.py")
setup = importlib.util.module_from_spec(spec)
spec.loader.exec_module(setup)
sys.path.insert(0, str(ROOT / "installer"))      # build.py imports its sibling build_car_packages
build_spec = importlib.util.spec_from_file_location("mm3_installer_build_steam_art_test", ROOT / "installer/build.py")
build = importlib.util.module_from_spec(build_spec)
build_spec.loader.exec_module(build)
setup.legacy_folders = lambda: (None, None)   # never touch this PC's real per-user folders


def vdf_string(key, value):
    return b"\x01" + key.encode() + b"\x00" + value.encode() + b"\x00"


def vdf_number(key, value):
    return b"\x02" + key.encode() + b"\x00" + struct.pack("<I", value)


def shortcuts_vdf(entries):
    """Steam's binary shortcuts.vdf for [(appid, name, exe)], in the layout Steam writes."""
    body = b"\x00shortcuts\x00"
    for index, (appid, name, exe) in enumerate(entries):
        body += b"\x00" + str(index).encode() + b"\x00"
        body += vdf_number("appid", appid) + vdf_string("AppName", name) + vdf_string("Exe", f'"{exe}"')
        body += vdf_string("StartDir", str(Path(exe).parent) + "\\") + vdf_string("icon", "")
        body += vdf_number("IsHidden", 0) + vdf_number("AllowOverlay", 1) + b"\x00tags\x00\x08\x08"
    return body + b"\x08\x08"


class SteamArtTest(unittest.TestCase):
    NUMBER = 2735476592            # an id above 2**31: Steam's shortcut numbers are unsigned
    OTHER = 3103350145
    SCRIPT = 1234567891

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="mm3-steam-art-")
        self.addCleanup(self.temporary.cleanup)
        root = Path(self.temporary.name)
        self.dest = root / "Midtown Madness 3"
        self.dest.mkdir()
        self.steam = root / "Steam"
        self.config = self.steam / "userdata" / "487441085" / "config"
        self.config.mkdir(parents=True)
        self.second = self.steam / "userdata" / "854618404" / "config"
        self.second.mkdir(parents=True)               # another Steam account on this PC, with no shortcuts
        (self.config / "shortcuts.vdf").write_bytes(shortcuts_vdf([
            (self.NUMBER, "mm3_recomp", str(self.dest / setup.GAME_EXE)),
            (self.OTHER, "Some other game", str(root / "Other" / "other.exe")),
            (self.SCRIPT, "Midtown Madness 3 (script)", str(self.dest / setup.LAUNCHER)),
        ]))
        self.messages = []
        branding = root / "branding"
        branding.mkdir()
        from PIL import Image, ImageDraw
        logo = Image.new("RGBA", (350, 103), (0, 0, 0, 0))
        ImageDraw.Draw(logo).rectangle([20, 20, 330, 80], fill=(242, 183, 5, 255))
        logo.save(branding / "mm3_logo.png")
        original = setup.BRANDING
        setup.BRANDING = branding
        self.addCleanup(setattr, setup, "BRANDING", original)

    def log(self, line):
        self.messages.append(line)

    def draw(self):
        written = setup.make_steam_art(self.dest, self.log)
        self.assertEqual(len(written), 5, self.messages)
        return written

    def test_binary_vdf_reads_unsigned_numbers_and_nested_maps(self):
        tree = setup.parse_binary_vdf((self.config / "shortcuts.vdf").read_bytes())
        entry = tree["shortcuts"]["0"]
        self.assertEqual(entry["appid"], self.NUMBER)
        self.assertEqual(entry["AppName"], "mm3_recomp")
        self.assertEqual(entry["tags"], {})
        self.assertEqual(sorted(tree["shortcuts"]), ["0", "1", "2"])

    def test_finds_only_the_shortcuts_that_start_this_install(self):
        found = setup.steam_shortcuts_for(self.dest, self.steam)
        self.assertEqual(found, [(self.config, self.NUMBER), (self.config, self.SCRIPT)])
        self.assertEqual(setup.steam_shortcuts_for(self.dest, self.steam / "nowhere"), [])
        broken = self.steam / "userdata" / "1" / "config"
        broken.mkdir(parents=True)
        (broken / "shortcuts.vdf").write_bytes(b"\x00shortcuts\x00\x00")   # cut short: skipped, never an error
        self.assertEqual(len(setup.steam_shortcuts_for(self.dest, self.steam)), 2)

    def test_draws_the_five_pictures_at_steams_sizes(self):
        from PIL import Image
        sizes = {}
        for path in self.draw():
            with Image.open(path) as image:
                sizes[path.name] = image.size
        self.assertEqual(sizes["capsule-portrait.png"], (600, 900))
        self.assertEqual(sizes["capsule-wide.png"], (920, 430))
        self.assertEqual(sizes["hero.png"], (3840, 1240))
        self.assertEqual(sizes["icon.png"], (256, 256))
        self.assertEqual(sizes["logo.png"][0], 960)
        with Image.open(self.dest / setup.STEAM_ART_DIR / "logo.png") as logo:
            self.assertEqual(logo.getpixel((0, 0))[3], 0)                         # transparent

    def test_the_pictures_show_the_game_picture_sharp_in_the_middle_and_no_logo_ghost_at_the_sides(self):
        from PIL import Image, ImageDraw
        scene = Image.new("RGBA", (640, 480), (20, 40, 90, 255))
        ImageDraw.Draw(scene).rectangle([100, 250, 540, 420], fill=(200, 200, 220, 255))        # the car
        ImageDraw.Draw(scene).rectangle([60, 50, 580, 190], fill=(250, 220, 0, 255))            # the logo, baked in
        original = setup.steam_scene
        setup.steam_scene = lambda dest: scene
        self.addCleanup(setattr, setup, "steam_scene", original)
        self.draw()
        folder = self.dest / setup.STEAM_ART_DIR
        with Image.open(folder / "capsule-wide.png") as wide:
            self.assertEqual(wide.size, (920, 430))
            centre = wide.getpixel((460, 100))
            self.assertTrue(centre[0] > 180 and centre[2] < 90, centre)                       # the logo band, sharp
            self.assertLess(max(wide.getpixel((6, 100))[:3]), 80)                              # blurred street only, no yellow
        with Image.open(folder / "hero.png") as hero:
            self.assertEqual(hero.size, (3840, 1240))
            for x in range(100, 3840, 480):                                                    # blurred: no yellow logo anywhere
                for y in range(60, 1240, 300):
                    pixel = hero.getpixel((x, y))
                    self.assertFalse(pixel[0] > 150 and pixel[2] < 90, (x, y, pixel))
        self.assertFalse(any("without the game picture" in line for line in self.messages))

    def test_the_steam_icon_is_the_same_tile_as_the_shortcuts(self):
        from PIL import Image
        Image.new("RGBA", (256, 256), (200, 10, 10, 255)).save(setup.BRANDING / "mm3.png")
        self.draw()
        with Image.open(self.dest / setup.STEAM_ART_DIR / "icon.png") as icon:
            self.assertEqual(icon.size, (256, 256))
            self.assertEqual(icon.getpixel((128, 128))[:3], (200, 10, 10))

    def test_the_red_3_is_cut_out_of_the_logo_with_its_outline_only(self):
        from PIL import Image, ImageDraw
        logo = Image.new("RGBA", (350, 103), (0, 0, 0, 0))
        draw = ImageDraw.Draw(logo)
        draw.rectangle([20, 20, 257, 80], fill=(250, 220, 0, 255))                              # the letters beside the 3
        draw.rectangle([255, 10, 340, 90], fill=(0, 0, 0, 255))                                 # its outline
        draw.rectangle([262, 16, 333, 84], fill=(225, 30, 40, 255))                             # its red
        three = build.logo_three(logo)
        self.assertIsNotNone(three)
        self.assertLess(three.width, 100)
        self.assertEqual(three.getpixel((three.width // 2, three.height // 2))[:3], (225, 30, 40))
        self.assertEqual(three.getpixel((0, three.height // 2))[3], 0)                         # the yellow beside it is gone
        self.assertIsNone(build.logo_three(Image.new("RGBA", (350, 103), (250, 220, 0, 255))))  # no red: the caller falls back

    def test_no_logo_means_no_pictures_and_no_error(self):
        setup.BRANDING = Path(self.temporary.name) / "no-branding"
        self.assertEqual(setup.make_steam_art(self.dest, self.log), [])

    def test_copies_for_each_shortcut_named_by_its_own_number(self):
        self.draw()
        copied = setup.place_steam_art(self.dest, self.log, self.steam)
        grid = self.config / "grid"
        names = {path.name for path in copied}
        for number in (self.NUMBER, self.SCRIPT):
            self.assertLessEqual({f"{number}p.png", f"{number}.png", f"{number}_hero.png", f"{number}_logo.png",
                                  f"{number}_icon.png"}, names)
        self.assertFalse(any(str(self.OTHER) in path.name for path in grid.iterdir()))
        self.assertFalse((self.second / "grid").exists())
        self.assertEqual(len(copied), 10)
        self.assertTrue(any("Restart Steam" in line for line in self.messages))
        self.assertEqual(setup.place_steam_art(self.dest, self.log, self.steam), [])   # nothing new the second time

    def test_a_picture_the_user_chose_is_never_replaced(self):
        self.draw()
        grid = self.config / "grid"
        grid.mkdir()
        (grid / f"{self.NUMBER}p.jpg").write_bytes(b"their own portrait")        # another extension: still theirs
        (grid / f"{self.NUMBER}_hero.png").write_bytes(b"their own hero")
        setup.place_steam_art(self.dest, self.log, self.steam)
        self.assertEqual((grid / f"{self.NUMBER}p.jpg").read_bytes(), b"their own portrait")
        self.assertFalse((grid / f"{self.NUMBER}p.png").exists())
        self.assertEqual((grid / f"{self.NUMBER}_hero.png").read_bytes(), b"their own hero")
        self.assertTrue((grid / f"{self.NUMBER}.png").is_file())                  # the rest is filled in

    def test_our_earlier_pictures_are_refreshed_until_the_user_changes_them(self):
        self.draw()
        setup.place_steam_art(self.dest, self.log, self.steam)
        grid = self.config / "grid"
        icon = grid / f"{self.NUMBER}_icon.png"
        (self.dest / setup.STEAM_ART_DIR / "icon.png").write_bytes(b"new icon")
        wide = grid / f"{self.NUMBER}.png"
        wide.write_bytes(b"edited by the user afterwards")
        (self.dest / setup.STEAM_ART_DIR / "capsule-wide.png").write_bytes(b"new wide")
        setup.place_steam_art(self.dest, self.log, self.steam)
        self.assertEqual(icon.read_bytes(), b"new icon")                          # ours, refreshed
        self.assertEqual(wide.read_bytes(), b"edited by the user afterwards")     # theirs now, left alone

    def test_uninstall_takes_back_only_what_setup_copied(self):
        self.draw()
        setup.place_steam_art(self.dest, self.log, self.steam)
        grid = self.config / "grid"
        (grid / f"{self.NUMBER}.png").write_bytes(b"changed by the user")
        recorded = json.loads((self.dest / setup.STEAM_ART_DIR / setup.STEAM_ART_RECORD).read_text())
        self.assertEqual(len(recorded), 10)
        setup.remove_steam_art(self.dest)
        self.assertFalse((self.dest / setup.STEAM_ART_DIR).exists())
        self.assertEqual({path.name for path in grid.iterdir()}, {f"{self.NUMBER}.png"})
        self.assertEqual((grid / f"{self.NUMBER}.png").read_bytes(), b"changed by the user")


if __name__ == "__main__":
    unittest.main()
