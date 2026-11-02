"""Setup's first-run experience: finding and judging the disc image, judging the folder, resuming a
stopped extraction, shortcuts, uninstall and problem reports. No disc, no game and no network."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import threading
import time
from types import SimpleNamespace
import unittest
from unittest.mock import patch
from urllib.parse import parse_qs, urlparse


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("mm3_installer_experience", ROOT / "installer/installer.py")
setup = importlib.util.module_from_spec(spec)
spec.loader.exec_module(setup)


def xbe(title_id, base=0x10000):
    """The start of an Xbox executable: header magic, base address, certificate pointer, certificate."""
    data = bytearray(0x400)
    data[:4] = b"XBEH"
    struct.pack_into("<I", data, 0x104, base)
    struct.pack_into("<I", data, 0x118, base + 0x200)
    struct.pack_into("<I", data, 0x200 + 8, title_id)
    return bytes(data)


class FakeDisc:
    """Stands in for scripts/extract_disc.py: a tiny image of (directory, name, sector, size) files."""

    def __init__(self, entries, title_id=setup.MM3_TITLE_ID, genuine=True, base=0, fail_on=None,
                 on_extract=None):
        self.entries, self.title_id, self.genuine, self.base = entries, title_id, genuine, base
        self.fail_on, self.on_extract, self.extracted = fail_on, on_extract, []
        outer = self
        self.CODE_SHA256 = "genuine"
        self.XisoError = type("XisoError", (Exception,), {})
        self.MEDIA_PATCH_BYTES = (0x7D, 0xEB)
        self.code_sha256 = lambda data: "genuine" if outer.genuine else "other"
        self.media_patch_offset = lambda data: 5

        class Iso:
            def __init__(self, path):
                self.base = outer.base

            def __enter__(self):
                return self

            def __exit__(self, *args):
                pass

            def walk(self):
                return [(directory, SimpleNamespace(name=name, sector=sector, size=size))
                        for directory, name, sector, size in outer.entries]

            def find(self, name):
                return next((entry for directory, entry in self.walk() if not directory and entry.name == name), None)

            def read(self, entry):
                return xbe(outer.title_id)

            def extract(self, entry, target):
                outer.extracted.append(entry.name)
                if outer.on_extract:
                    outer.on_extract(entry.name)
                if entry.name == outer.fail_on:
                    raise OSError("the image could not be read")
                Path(target).write_bytes(b"disc " + entry.name.encode())
        self.Xiso = Iso


GOOD = [("", "default.xbe", 100, 400), ("Data", "Data_hd.zip", 101, 4096), ("Data", "Data_dvd.zip", 103, 2048),
        ("media", "movie.wmv", 104, 10)]


class ImageInspectionTest(unittest.TestCase):
    def setUp(self):
        scratch = tempfile.TemporaryDirectory()
        self.addCleanup(scratch.cleanup)
        self.folder = Path(scratch.name)
        self.image = self.folder / "mm3.iso"
        self.image.write_bytes(bytes(2048 * 120))   # long enough to hold every file of GOOD

    def inspect(self, disc, path=None):
        with patch.object(setup, "extract_disc", return_value=disc):
            return setup.inspect_image(path or self.image)

    def test_title_id_comes_from_the_certificate(self):
        self.assertEqual(setup.xbe_title_id(xbe(0x4D53002A)), 0x4D53002A)
        self.assertEqual(setup.xbe_title_id(xbe(0x12345678, base=0x400000)), 0x12345678)
        for broken in (b"", b"MZ" + bytes(100), xbe(1)[:0x110], b"XBEH" + bytes(0x200)):
            self.assertIsNone(setup.xbe_title_id(broken), broken[:8])

    def test_the_right_disc_is_recognised_with_its_size(self):
        info = self.inspect(FakeDisc(GOOD))
        self.assertTrue(info.ok, info.message)
        self.assertEqual((info.total_bytes, info.files), (400 + 4096 + 2048 + 10, 4))
        self.assertIn("recognised", info.message)

    def test_wrong_things_are_named_in_plain_words(self):
        disc = FakeDisc(GOOD)
        self.assertIn("folder", self.inspect(disc, self.folder).message)
        self.assertIn("not there", self.inspect(disc, self.folder / "missing.iso").message)
        archive = self.folder / "mm3.zip"
        archive.write_bytes(b"PK")
        self.assertIn("archive", self.inspect(disc, archive).message)
        game = self.inspect(FakeDisc(GOOD, title_id=0x4D530001, genuine=False))
        self.assertFalse(game.ok)
        self.assertIn("different Xbox game", game.message)
        self.assertIn("4D530001", game.message)
        release = self.inspect(FakeDisc(GOOD, genuine=False))
        self.assertFalse(release.ok)
        self.assertIn("another release", release.message)
        self.assertNotIn("different Xbox game", release.message)
        no_xbe = self.inspect(FakeDisc([("Data", "Data_hd.zip", 101, 5)]))
        self.assertIn("default.xbe", no_xbe.message)

    def test_an_image_that_stops_early_is_reported_before_extracting(self):
        # The last file ends at sector 104 + 10 bytes = 213,000 bytes; the image has 131,072.
        short = self.folder / "short.iso"
        short.write_bytes(bytes(2048 * 60))
        info = self.inspect(FakeDisc(GOOD), short)
        self.assertFalse(info.ok)
        self.assertIn("incomplete", info.message)
        # A partition that starts later (a full-disc dump) counts its offset.
        self.assertIn("incomplete", self.inspect(FakeDisc(GOOD, base=2048 * 30)).message)
        self.assertTrue(self.inspect(FakeDisc(GOOD, base=2048 * 5)).ok)

    def test_a_tiny_or_unreadable_file_is_not_a_disc(self):
        disc = FakeDisc(GOOD)

        def unreadable(path):
            raise disc.XisoError("no descriptor")
        disc.Xiso = unreadable
        small = self.inspect(disc)
        self.assertFalse(small.ok)
        self.assertIn("too small", small.message)

    def test_check_iso_keeps_its_old_shape(self):
        with patch.object(setup, "extract_disc", return_value=FakeDisc(GOOD)):
            ok, message = setup.check_iso(self.image)
        self.assertEqual((ok, type(message)), (True, str))


class FindImageTest(unittest.TestCase):
    def setUp(self):
        scratch = tempfile.TemporaryDirectory()
        self.addCleanup(scratch.cleanup)
        self.root = Path(scratch.name)

    def big(self, path, age=0):
        path.parent.mkdir(parents=True, exist_ok=True)
        with open(path, "wb") as handle:
            handle.truncate(1_000_000_000)
        os.utime(path, (path.stat().st_atime, path.stat().st_mtime - age))
        return path

    def test_lists_big_images_in_the_folders_and_their_subfolders_likely_names_first(self):
        other = self.big(self.root / "backup.iso")
        old_mm3 = self.big(self.root / "Midtown Madness 3.iso", age=5000)
        nested = self.big(self.root / "Games" / "xbox" / "deeper" / "mm3.iso")      # two levels: not looked at
        sub = self.big(self.root / "Games" / "mm3.xiso")
        (self.root / "small.iso").write_bytes(b"tiny")
        (self.root / "notes.txt").write_bytes(bytes(2000))
        found = setup.image_candidates([self.root])
        self.assertEqual(found, [sub, old_mm3, other])    # likely names first, newest first within them
        self.assertNotIn(nested, found)
        self.assertEqual(setup.image_candidates([self.root / "nowhere"]), [])

    def test_the_first_image_that_really_is_the_game_wins(self):
        first, second = self.big(self.root / "midtown.iso"), self.big(self.root / "other.iso")
        verdicts = {first: setup.ImageInfo(False, "different game"), second: setup.ImageInfo(True, "ok")}
        with patch.object(setup, "inspect_image", side_effect=lambda path: verdicts[Path(path)]):
            self.assertEqual(setup.find_game_image([self.root]), second)
        with patch.object(setup, "inspect_image", return_value=setup.ImageInfo(False, "no")):
            self.assertIsNone(setup.find_game_image([self.root]))

    def test_known_folders_always_give_a_path(self):
        for name in ("Downloads", "Desktop", "Documents"):
            self.assertTrue(isinstance(setup.known_folder(name), Path) and str(setup.known_folder(name)))


class DestinationTest(unittest.TestCase):
    def check(self, text, needed=0, free=500_000_000_000):
        with patch.object(setup, "free_bytes", return_value=free):
            return setup.check_destination(text, needed)

    def test_folders_that_cannot_work_are_refused_with_a_reason(self):
        for text, word in (("", "Choose a folder"), ("D:\\", "not the whole drive"),
                           ("\\\\server\\share\\MM3", "Network folders")):
            ok, message = self.check(text)
            self.assertFalse(ok, text)
            self.assertIn(word, message)
        with patch.dict(os.environ, {"ProgramFiles": "C:\\Program Files"}):
            for text in ("C:\\Program Files", "C:\\Program Files\\Midtown Madness 3"):
                ok, message = self.check(text)
                self.assertFalse(ok, text)
                self.assertIn("read-only", message)
            self.assertTrue(self.check("C:\\Program Files Extra\\MM3")[0])
        with tempfile.NamedTemporaryFile() as handle:
            self.assertEqual(self.check(handle.name)[1], "That is a file, not a folder.")

    def test_space_is_checked_against_the_drive_and_said_in_gigabytes(self):
        ok, message = self.check("D:\\Games\\MM3", needed=5_000_000_000, free=100_000_000_000)
        self.assertTrue(ok)
        self.assertIn("100 GB free", message)
        self.assertIn("5.0 GB", message)
        ok, message = self.check("D:\\Games\\MM3", needed=5_000_000_000, free=2_000_000_000)
        self.assertFalse(ok)
        self.assertIn("5.0 GB needed, 2.0 GB free", message)

    def test_onedrive_is_allowed_with_a_warning(self):
        ok, message = self.check("C:\\Users\\Me\\OneDrive\\Games\\MM3")
        self.assertTrue(ok)
        self.assertIn("OneDrive", message)

    def test_space_needed_counts_the_disc_only_when_game_files_is_not_there(self):
        with tempfile.TemporaryDirectory() as scratch:
            dest = Path(scratch)
            self.assertEqual(setup.space_needed(4_000_000_000, False, dest), 4_000_000_000 + setup.BASE_EXTRA_BYTES)
            self.assertEqual(setup.space_needed(4_000_000_000, True, dest), 4_000_000_000 + setup.EXTRA_CARS_BYTES)
            (dest / "game_files").mkdir()
            self.assertEqual(setup.space_needed(4_000_000_000, False, dest), setup.BASE_EXTRA_BYTES)

    def test_install_refuses_a_folder_that_cannot_work_before_touching_anything(self):
        with patch.object(setup, "check_iso", return_value=(True, "")), \
                self.assertRaisesRegex(setup.InstallError, "whole drive"):
            setup.install("disc.iso", Path(Path.cwd().anchor), False, True, lambda _: None, lambda *_: None)


class ResumeTest(unittest.TestCase):
    """A stopped extraction is continued file by file, from the same image only."""

    def setUp(self):
        scratch = tempfile.TemporaryDirectory()
        self.addCleanup(scratch.cleanup)
        self.root = Path(scratch.name)
        self.image = self.root / "mm3.iso"
        self.image.write_bytes(bytes(2048 * 120))
        self.dest = self.root / "install"
        self.dest.mkdir()

    def extract(self, disc, cancel=None, lines=None):
        with patch.object(setup, "extract_disc", return_value=disc):
            setup.extract_game(self.image, self.dest, False, (lines.append if lines is not None else lambda _: None),
                               lambda *_: None, cancel)

    def test_cancel_keeps_the_finished_files_and_the_next_run_does_only_the_rest(self):
        cancel = threading.Event()
        first = FakeDisc(GOOD, on_extract=lambda name: cancel.set() if name == "Data_hd.zip" else None)
        with self.assertRaises(setup.InstallCancelled) as raised:
            self.extract(first, cancel)
        self.assertIn("continue where it left off", str(raised.exception))
        partial = self.dest / "game_files.partial"
        self.assertTrue((partial / "default.xbe").is_file())
        self.assertTrue((partial / "Data/Data_hd.zip").is_file())      # the file in progress finished
        self.assertFalse((self.dest / "game_files").exists())
        second, lines = FakeDisc(GOOD), []
        self.extract(second, lines=lines)
        self.assertEqual(second.extracted, ["Data_dvd.zip", "movie.wmv"])
        self.assertTrue(any("Continuing" in line for line in lines))
        game = self.dest / "game_files"
        self.assertEqual(sorted(p.relative_to(game).as_posix() for p in game.rglob("*") if p.is_file()),
                         ["Data/Data_dvd.zip", "Data/Data_hd.zip", "default.xbe", "media/movie.wmv"])
        self.assertFalse(partial.exists())
        self.assertEqual(list(game.rglob("*.part")), [])
        self.assertFalse((game / ".mm3-setup-image").exists())

    def test_a_crash_in_the_middle_of_a_file_leaves_no_half_file_and_is_continued(self):
        crashed = FakeDisc(GOOD, fail_on="Data_hd.zip")
        with self.assertRaises(OSError):
            self.extract(crashed)
        partial = self.dest / "game_files.partial"
        self.assertFalse((partial / "Data/Data_hd.zip").exists())      # only the .part file existed
        resumed = FakeDisc(GOOD)
        self.extract(resumed)
        self.assertEqual(resumed.extracted, ["Data_hd.zip", "Data_dvd.zip", "movie.wmv"])
        self.assertEqual((self.dest / "game_files/Data/Data_hd.zip").read_bytes(), b"disc Data_hd.zip")

    def test_another_image_starts_from_scratch(self):
        cancel = threading.Event()
        cancel.set()
        with self.assertRaises(setup.InstallCancelled):
            self.extract(FakeDisc(GOOD), cancel)
        (self.dest / "game_files.partial" / "stale.bin").write_bytes(b"from the other image")
        self.image.write_bytes(bytes(2048 * 121))        # a different file now
        fresh = FakeDisc(GOOD)
        self.extract(fresh)
        self.assertEqual(fresh.extracted, ["Data_hd.zip", "Data_dvd.zip", "movie.wmv"])
        self.assertFalse((self.dest / "game_files/stale.bin").exists())

    def test_a_partial_folder_without_our_marker_is_never_trusted(self):
        partial = self.dest / "game_files.partial"
        (partial / "Data").mkdir(parents=True)
        (partial / "Data/Data_hd.zip").write_bytes(b"older Setup, unknown state")
        disc = FakeDisc(GOOD)
        self.extract(disc)
        self.assertEqual((self.dest / "game_files/Data/Data_hd.zip").read_bytes(), b"disc Data_hd.zip")


class ShortcutTest(unittest.TestCase):
    def setUp(self):
        scratch = tempfile.TemporaryDirectory()
        self.addCleanup(scratch.cleanup)
        self.root = Path(scratch.name)

    @staticmethod
    def read_target(link):
        script = "(New-Object -ComObject WScript.Shell).CreateShortcut($env:LNK).TargetPath"
        done = subprocess.run(["powershell", "-NoProfile", "-Command", script], env=dict(os.environ, LNK=str(link)),
                              capture_output=True, text=True, timeout=60)
        return done.stdout.strip()

    def test_a_real_shortcut_points_at_the_start_script_even_with_quotes_in_the_folder_name(self):
        dest = self.root / "O'Brien's & $HOME (MM3)"
        dest.mkdir()
        (dest / setup.LAUNCHER).write_text("@echo off\n")
        folders = {"Desktop": self.root / "Desk top", "Start menu": self.root / "Programs"}
        lines = []
        made = setup.create_shortcuts(dest, lines.append, folders)
        self.assertEqual(sorted(path.parent.name for path in made), ["Desk top", "Programs"], lines)
        for link in made:
            self.assertEqual(link.name, setup.SHORTCUT_NAME)
            self.assertEqual(self.read_target(link), str(dest / setup.LAUNCHER))
        self.assertEqual(len([line for line in lines if line.startswith("Shortcut added")]), 2)

    def test_a_failure_is_a_message_and_never_an_error(self):
        lines = []
        with patch.object(setup.subprocess, "run", side_effect=FileNotFoundError("no powershell")):
            made = setup.create_shortcuts(self.root, lines.append, {"Desktop": self.root / "d"})
        self.assertEqual(made, [])
        self.assertIn("Could not add the Desktop shortcut", lines[0])
        self.assertIn(setup.LAUNCHER, lines[0])

    def test_only_our_recorded_shortcuts_are_removed(self):
        ours, other = self.root / setup.SHORTCUT_NAME, self.root / "Another game.lnk"
        ours.write_bytes(b"lnk")
        other.write_bytes(b"lnk")
        self.assertEqual(setup.remove_shortcuts([str(ours), str(other), str(self.root / "gone.lnk")]), [ours])
        self.assertFalse(ours.exists())
        self.assertTrue(other.exists())
        self.assertEqual(setup.remove_shortcuts(None), [])
        self.assertEqual(setup.remove_shortcuts("not a list"), [])

    def test_install_records_the_shortcuts_and_a_later_install_without_them_removes_them(self):
        dest = self.root / "install"
        link = self.root / setup.SHORTCUT_NAME
        link.write_bytes(b"lnk")

        def run(shortcuts):
            with patch.object(setup, "check_iso", return_value=(True, "")), \
                    patch.object(setup, "game_files_ready", return_value=True), \
                    patch.object(setup, "install_game_exe"), patch.object(setup, "report_dlc", return_value=[]), \
                    patch.object(setup, "apply_cars"), \
                    patch.object(setup, "create_shortcuts", return_value=[link]) as create:
                setup.install("disc.iso", dest, False, True, lambda _: None, lambda *_: None, shortcuts=shortcuts)
            return create

        self.assertEqual(run(False).call_count, 0)
        self.assertEqual(setup.saved_options(dest)["shortcuts"], [])
        self.assertEqual(run(True).call_count, 1)
        self.assertEqual(setup.saved_options(dest)["shortcuts"], [str(link)])
        run(False)
        self.assertFalse(link.exists())
        self.assertEqual(setup.saved_options(dest)["shortcuts"], [])


class UninstallTest(unittest.TestCase):
    def setUp(self):
        scratch = tempfile.TemporaryDirectory()
        self.addCleanup(scratch.cleanup)
        self.dest = Path(scratch.name) / "MM3"
        game = self.dest / "game_files"
        for name, data in (("default.xbe", b"xbe"), ("UDATA/save.bin", b"my save"), ("TDATA/dlc.bin", b"dlc")):
            (game / name).parent.mkdir(parents=True, exist_ok=True)
            (game / name).write_bytes(data)
        for name in (setup.GAME_EXE, "vcruntime140.dll", setup.LAUNCHER, "Extra cars - credits.txt"):
            (self.dest / name).write_bytes(b"x")
        (self.dest / "updates").mkdir()
        (self.dest / "updates/MM3-Setup.exe").write_bytes(b"MZ")
        (self.dest / "logs").mkdir()
        (self.dest / "logs/last-run.log").write_text("log")
        self.link = Path(scratch.name) / setup.SHORTCUT_NAME
        self.link.write_bytes(b"lnk")
        (self.dest / "setup-options.json").write_text(json.dumps({"shortcuts": [str(self.link)]}))
        patcher = patch.object(setup, "game_running", return_value=False)
        patcher.start()
        self.addCleanup(patcher.stop)

    def files(self):
        return sorted(p.relative_to(self.dest).as_posix() for p in self.dest.rglob("*") if p.is_file())

    def test_removes_what_setup_made_and_keeps_the_games_files_and_saves(self):
        lines = []
        removed = setup.uninstall(self.dest, lines.append)
        self.assertEqual(self.files(), ["game_files/TDATA/dlc.bin", "game_files/UDATA/save.bin",
                                        "game_files/default.xbe", "logs/last-run.log"])
        self.assertFalse(self.link.exists())
        self.assertTrue(any("shortcut" in item for item in removed))
        self.assertIn(setup.GAME_EXE, removed)
        self.assertTrue(lines and lines[0].startswith("Removed:"))
        # Once it is gone the folder is no longer "an install by Setup": a second run refuses.
        with self.assertRaises(setup.InstallError):
            setup.uninstall(self.dest, lambda _: None)

    def test_deleting_the_game_files_is_a_separate_explicit_choice(self):
        setup.uninstall(self.dest, lambda _: None, delete_game_files=True)
        self.assertFalse((self.dest / "game_files").exists())
        self.assertEqual(self.files(), ["logs/last-run.log"])

    def test_a_folder_setup_did_not_make_is_refused_untouched(self):
        (self.dest / "setup-options.json").unlink()
        before = self.files()
        with self.assertRaisesRegex(setup.InstallError, "not installed by Setup"):
            setup.uninstall(self.dest, lambda _: None, delete_game_files=True)
        self.assertEqual(self.files(), before)

    def test_a_running_game_stops_it(self):
        before = self.files()
        with patch.object(setup, "game_running", return_value=True), self.assertRaisesRegex(setup.InstallError, "running"):
            setup.uninstall(self.dest, lambda _: None)
        self.assertEqual(self.files(), before)

    def test_a_junction_to_the_game_files_is_never_followed(self):
        target = self.dest.parent / "real_game_files"
        (self.dest / "game_files").rename(target)
        done = subprocess.run(["cmd", "/c", "mklink", "/J", str(self.dest / "game_files"), str(target)],
                              capture_output=True, text=True)
        if done.returncode != 0:
            self.skipTest("cannot create a junction here: " + done.stderr.strip())
        before = self.files()
        with self.assertRaisesRegex(setup.InstallError, "link"):
            setup.uninstall(self.dest, lambda _: None, delete_game_files=True)
        self.assertEqual(self.files(), before)             # not even the exe was removed
        self.assertEqual((target / "UDATA/save.bin").read_bytes(), b"my save")

    def test_the_command_line_reports_success_and_refusal(self):
        self.assertEqual(setup.main(["--uninstall", "--dest", str(self.dest)]), 0)
        self.assertEqual(setup.main(["--uninstall", "--dest", str(self.dest)]), 1)


class ProblemReportTest(unittest.TestCase):
    def test_details_hide_the_user_name_and_keep_only_the_last_lines(self):
        home = str(Path.home())
        lines = [f"line {number} in {home}\\Documents" for number in range(100)]
        text = setup.problem_details(f"Cannot write {home}\\MM3", lines, folder=f"{home}\\MM3", extra_cars=True)
        self.assertNotIn(home, text)
        self.assertIn("%USERPROFILE%\\MM3", text)
        self.assertIn("line 99", text)
        self.assertNotIn("line 59 ", text)
        self.assertIn(f"Midtown Madness 3 Setup {setup.VERSION}", text)
        self.assertIn("extra_cars=True", text)

    def test_the_report_link_opens_a_prefilled_issue_of_this_project(self):
        details = setup.problem_details("Not enough disk space: about 5.0 GB needed", ["a", "b"])
        url = urlparse(setup.report_url(details))
        self.assertEqual(f"{url.scheme}://{url.netloc}{url.path}", setup.ISSUES_URL)
        query = parse_qs(url.query)
        self.assertEqual(query["title"], ["Setup problem: Not enough disk space: about 5.0 GB needed"])
        self.assertIn("Not enough disk space", query["body"][0])
        self.assertLess(len(url.geturl()), 8000)
        long = setup.report_url("Problem:\n" + "x" * 50000)
        self.assertLess(len(long), 8000)

    def test_the_session_log_is_written_for_support_and_starts_fresh_each_install(self):
        with tempfile.TemporaryDirectory() as scratch:
            session = setup.SessionLog(Path(scratch) / "log.txt")
            session.write("Extracting 4 files")
            self.assertIn("Extracting 4 files", (Path(scratch) / "log.txt").read_text(encoding="utf-8"))
            self.assertIn(f"Setup {setup.VERSION}", session.lines[0])
            session.clear()
            self.assertEqual((Path(scratch) / "log.txt").read_text(encoding="utf-8"), "")
            self.assertEqual(len(session.lines), 0)


class UpdateInstallTest(unittest.TestCase):
    """"Update this install": reuse the extracted game files and the DLC, refresh only what Setup writes,
    and ask for the image only when something essential is gone."""

    def setUp(self):
        scratch = tempfile.TemporaryDirectory()
        self.addCleanup(scratch.cleanup)
        self.root = Path(scratch.name)
        self.dest = self.root / "MM3"
        self.payload = self.root / "payload"
        self.payload.mkdir()
        (self.payload / setup.GAME_EXE).write_bytes(b"new native game")
        (self.payload / "vcruntime140.dll").write_bytes(b"new runtime")
        (self.payload / "mm3_recomp.pdb").write_bytes(b"new symbols")
        self.game = self.dest / "game_files"
        files = {"default.xbe": xbe(setup.MM3_TITLE_ID), "Data/Data_hd.zip": b"cars modified by the installer",
                 "Data/Data_dvd.zip": b"menus", "media/movie.wmv": b"video", "media/empty.bin": b""}
        for name, data in files.items():
            (self.game / name).parent.mkdir(parents=True, exist_ok=True)
            (self.game / name).write_bytes(data)
        self.sizes = {"default.xbe": 400, "Data/Data_hd.zip": 4096, "Data/Data_dvd.zip": 2048,
                      "media/movie.wmv": 10, "media/empty.bin": 0}
        (self.game / "UDATA").mkdir()
        (self.game / "UDATA/save.bin").write_bytes(b"my save")
        (self.game / "TDATA/4d53002a/$C/4d53002a00000005").mkdir(parents=True)
        (self.game / "TDATA/4d53002a/$C/4d53002a00000005/contentmeta.xbx").write_bytes(b"dlc")
        (self.dest / setup.GAME_EXE).write_bytes(b"old native game")
        (self.dest / setup.DISC_FILES).write_text(json.dumps({"files": self.sizes}))
        self.write_options()
        self.disc = FakeDisc(GOOD)
        for patcher in (patch.object(setup, "extract_disc", return_value=self.disc),
                        patch.object(setup, "PAYLOAD", self.payload),
                        patch.object(setup, "game_running", return_value=False),
                        patch.object(setup, "check_iso", side_effect=AssertionError("no image should be read")),
                        patch.object(setup, "extract_game", side_effect=AssertionError("nothing is extracted")),
                        patch.object(setup, "restore_missing_files", side_effect=AssertionError("nothing is restored"))):
            patcher.start()
            self.addCleanup(patcher.stop)
        self.cars = patch.object(setup, "apply_cars")
        self.apply_cars = self.cars.start()
        self.addCleanup(self.cars.stop)

    def write_options(self, **changes):
        options = {"setup_version": "2.1", "extra_cars": True, "game_improvements": False, "dlc_packs": [],
                   "shortcuts": []}
        options.update(changes)
        (self.dest / "setup-options.json").write_text(json.dumps(options))

    def update(self, **keywords):
        lines = []
        arguments = dict(extra_cars=True, improvements=False)
        arguments.update(keywords)
        setup.install(None, self.dest, arguments["extra_cars"], arguments["improvements"], lines.append,
                      lambda *_: None)
        return lines

    def untouched(self):
        return {name: (self.game / name).read_bytes() for name in self.sizes} | {
            "UDATA/save.bin": (self.game / "UDATA/save.bin").read_bytes(),
            "dlc": (self.game / "TDATA/4d53002a/$C/4d53002a00000005/contentmeta.xbx").read_bytes()}

    # --- the check

    def test_a_healthy_install_can_be_updated_without_the_image(self):
        check = setup.check_existing_install(self.dest)
        self.assertTrue(check.ok, check.message)
        self.assertEqual(check.missing, ())

    def test_only_an_install_made_by_setup_is_updated(self):
        (self.dest / "setup-options.json").unlink()
        check = setup.check_existing_install(self.dest)
        self.assertFalse(check.ok)
        self.assertIn("No install made by Setup", check.message)
        before = (self.dest / setup.GAME_EXE).read_bytes()
        with self.assertRaisesRegex(setup.InstallError, "No install made by Setup"):
            self.update()
        self.assertEqual((self.dest / setup.GAME_EXE).read_bytes(), before)    # the developer's exe stays

    def test_the_program_must_be_the_retail_one(self):
        self.disc.genuine = False
        check = setup.check_existing_install(self.dest)
        self.assertFalse(check.ok)
        self.assertIn("not the retail Midtown Madness 3 program", check.message)
        self.assertIn("Choose your game image", check.message)
        (self.game / "default.xbe").write_bytes(xbe(0x4D530001))
        self.assertIn("another game's program", setup.check_existing_install(self.dest).message)
        (self.game / "default.xbe").unlink()
        self.assertIn("default.xbe is missing", setup.check_existing_install(self.dest).message)

    def test_lost_or_emptied_disc_files_are_named_and_need_the_image(self):
        (self.game / "media/movie.wmv").unlink()
        (self.game / "Data/Data_dvd.zip").write_bytes(b"")           # emptied: the disc's file had 2048 bytes
        (self.game / "media/empty.bin").write_bytes(b"")             # the disc's own empty file is fine
        check = setup.check_existing_install(self.dest)
        self.assertFalse(check.ok)
        self.assertEqual(sorted(check.missing), ["Data/Data_dvd.zip", "media/movie.wmv"])
        self.assertIn("2 game files are missing or empty", check.message)
        self.assertIn("Choose your game image", check.message)
        self.assertIn("saved games stay", check.message)

    def test_an_older_install_without_the_file_list_is_judged_by_the_program_and_main_archive(self):
        (self.dest / setup.DISC_FILES).unlink()
        (self.game / "media/movie.wmv").unlink()          # not recorded, so not noticed
        self.assertTrue(setup.check_existing_install(self.dest).ok)
        (self.game / "Data/Data_hd.zip").unlink()
        check = setup.check_existing_install(self.dest)
        self.assertFalse(check.ok)
        self.assertEqual(check.missing, ("Data/Data_hd.zip",))
        (self.dest / setup.DISC_FILES).write_text("not json")
        (self.game / "Data/Data_hd.zip").write_bytes(b"x")
        self.assertTrue(setup.check_existing_install(self.dest).ok)

    def test_a_list_naming_a_path_outside_game_files_is_ignored(self):
        self.sizes["../escape.bin"] = 5
        (self.dest / setup.DISC_FILES).write_text(json.dumps({"files": self.sizes}))
        self.assertTrue(setup.check_existing_install(self.dest).ok)

    # --- the update

    def test_update_refreshes_the_program_and_keeps_game_files_dlc_saves_and_mods(self):
        before = self.untouched()
        lines = self.update()
        self.assertEqual(self.untouched(), before)
        self.assertEqual((self.dest / setup.GAME_EXE).read_bytes(), b"new native game")
        self.assertEqual((self.dest / "vcruntime140.dll").read_bytes(), b"new runtime")
        self.assertEqual((self.dest / "mm3_recomp.pdb").read_bytes(), b"new symbols")
        self.assertEqual(list(self.dest.glob("*.new")), [])
        self.apply_cars.assert_called_once_with(self.game, True, unittest.mock.ANY)
        self.assertTrue((self.dest / setup.LAUNCHER).is_file())
        self.assertTrue(any("its game files and DLC stay as they are" in line for line in lines))
        options = setup.saved_options(self.dest)
        self.assertEqual((options["setup_version"], options["extra_cars"], options["game_improvements"]),
                         (setup.VERSION, True, False))
        self.assertEqual(options["exe_sha256"], hashlib.sha256(b"new native game").hexdigest())
        self.assertEqual(options["dlc_packs"], [])        # the incomplete test pack is not counted as installed
        self.assertTrue((self.dest / "Extra cars - credits.txt").is_file())

    def test_update_follows_the_choices_made_now(self):
        self.update(extra_cars=False, improvements=True)
        self.apply_cars.assert_called_once_with(self.game, False, unittest.mock.ANY)
        self.assertFalse((self.dest / "Extra cars - credits.txt").exists())
        self.assertIn("improvements", (self.dest / setup.LAUNCHER).read_text().lower())
        self.assertEqual(setup.saved_options(self.dest)["game_improvements"], True)

    def test_blank_image_means_update_in_every_spelling(self):
        for blank in (None, "", "   "):
            with self.subTest(blank=blank):
                setup.install(blank, self.dest, False, True, lambda _: None, lambda *_: None)

    def test_a_damaged_install_is_refused_before_anything_is_changed(self):
        (self.game / "Data/Data_hd.zip").unlink()
        with self.assertRaisesRegex(setup.InstallError, "Choose your game image"):
            self.update()
        self.assertEqual((self.dest / setup.GAME_EXE).read_bytes(), b"old native game")
        self.assertFalse((self.dest / "mm3_recomp.pdb").exists())
        self.apply_cars.assert_not_called()

    def test_the_program_setup_did_not_install_is_left_alone(self):
        """The developer's own build, copied over an install, keeps running after an update."""
        self.write_options(exe_sha256=hashlib.sha256(b"what Setup wrote earlier").hexdigest())
        lines = self.update()
        self.assertEqual((self.dest / setup.GAME_EXE).read_bytes(), b"old native game")
        self.assertFalse((self.dest / "mm3_recomp.pdb").exists())              # its symbols belong to the new one
        self.assertEqual((self.dest / "vcruntime140.dll").read_bytes(), b"new runtime")
        self.assertTrue(any(line.startswith(f"Kept {setup.GAME_EXE}") for line in lines))
        self.assertEqual(setup.saved_options(self.dest)["exe_sha256"],
                         hashlib.sha256(b"what Setup wrote earlier").hexdigest())
        # The program Setup did install is replaced, as is one of an install that recorded no hash.
        self.write_options(exe_sha256=hashlib.sha256(b"old native game").hexdigest())
        self.update()
        self.assertEqual((self.dest / setup.GAME_EXE).read_bytes(), b"new native game")
        (self.dest / setup.GAME_EXE).write_bytes(b"some build")
        self.write_options()
        self.update()
        self.assertEqual((self.dest / setup.GAME_EXE).read_bytes(), b"new native game")

    def test_installing_from_the_image_always_replaces_the_program(self):
        self.write_options(exe_sha256=hashlib.sha256(b"what Setup wrote earlier").hexdigest())
        with patch.object(setup, "check_iso", return_value=(True, "")), \
                patch.object(setup, "game_files_ready", return_value=True), \
                patch.object(setup, "record_disc_files"):
            setup.install("disc.iso", self.dest, True, False, lambda _: None, lambda *_: None)
        self.assertEqual((self.dest / setup.GAME_EXE).read_bytes(), b"new native game")

    def test_an_update_keeps_the_shortcuts_it_is_asked_for(self):
        link = self.root / setup.SHORTCUT_NAME
        link.write_bytes(b"lnk")
        self.write_options(shortcuts=[str(link)])
        with patch.object(setup, "create_shortcuts", return_value=[link]) as create:
            setup.install(None, self.dest, True, False, lambda _: None, lambda *_: None, shortcuts=True)
        self.assertEqual(create.call_count, 1)
        self.assertEqual(setup.saved_options(self.dest)["shortcuts"], [str(link)])

    def test_the_command_line_updates_with_what_the_install_had(self):
        self.write_options(extra_cars=True, game_improvements=False)
        self.assertEqual(setup.main(["--cli", "--update-game", "--dest", str(self.dest)]), 0)
        self.apply_cars.assert_called_once_with(self.game, True, unittest.mock.ANY)
        self.assertEqual(setup.saved_options(self.dest)["game_improvements"], False)
        self.assertEqual((self.dest / setup.GAME_EXE).read_bytes(), b"new native game")
        (self.game / "default.xbe").unlink()
        self.assertEqual(setup.main(["--update-game", "--dest", str(self.dest)]), 1)

    def test_a_full_install_records_the_disc_files_for_later_checks(self):
        (self.dest / setup.DISC_FILES).unlink()
        disc = FakeDisc(GOOD)
        with patch.object(setup, "extract_disc", return_value=disc), \
                patch.object(setup, "check_iso", return_value=(True, "")), \
                patch.object(setup, "game_files_ready", return_value=True):
            setup.install("disc.iso", self.dest, True, False, lambda _: None, lambda *_: None)
        recorded = json.loads((self.dest / setup.DISC_FILES).read_text())["files"]
        self.assertEqual(recorded, {"default.xbe": 400, "Data/Data_hd.zip": 4096, "Data/Data_dvd.zip": 2048,
                                    "media/movie.wmv": 10})

    def test_uninstall_removes_the_new_files_too(self):
        self.update()
        removed = setup.uninstall(self.dest, lambda _: None)
        for name in (setup.DISC_FILES, "mm3_recomp.pdb", setup.GAME_EXE):
            self.assertFalse((self.dest / name).exists(), name)
            self.assertIn(name, removed)
        self.assertTrue((self.game / "UDATA/save.bin").is_file())


class WindowTest(unittest.TestCase):
    """The window offers Update this install over a healthy install, and searches for the image only
    when it is needed (or when asked)."""

    def run_window(self, dest, check=None, found=None, press=None, wait=1.2):
        import tkinter as tk
        calls = {"search": 0}
        seen = {}

        def search(*_args, **_kwargs):
            calls["search"] += 1
            return found

        def widgets(root):
            for child in root.winfo_children():
                yield child
                yield from widgets(child)

        def by_text(root, text):
            return next((w for w in widgets(root) if w.winfo_class() == "TButton" and w.cget("text") == text), None)

        def mainloop(root, n=0):
            deadline = time.time() + wait
            pressed = False
            while time.time() < deadline:
                root.update()
                time.sleep(0.02)
                if press and not pressed and time.time() > deadline - wait / 2:
                    pressed = True
                    by_text(root, press).invoke()
            root.update()
            seen["update"] = by_text(root, "Update this install")
            seen["install"] = by_text(root, "Install from image") or by_text(root, "Install") \
                or by_text(root, "Install again")
            seen["find"] = by_text(root, "Find on this PC")
            seen["labels"] = " | ".join(str(w.cget("text")) for w in widgets(root) if w.winfo_class() == "Label")
            seen["update_packed"] = bool(seen["update"] and seen["update"].winfo_manager())
            seen["update_enabled"] = bool(seen["update"]) and "disabled" not in seen["update"].state()
            seen["install_text"] = seen["install"].cget("text") if seen["install"] else ""
            seen["install_enabled"] = bool(seen["install"]) and "disabled" not in seen["install"].state()
            if seen["update_packed"]:
                seen["update_right_of_install"] = seen["update"].winfo_rootx() > seen["install"].winfo_rootx()
            seen["iso"] = next((w.get() for w in widgets(root) if w.winfo_class() == "TEntry"), "")
            for job in root.tk.call("after", "info"):      # the window's timers, so none fires after it is gone
                root.after_cancel(job)
            root.destroy()

        with patch.object(setup, "find_game_image", search), \
                patch.object(setup, "inspect_image", return_value=setup.ImageInfo(True, "ok", 1_000_000, 3)), \
                patch.object(setup, "check_existing_install", check or (lambda folder: setup.InstallCheck(False, "x"))), \
                patch.object(setup, "SessionLog", lambda *a, **k: SimpleNamespace(
                    write=lambda *_: None, lines=[], clear=lambda: None, path=Path("log"))), \
                patch.object(tk.Tk, "mainloop", mainloop):
            try:
                setup.run_gui(Path(dest))
            except tk.TclError as error:
                self.skipTest(f"no display for tkinter: {error}")
        return calls, seen

    def fake_install(self, root, with_options=True):
        folder = Path(root) / "MM3"
        (folder / "game_files").mkdir(parents=True)
        if with_options:
            (folder / "setup-options.json").write_text(json.dumps(
                {"setup_version": "2.1", "extra_cars": True, "game_improvements": True, "shortcuts": []}))
        return folder

    def test_a_healthy_install_offers_the_update_and_does_not_search_for_the_image(self):
        with tempfile.TemporaryDirectory() as scratch:
            folder = self.fake_install(scratch)
            calls, seen = self.run_window(folder, check=lambda f: setup.InstallCheck(True, "The game files here are complete."))
        self.assertEqual(calls["search"], 0)
        self.assertTrue(seen["update_packed"] and seen["update_enabled"])
        self.assertTrue(seen["update_right_of_install"])            # the main button is the rightmost
        self.assertEqual(seen["install_text"], "Install from image")
        self.assertFalse(seen["install_enabled"])                    # no image chosen
        self.assertIn("Not needed to update this install", seen["labels"])
        self.assertIn("Update this install refreshes", seen["labels"])

    def test_a_damaged_install_asks_for_the_image_and_looks_for_it(self):
        message = "2 game files are missing or empty (a, b). Choose your game image to put them back."
        with tempfile.TemporaryDirectory() as scratch:
            folder = self.fake_install(scratch)
            calls, seen = self.run_window(folder, check=lambda f: setup.InstallCheck(False, message, ("a", "b")),
                                          found=Path("C:/images/mm3.iso"))
        self.assertEqual(calls["search"], 1)
        self.assertFalse(seen["update_packed"])
        self.assertEqual(seen["install_text"], "Install again")
        self.assertIn(message, seen["labels"])
        self.assertTrue(seen["install_enabled"])                     # the found image was filled in and accepted
        self.assertEqual(Path(seen["iso"]), Path("C:/images/mm3.iso"))

    def test_a_new_install_searches_at_once_and_has_no_update_button(self):
        with tempfile.TemporaryDirectory() as scratch:
            calls, seen = self.run_window(Path(scratch) / "MM3", found=Path("C:/images/mm3.iso"))
        self.assertEqual(calls["search"], 1)
        self.assertFalse(seen["update_packed"])
        self.assertEqual(seen["install_text"], "Install")
        self.assertTrue(seen["install_enabled"])

    def test_the_find_button_searches_on_request_even_when_updating(self):
        with tempfile.TemporaryDirectory() as scratch:
            folder = self.fake_install(scratch)
            calls, seen = self.run_window(folder, check=lambda f: setup.InstallCheck(True, "ok"),
                                          found=Path("C:/images/mm3.iso"), press="Find on this PC")
        self.assertEqual(calls["search"], 1)
        self.assertEqual(Path(seen["iso"]), Path("C:/images/mm3.iso"))
        self.assertTrue(seen["update_packed"])                      # still offered: an image is optional


if __name__ == "__main__":
    unittest.main()
