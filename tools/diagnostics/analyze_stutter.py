"""Analyze interactive frame CSVs and correlate slow intervals with a Play log.

Example: python tools/diagnostics/analyze_stutter.py logs/full-diagnostics-.../frames.csv
         logs/play-....log --start-ms 123000 --end-ms 153000
Bounds use the CSV/log QPC clock, not UTC or elapsed time since launch.
"""
import argparse
from collections import Counter
import csv
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]
_spec = importlib.util.spec_from_file_location(
    "mm3_frame_summary", ROOT / "tools/xboxrecomp/tools/bench/frames.py")
_frames = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_frames)

METRICS = {"frame_ms": "qpc_ms", "present_ms": "present_qpc_ms"}
THRESHOLDS = {"over_33_33_ms": 33.33, "over_50_ms": 50.0, "over_100_ms": 100.0}
FIELDS = re.compile(r'(?<!\S)(\w+)=("[^"\r\n]*"|\S+)')
NOTES = [
    "A capture can include loading, menus, pauses and driving. No gameplay window is inferred; "
    "use --start-ms/--end-ms to select a known test segment.",
    "Full diagnostics can add tracing and synchronous log I/O overhead. Confirm timing with "
    "a matched light diagnostics capture.",
    "frame_ms measures guest flip completion intervals; present_ms measures successful host "
    "DXGI Present return intervals. Neither measures when a frame reaches the display.",
    "HITCH overlap is temporal correlation, not a causal diagnosis. Phases can be nested or "
    "overlap each other; their durations must not be added as independent costs.",
    "Only complete newline-terminated snapshot records are read. Live CSV buffers may not "
    "yet contain the newest frames. Percentiles use the existing frame-summary convention.",
]


def complete_lines(path):
    """Read a bounded snapshot; never accept an unfinished live record."""
    with Path(path).open("rb") as stream:
        data = stream.read(os.fstat(stream.fileno()).st_size)
    text = data.decode("utf-8-sig", errors="replace")
    lines = text.splitlines(keepends=True)
    partial = bool(lines and not lines[-1].endswith("\n"))
    if partial:
        lines.pop()
    return lines, partial


def finite_number(value):
    value = float(value)
    if not math.isfinite(value):
        raise ValueError("non-finite number")
    return value


def validate_window(start_ms, end_ms):
    for value in (start_ms, end_ms):
        if value is not None and (not math.isfinite(value) or value < 0):
            raise ValueError("QPC bounds must be finite and nonnegative")
    if start_ms is not None and end_ms is not None and start_ms > end_ms:
        raise ValueError("--start-ms must not exceed --end-ms")


def contained(start, end, start_ms, end_ms):
    # Include a sample only when its whole measured interval is in the window.
    return (start_ms is None or start >= start_ms) and (end_ms is None or end <= end_ms)


def overlap(start, end, other_start, other_end):
    # Adjacent endpoints have no positive measured time in common.
    return max(0.0, min(end, other_end) - max(start, other_start))


def read_frames(path, start_ms=None, end_ms=None):
    validate_window(start_ms, end_ms)
    lines, partial = complete_lines(path)
    samples = {metric: [] for metric in METRICS}
    info = {"partial_trailing_line_ignored": partial, "malformed_rows": 0,
            "malformed_metric_samples": dict.fromkeys(METRICS, 0),
            "missing_metric_samples": dict.fromkeys(METRICS, 0),
            "outside_window_samples": dict.fromkeys(METRICS, 0), "data_rows": 0}
    header = None
    for number, line in enumerate(lines, 1):
        if not line.strip():
            continue
        try:
            values = next(csv.reader([line], strict=True))
        except csv.Error:
            if header is None:
                raise ValueError("invalid CSV header") from None
            info["malformed_rows"] += 1
            continue
        if header is None:
            header = values
            if len(header) != len(set(header)) or not {"qpc_ms", "frame_ms"} <= set(header):
                raise ValueError("CSV header must contain unique qpc_ms and frame_ms columns")
            continue
        info["data_rows"] += 1
        if len(values) != len(header):
            info["malformed_rows"] += 1
            continue
        row = dict(zip(header, values))
        for metric, timestamp in METRICS.items():
            if not row.get(metric) or not row.get(timestamp):
                info["missing_metric_samples"][metric] += 1
                continue
            try:
                duration, end = finite_number(row[metric]), finite_number(row[timestamp])
                start = end - duration
                if duration < 0 or start < 0:
                    raise ValueError("negative interval")
                if duration == 0:
                    info["missing_metric_samples"][metric] += 1
                    continue
            except ValueError:
                info["malformed_metric_samples"][metric] += 1
                continue
            if not contained(start, end, start_ms, end_ms):
                info["outside_window_samples"][metric] += 1
                continue
            samples[metric].append({"csv_line": number, "start_qpc_ms": start,
                                    "end_qpc_ms": end, "duration_ms": duration})
    if header is None:
        raise ValueError("no complete CSV header is available yet")
    info["columns"] = header
    return samples, info


def read_hitches(path, start_ms=None, end_ms=None):
    validate_window(start_ms, end_ms)
    lines, partial = complete_lines(path)
    events = []
    info = {"partial_trailing_line_ignored": partial, "malformed_hitch_records": 0,
            "outside_window_records": 0}
    for number, line in enumerate(lines, 1):
        if "[HITCH]" not in line:
            continue
        fields = dict(FIELDS.findall(line.split("[HITCH]", 1)[1]))
        try:
            phase = fields["phase"]
            end = finite_number(fields["qpc_ms"])
            duration = finite_number(fields["duration_ms"])
            start = (finite_number(fields["start_qpc_ms"])
                     if "start_qpc_ms" in fields else end - duration)
            if not phase or start < 0 or duration <= 0 or start >= end:
                raise ValueError("invalid hitch interval")
        except (KeyError, ValueError):
            info["malformed_hitch_records"] += 1
            continue
        if overlap(start, end, start_ms if start_ms is not None else start,
                   end_ms if end_ms is not None else end) <= 0:
            info["outside_window_records"] += 1
            continue
        events.append({"log_line": number, "phase": phase, "start_qpc_ms": start,
                       "end_qpc_ms": end, "duration_ms": duration,
                       "start_source": "logged" if "start_qpc_ms" in fields else "end_minus_duration",
                       "message": line.rstrip("\r\n")})
    return events, info


def analyze(csv_path, log_path, start_ms=None, end_ms=None, top=10):
    validate_window(start_ms, end_ms)
    if top < 1:
        raise ValueError("--top must be positive")
    samples, csv_info = read_frames(csv_path, start_ms, end_ms)
    events, log_info = read_hitches(log_path, start_ms, end_ms)
    metrics = {}
    for metric, intervals in samples.items():
        values = [sample["duration_ms"] for sample in intervals]
        statistics = _frames.summarize(values) if values else None
        if statistics is not None:
            statistics.update({name: sum(value > threshold for value in values)
                               for name, threshold in THRESHOLDS.items()})
        slow = sorted((sample for sample in intervals if sample["duration_ms"] > 33.33),
                      key=lambda sample: (-sample["duration_ms"], sample["end_qpc_ms"]))
        top_intervals = []
        for sample in slow[:top]:
            matches = []
            for event in events:
                shared = overlap(sample["start_qpc_ms"], sample["end_qpc_ms"],
                                 event["start_qpc_ms"], event["end_qpc_ms"])
                if shared > 0:
                    matches.append({**event, "overlap_ms": shared})
            matches.sort(key=lambda event: (event["start_qpc_ms"], event["log_line"]))
            top_intervals.append({**sample, "overlapping_hitches": matches})
        metrics[metric] = {"summary": statistics, "slow_interval_count": len(slow),
                           "top_intervals": top_intervals}
    return {"frames_csv": str(csv_path), "play_log": str(log_path),
            "window": {"start_ms": start_ms, "end_ms": end_ms,
                       "selection": "whole interval contained; inclusive bounds"},
            "csv_input": csv_info, "log_input": log_info, "metrics": metrics,
            "hitch_phase_counts": dict(sorted(Counter(event["phase"] for event in events).items())),
            "notes": NOTES}


def render(report):
    window = report["window"]
    out = [f"Frames: {report['frames_csv']}", f"Log: {report['play_log']}",
           f"QPC window: {window['start_ms']} to {window['end_ms']} ms (None = unbounded); "
           "complete measured intervals only."]
    for metric, result in report["metrics"].items():
        summary = result["summary"]
        if summary is None:
            out.append(f"{metric}: no usable intervals in this window.")
            continue
        out.append(f"{metric}: {_frames.fmt(summary)}")
        out.append("  " + ", ".join(f">{threshold:g} ms: {summary[name]}"
                                     for name, threshold in THRESHOLDS.items()))
        for sample in result["top_intervals"]:
            out.append(f"  CSV line {sample['csv_line']}: {sample['duration_ms']:.4f} ms "
                       f"QPC [{sample['start_qpc_ms']:.4f}, {sample['end_qpc_ms']:.4f}]")
            for event in sample["overlapping_hitches"]:
                out.append(f"    Log line {event['log_line']}: {event['phase']} "
                           f"{event['duration_ms']:.4f} ms "
                           f"QPC [{event['start_qpc_ms']:.4f}, {event['end_qpc_ms']:.4f}] "
                           f"overlap={event['overlap_ms']:.4f} ms; start={event['start_source']}")
            if not sample["overlapping_hitches"]:
                out.append("    No recorded HITCH interval overlaps this sample.")
    out.extend(["CSV input: " + json.dumps(report["csv_input"], sort_keys=True),
                "Log input: " + json.dumps(report["log_input"], sort_keys=True),
                "HITCH phase counts intersecting window: " + json.dumps(report["hitch_phase_counts"]),
                *report["notes"]])
    return "\n".join(out)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("frames_csv", type=Path)
    parser.add_argument("play_log", type=Path)
    parser.add_argument("--start-ms", type=float, help="Inclusive interval start on the QPC clock")
    parser.add_argument("--end-ms", type=float, help="Inclusive interval end on the QPC clock")
    parser.add_argument("--top", type=int, default=10, help="Top intervals >33.33 ms per metric (default: 10)")
    parser.add_argument("--json", action="store_true", help="Emit machine-readable results and raw matching log records")
    args = parser.parse_args(argv)
    try:
        report = analyze(args.frames_csv, args.play_log, args.start_ms, args.end_ms, args.top)
    except (OSError, ValueError) as error:
        print(f"MM3 stutter analysis: {error}", file=sys.stderr)
        return 2
    print(json.dumps(report, indent=2, allow_nan=False) if args.json else render(report))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
