"""Disc reuse and local official DLC imports preserve existing game data."""
import importlib.util
from pathlib import Path
import struct
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import zipfile


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("mm3_installer_test", ROOT / "installer/installer.py")
setup = importlib.util.module_from_spec(spec)
spec.loader.exec_module(setup)
PACK = "4d53002a00000005"


def metadata(pack=PACK):
    data = bytearray(108)
    data[20:24] = b"XCSF"
    struct.pack_into("<I", data, 24, 108)
    data[32] = 1
    struct.pack_into("<III", data, 36, 0x4D53002A, int(pack[-8:], 16), 0x4D53002A)
    return bytes(data)


def pack_files(pack=PACK):
    files = {name: b"asset" for name in setup.dlc_required_files(pack)}
    files["contentmeta.xbx"] = metadata(pack)
    return files


def make_zip(path, packs=None, extra=None):
    with zipfile.ZipFile(path, "w") as archive:
        for pack, files in (packs or {PACK: pack_files()}).items():
            for name, data in files.items():
                archive.writestr(f"Midtown Madness 3 DLC/content/c/{pack}/{name}", data)
        for name, data in (extra or {}).items():
            archive.writestr(name, data)


class NativeMetadataTest(unittest.TestCase):
    def test_signs_blank_offline_metadata_for_runtime_key(self):
        signed = setup.native_dlc_metadata(metadata(), PACK)
        self.assertEqual(signed[:20].hex(), "57550046d4714d448ffd8634629dc647d8767593")
        self.assertEqual(signed[20:], metadata()[20:])
        self.assertEqual(setup.native_dlc_metadata(signed, PACK), signed)

    def test_rejects_other_title_wrong_content_id_and_short_header(self):
        for offset, value in ((24, 52), (24, 109), (36, 0x12345678), (40, 1), (44, 0)):
            broken = bytearray(metadata())
            struct.pack_into("<I", broken, offset, value)
            with self.subTest(offset=offset, value=value), self.assertRaises(setup.InstallError):
                setup.native_dlc_metadata(broken, PACK)


class DlcImportTest(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory()
        self.addCleanup(self.scratch.cleanup)
        self.root = Path(self.scratch.name)
        self.game = self.root / "game_files"
        self.game.mkdir()
        self.archive = self.root / "dlc.zip"

    @property
    def content(self):
        return self.game / "TDATA" / setup.DLC_TITLE_ID / "$C"

    def test_imports_six_packs_excludes_console_updates_and_preserves_edits(self):
        all_packs = {pack: pack_files(pack) for pack in setup.DLC_PACKS}
        make_zip(self.archive, all_packs,
                 {"Midtown Madness 3 DLC/content/uPAL/default.xbe": b"console-update",
                  "Midtown Madness 3 DLC/default.xbe": b"console-installer"})
        edited = self.content / PACK / "Cars/Tweak/car213.txt"
        edited.parent.mkdir(parents=True)
        edited.write_bytes(b"my tuning")
        save = self.game / "UDATA/profile.sav"
        save.parent.mkdir()
        save.write_bytes(b"save")
        (self.game / "default.xbe").write_bytes(b"native-compatible game")
        setup.install_dlc(self.archive, self.game, lambda _: None)
        self.assertEqual(setup.installed_dlc_packs(self.game), list(setup.DLC_PACKS))
        self.assertEqual(edited.read_bytes(), b"my tuning")
        self.assertEqual(save.read_bytes(), b"save")
        self.assertEqual((self.game / "default.xbe").read_bytes(), b"native-compatible game")
        self.assertEqual(list(self.game.rglob("*.xbe")), [self.game / "default.xbe"])
        before = {p.relative_to(self.game): p.read_bytes() for p in self.game.rglob("*") if p.is_file()}
        setup.install_dlc(self.archive, self.game, lambda _: None)
        after = {p.relative_to(self.game): p.read_bytes() for p in self.game.rglob("*") if p.is_file()}
        self.assertEqual(before, after)
        self.assertFalse(list(self.content.glob(".mm3-dlc-*")))

    def test_accepts_extracted_lowercase_content_directories(self):
        folder = self.root / "extracted"
        for name, data in pack_files().items():
            target = folder / PACK / name.replace("Cars/", "cars/")
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        setup.install_dlc(folder, self.game, lambda _: None)
        # The native target is Windows, where Cars and cars name the same folder.
        self.assertTrue((self.content / PACK / "cars/Tweak/car213.txt").is_file())
        self.assertEqual((self.content / PACK / "contentmeta.xbx").read_bytes(),
                         setup.native_dlc_metadata(metadata(), PACK))

    def test_does_not_count_unsigned_or_incomplete_content_as_installed(self):
        folder = self.content / PACK
        for name, data in pack_files().items():
            target = folder / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        self.assertEqual(setup.installed_dlc_packs(self.game), [])
        (folder / "contentmeta.xbx").write_bytes(setup.native_dlc_metadata(metadata(), PACK))
        self.assertEqual(setup.installed_dlc_packs(self.game), [PACK])
        (folder / "Cars/car214/body.cmp").unlink()
        self.assertEqual(setup.installed_dlc_packs(self.game), [])

    def test_reimport_repairs_empty_files_and_keeps_nonempty_edits(self):
        make_zip(self.archive)
        body = self.content / PACK / "Cars/car214/body.cmp"
        body.parent.mkdir(parents=True)
        body.write_bytes(b"")
        setup.install_dlc(self.archive, self.game, lambda _: None)
        self.assertEqual(body.read_bytes(), b"asset")
        self.assertEqual(setup.installed_dlc_packs(self.game), [PACK])

    def test_rejects_incomplete_wrong_title_traversal_and_duplicate_packs_before_import(self):
        bad_sets = []
        incomplete = pack_files()
        del incomplete["Cars/car215/body.cmp"]
        bad_sets.append((incomplete, {}))
        wrong_title = pack_files()
        wrong_title["contentmeta.xbx"] = bytes(108)
        bad_sets.append((wrong_title, {}))
        bad_sets.append((pack_files(), {f"{PACK}/../../escaped.txt": b"bad"}))
        bad_sets.append((pack_files(), {f"other/{PACK}/Cars/Tweak/car213.txt": b"duplicate"}))
        bad_sets.append((pack_files(), {f"{PACK}/default.xbe": b"update"}))
        for files, extra in bad_sets:
            with self.subTest(extra=extra):
                make_zip(self.archive, {PACK: files}, extra)
                with self.assertRaises(setup.InstallError):
                    setup.install_dlc(self.archive, self.game, lambda _: None)
                self.assertFalse(self.content.exists())
                self.assertFalse((self.root / "escaped.txt").exists())

    def test_bad_crc_cannot_partially_replace_existing_content(self):
        files = pack_files()
        files["contentimage.xbx"] = b"corrupt-me"
        make_zip(self.archive, {PACK: files})
        self.archive.write_bytes(self.archive.read_bytes().replace(b"corrupt-me", b"broken----", 1))
        existing = self.content / PACK / "contentmeta.xbx"
        existing.parent.mkdir(parents=True)
        existing.write_bytes(b"keep original")
        with self.assertRaises(setup.InstallError):
            setup.install_dlc(self.archive, self.game, lambda _: None)
        self.assertEqual(existing.read_bytes(), b"keep original")
        self.assertEqual([p for p in self.content.rglob("*") if p.is_file()], [existing])


class DiscReuseTest(unittest.TestCase):
    def test_installs_game_with_bundled_visual_cpp_runtime(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            payload, target = root / "payload", root / "install"
            payload.mkdir()
            target.mkdir()
            (payload / setup.GAME_EXE).write_bytes(b"current native game")
            (payload / "vcruntime140.dll").write_bytes(b"redistributable runtime")
            (target / setup.GAME_EXE).write_bytes(b"old game")
            with patch.object(setup, "PAYLOAD", payload):
                setup.install_game_exe(target, lambda _: None)
            self.assertEqual((target / setup.GAME_EXE).read_bytes(), b"current native game")
            self.assertEqual((target / "vcruntime140.dll").read_bytes(), b"redistributable runtime")
            self.assertFalse(list(target.glob("*.new")))

    def test_every_disc_file_must_exist_but_modified_archives_are_allowed(self):
        class Iso:
            def __init__(self, path):
                pass

            def __enter__(self):
                return self

            def __exit__(self, *args):
                pass

            def walk(self):
                return [("", SimpleNamespace(name="default.xbe", size=10)),
                        ("Data", SimpleNamespace(name="Data_hd.zip", size=100)),
                        ("media", SimpleNamespace(name="movie.wmv", size=10))]

        ed = SimpleNamespace(CODE_SHA256="expected", code_sha256=lambda _: "expected", Xiso=Iso)
        with tempfile.TemporaryDirectory() as scratch, patch.object(setup, "extract_disc", return_value=ed):
            game = Path(scratch)
            (game / "Data").mkdir()
            (game / "default.xbe").write_bytes(b"valid xbe")
            (game / "Data/Data_hd.zip").write_bytes(b"modified car data")
            self.assertFalse(setup.game_files_ready(game, Path("disc.iso")))
            (game / "media").mkdir()
            (game / "media/movie.wmv").write_bytes(b"video")
            self.assertTrue(setup.game_files_ready(game, Path("disc.iso")))
            (game / "media/movie.wmv").write_bytes(b"")
            self.assertFalse(setup.game_files_ready(game, Path("disc.iso")))

    def test_image_filenames_cannot_escape_destination(self):
        with tempfile.TemporaryDirectory() as scratch:
            for directory, name in (("../outside", "file"), ("Data", "/outside"),
                                    ("Data", "name:stream"), ("Data", "trailing.")):
                with self.subTest(name=name), self.assertRaises(setup.InstallError):
                    setup.safe_target(Path(scratch), directory, name)


if __name__ == "__main__":
    unittest.main()
