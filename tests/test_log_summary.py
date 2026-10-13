"""Real fallback availability, genuine open failures, and incomplete audits."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("mm3_log_summary", ROOT / "tools/diagnostics/summarize_log.py")
summary = importlib.util.module_from_spec(spec)
spec.loader.exec_module(summary)


def write(path, content=b"asset"):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(content)


def archives(folder):
    (folder / "Data").mkdir(parents=True, exist_ok=True)
    for name, member in (("Data_hd.zip", "Cars/AudiTT/materialSet00.omb"),
                         ("Data_dvd.zip", "Cars/AudiTT/AudiTexture.cdds")):
        with zipfile.ZipFile(folder / "Data" / name, "w") as archive:
            archive.writestr(member, b"archive asset")


class LogSummaryTest(unittest.TestCase):
    def test_old_probe_chains_resolve_shared_dlc_and_archive_without_writes(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            write(folder / "Data/Shared/Cars/NewBeetle/body.cmp")
            write(folder / "TDATA/4d53002a/$C/4d53002a00000005/cars/car215/Concept.cdds")
            archives(folder)
            before = {p: p.read_bytes() for p in folder.rglob("*") if p.is_file()}
            lines = [
                r"[NtCreateFile] NOT_FOUND path=C:\User\Cache\\Data\Shared\Cars\newbeetle\body.cmp err=3 access=0x80100080",
                r"[NtCreateFile] NOT_FOUND path=game_files\Data\XBox\Cars\newbeetle\body.cmp err=3",
                r"[NtCreateFile] NOT_FOUND path=game_files\TDATA\4d53002a\$C\4d53002a00000004\Cars\car215\concept.cdds err=3",
                r"[NtCreateFile] NOT_FOUND path=game_files\Data\Shared\Cars\AudiTT\materialSet00.omb err=3",
                r"[NtCreateFile] NOT_FOUND path=game_files\Data\Shared\Cars\AudiTT\AudiTexture.cdds err=3",
                "  [FILE] -> 0xC000003A NOT_FOUND (win32 err=3 ERROR_PATH_NOT_FOUND)",
            ]
            report = summary.summarize(lines, folder)
            self.assertEqual(report["probe_records"], 5)
            self.assertEqual(report["logical_assets"], 4)
            self.assertEqual(report["statuses"], {"resolved": 4})
            assets = {a["asset"]: a for a in report["assets"]}
            self.assertEqual(assets["cars/newbeetle/body.cmp"]["probe_records"], 2)
            self.assertIn("00000005", assets["cars/car215/concept.cdds"]["fallbacks"][0])
            self.assertIn("Data_hd.zip!", assets["cars/auditt/materialset00.omb"]["fallbacks"][0])
            self.assertEqual(before, {p: p.read_bytes() for p in folder.rglob("*") if p.is_file()})

    def test_new_probe_summaries_and_errors_remain_separate_with_spaced_paths(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            archives(folder)
            lines = [
                '[FILE_PROBE] path="C:\\My Cache\\Data\\Shared\\ParticleSystems\\rats.dps" count=64 suppressed=62',
                r"2026-10-13 [stderr] [FILE_PROBE] candidate_miss count=128 path=C:\My Cache\Data\Shared\ParticleSystems\rats.dps err=3 thread=7 tick_ms=12864",
                '[FILE_ERROR] op=read-open path="game_files\\Data\\Data_hd.zip" err=32 caller=00123456',
                r"[NtCreateFile] ERROR path=game_files\Data\Shared\Cars\AudiTT\body.cmp err=5 access=0x80000000",
                "[FILE_PROBE] path=game_files\\Other\\unknown.dat count=10",
            ]
            report = summary.summarize(lines, folder)
            self.assertEqual(report["statuses"], {"unresolved": 1})
            self.assertEqual(report["assets"][0]["runtime_counter_max"], 128)
            self.assertEqual(report["assets"][0]["last_line"], 2)
            self.assertEqual(len(report["file_errors"]), 2)
            self.assertEqual(report["file_errors"][0]["path"], r"game_files\Data\Data_hd.zip")
            self.assertEqual(len(report["unclassified_probes"]), 1)
            self.assertIn("ERROR line 3", summary.render(report))

    def test_locked_archive_leaves_missing_asset_unverified_but_loose_asset_resolved(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            archives(folder)
            write(folder / "Data/Shared/Cars/AudiTT/body.cmp")
            original = summary.zipfile.ZipFile

            def locked(path):
                if Path(path).name == "Data_hd.zip":
                    raise PermissionError(13, "archive in use", str(path))
                return original(path)

            with patch.object(summary.zipfile, "ZipFile", side_effect=locked):
                report = summary.summarize([
                    r"[FILE_PROBE] path=game_files\Data\Shared\Cars\AudiTT\materialSet00.omb count=5",
                    r"[FILE_PROBE] path=game_files\Data\XBox\Cars\AudiTT\body.cmp count=2"], folder)
            self.assertEqual(report["statuses"], {"resolved": 1, "not_verified": 1})
            self.assertEqual(len(report["locations_unavailable"]), 1)
            self.assertIn("archive in use", report["locations_unavailable"][0]["error"])

    def test_skipped_missing_or_corrupt_archives_do_not_prove_asset_is_absent(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            write(folder / "Data/Data_hd.zip", b"invalid ZIP")
            write(folder / ".mm3-car-mods/Data/Shared/ParticleSystems/rats.dps")
            line = r"[FILE_PROBE] path=game_files\Data\Shared\ParticleSystems\rats.dps count=20"
            for scan in (True, False):
                report = summary.summarize([line], folder, scan_archives=scan)
                self.assertEqual(report["statuses"], {"not_verified": 1})
                self.assertFalse(report["assets"][0]["fallbacks"])
                self.assertEqual(len(report["locations_unavailable"]), 2)


if __name__ == "__main__":
    unittest.main()
