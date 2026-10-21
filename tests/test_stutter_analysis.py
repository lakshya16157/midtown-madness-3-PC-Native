"""Interactive stutter reports keep QPC windows, Present intervals and evidence distinct."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "mm3_stutter_analysis", ROOT / "tools/diagnostics/analyze_stutter.py")
analysis = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analysis)

HEADER = "qpc_ms,frame_ms,pace_ms,present_qpc_ms,present_ms\n"


class StutterAnalysisTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.folder = Path(temporary.name)
        self.csv = self.folder / "frames.csv"
        self.log = self.folder / "play.log"
        self.log.write_text("", encoding="utf-8")

    def frames(self, body, header=HEADER):
        self.csv.write_text(header + body, encoding="utf-8")

    def report(self, **options):
        return analysis.analyze(self.csv, self.log, **options)

    def test_full_interval_boundaries_are_inclusive_and_crossing_samples_excluded(self):
        self.frames("100,20,0,100,20\n120,20,0,120,20\n140,20,0,140,20\n160,20,0,160,20\n")
        result = self.report(start_ms=100, end_ms=140)
        for metric in analysis.METRICS:
            self.assertEqual(result["metrics"][metric]["summary"]["frames"], 2)
            self.assertEqual(result["csv_input"]["outside_window_samples"][metric], 2)
        self.assertEqual(self.report(start_ms=101, end_ms=139)["metrics"]["frame_ms"]["summary"], None)
        self.assertEqual(self.report(start_ms=120, end_ms=120)["metrics"]["frame_ms"]["summary"], None)

    def test_present_intervals_use_their_own_timestamp_duration_and_window(self):
        self.frames("150,50,0,149,80\n170,20,0,169,20\n300,130,0,299,130\n")
        result = self.report(start_ms=100, end_ms=300)
        flip, present = (result["metrics"][key] for key in ("frame_ms", "present_ms"))
        self.assertEqual(flip["summary"]["frames"], 3)
        self.assertEqual(present["summary"]["frames"], 2)
        self.assertEqual(flip["summary"]["over_50_ms"], 1)
        self.assertEqual(flip["summary"]["over_100_ms"], 1)
        self.assertEqual(present["top_intervals"][0]["end_qpc_ms"], 299)
        self.assertEqual(flip["top_intervals"][0]["end_qpc_ms"], 300)
        self.assertEqual(present["summary"]["p99"], 130)

    def test_hitches_overlap_by_qpc_not_log_order_and_touching_endpoints_are_excluded(self):
        self.frames("200,100,0,190,80\n")
        self.log.write_text(
            "unrelated\n"
            "[HITCH] phase=after qpc_ms=210 start_qpc_ms=200 duration_ms=10\n"
            "[HITCH] phase=inside qpc_ms=150 start_qpc_ms=120 duration_ms=30\n"
            "[HITCH] phase=before qpc_ms=100 start_qpc_ms=90 duration_ms=10\n"
            "[HITCH] phase=spanning qpc_ms=250 start_qpc_ms=50 duration_ms=200\n"
            "[HITCH] phase=left qpc_ms=115 start_qpc_ms=95 duration_ms=20\n",
            encoding="utf-8")
        result = self.report(start_ms=100, end_ms=200)
        top = result["metrics"]["frame_ms"]["top_intervals"][0]
        events = {event["phase"]: event for event in top["overlapping_hitches"]}
        self.assertEqual(set(events), {"inside", "spanning", "left"})
        self.assertEqual(events["inside"]["log_line"], 3)
        self.assertEqual(events["spanning"]["overlap_ms"], 100)
        self.assertEqual(events["left"]["overlap_ms"], 15)
        self.assertEqual(result["log_input"]["outside_window_records"], 2)
        present_events = result["metrics"]["present_ms"]["top_intervals"][0]["overlapping_hitches"]
        self.assertEqual(next(event for event in present_events if event["phase"] == "left")["overlap_ms"], 5)
        rendered = analysis.render(result)
        self.assertIn("QPC [100.0000, 200.0000]", rendered)
        self.assertIn("Log line 3", rendered)
        self.assertIn("not a causal diagnosis", rendered)

    def test_partial_live_records_are_ignored_even_when_they_look_parseable(self):
        self.frames("100,40,0,99,40\n200,100,0,199,100")
        self.log.write_text("[HITCH] phase=not_finished qpc_ms=95 duration_ms=20", encoding="utf-8")
        result = self.report()
        self.assertEqual(result["metrics"]["frame_ms"]["summary"]["frames"], 1)
        self.assertTrue(result["csv_input"]["partial_trailing_line_ignored"])
        self.assertTrue(result["log_input"]["partial_trailing_line_ignored"])
        self.assertEqual(result["hitch_phase_counts"], {})

    def test_malformed_rows_and_nonfinite_samples_cannot_corrupt_percentiles(self):
        self.frames("100,40,0,99,\n110,nan,0,109,20\n120,20\n"
                    "130,inf,0,129,-5\n140,20,0,139,20,extra\n"
                    "150,20,0,149,20\n160,-1,0,159,0\n")
        result = self.report()
        self.assertEqual(result["csv_input"]["malformed_rows"], 2)
        self.assertEqual(result["csv_input"]["malformed_metric_samples"]["frame_ms"], 3)
        self.assertEqual(result["csv_input"]["malformed_metric_samples"]["present_ms"], 1)
        self.assertEqual(result["metrics"]["frame_ms"]["summary"]["frames"], 2)
        self.assertEqual(result["metrics"]["present_ms"]["summary"]["frames"], 2)
        json.dumps(result, allow_nan=False)

    def test_thresholds_are_strict_and_top_limits_do_not_change_summary(self):
        self.frames("100,33.33,0,99,33.33\n200,50,0,199,50\n"
                    "300,100,0,299,100\n500,101,0,499,101\n")
        metric = self.report(top=1)["metrics"]["frame_ms"]
        self.assertEqual([metric["summary"][key] for key in analysis.THRESHOLDS], [3, 2, 1])
        self.assertEqual(metric["slow_interval_count"], 3)
        self.assertEqual(len(metric["top_intervals"]), 1)
        self.assertEqual(metric["top_intervals"][0]["csv_line"], 5)

    def test_bom_crlf_and_bad_csv_quoting_preserve_physical_line_numbers(self):
        self.csv.write_bytes(b"\xef\xbb\xbf" + HEADER.replace("\n", "\r\n").encode()
                             + b'"unfinished,40,0,99,40\r\n100,40,0,99,40\r\n')
        result = self.report()
        self.assertEqual(result["csv_input"]["malformed_rows"], 1)
        sample = result["metrics"]["frame_ms"]["top_intervals"][0]
        self.assertEqual(sample["csv_line"], 3)
        self.assertFalse(result["csv_input"]["partial_trailing_line_ignored"])

    def test_legacy_columns_and_hitches_derive_start_without_fabricating_present_data(self):
        self.frames("100,40\n", header="qpc_ms,frame_ms\n")
        self.log.write_text("[HITCH] phase=legacy qpc_ms=90 duration_ms=20\n"
                            "[HITCH] phase=bad qpc_ms=inf duration_ms=20\n"
                            "[HITCH] phase=backward qpc_ms=80 start_qpc_ms=90 duration_ms=10\n"
                            "[HITCH] phase=truncated duration_ms=20\n", encoding="utf-8")
        result = self.report()
        self.assertIsNone(result["metrics"]["present_ms"]["summary"])
        event = result["metrics"]["frame_ms"]["top_intervals"][0]["overlapping_hitches"][0]
        self.assertEqual(event["start_qpc_ms"], 70)
        self.assertEqual(event["start_source"], "end_minus_duration")
        self.assertEqual(result["log_input"]["malformed_hitch_records"], 3)

    def test_empty_headers_invalid_windows_and_cli_errors_are_explicit(self):
        self.frames("")
        self.assertIsNone(self.report()["metrics"]["frame_ms"]["summary"])
        for options in ({"start_ms": 2, "end_ms": 1}, {"start_ms": float("nan")},
                        {"end_ms": -1}, {"top": 0}):
            with self.assertRaises(ValueError):
                self.report(**options)
        self.csv.write_text("qpc_ms,frame_ms", encoding="utf-8")
        with contextlib.redirect_stderr(io.StringIO()) as error:
            self.assertEqual(analysis.main([str(self.csv), str(self.log)]), 2)
        self.assertIn("no complete CSV header", error.getvalue())
        self.csv.write_text("qpc_ms,frame_ms,frame_ms\n", encoding="utf-8")
        with self.assertRaises(ValueError):
            self.report()


if __name__ == "__main__":
    unittest.main()
