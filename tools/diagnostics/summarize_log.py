#!/usr/bin/env python3
"""Explain MM3 file probes using the current extracted game files, read-only.

An existing fallback proves availability now, not that a past run loaded it.
Locked, missing, corrupt, or skipped archives leave absent assets unverified.
Nothing is extracted, modified, or copied; pristine installer backups are not
runtime fallback locations and are deliberately excluded.
"""

from __future__ import annotations

import argparse
from collections import Counter
import json
import os
from pathlib import Path
import re
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[2]
TAG = re.compile(r"\[(NtCreateFile|FILE_PROBE|FILE_ERROR)\]")
PATH = re.compile(r'\bpath=(?:"([^"]*)"|(.*?))(?=\s+[A-Za-z_][A-Za-z_0-9]*=|$)')
LOGICAL = re.compile(r"(?:^|/)(?:Data/(?:Shared|XBox)/|TDATA/[^/]+/\$C/[0-9a-f]+/)(.+)$", re.I)
COUNT = re.compile(r"\b(?:count|attempts|suppressed)=(\d+)\b")


def logical_asset(path: str) -> str | None:
    match = LOGICAL.search(re.sub(r"/+", "/", path.replace("\\", "/")))
    return match.group(1).casefold() if match else None


def read_events(lines) -> tuple[dict, list, list]:
    probes, errors, unclassified = {}, [], []
    for number, line in enumerate(lines, 1):
        tag = TAG.search(line)
        if not tag:
            continue
        match = PATH.search(line[tag.end():].rstrip())
        path = (match.group(1) if match.group(1) is not None else match.group(2)) if match else None
        is_probe = tag.group(1) == "FILE_PROBE" or (
            tag.group(1) == "NtCreateFile" and "NOT_FOUND" in line[tag.end():])
        if not is_probe:
            errors.append({"line": number, "path": path, "message": line.strip()})
            continue
        asset = logical_asset(path) if path else None
        if asset is None:
            unclassified.append({"line": number, "path": path, "message": line.strip()})
            continue
        item = probes.setdefault(asset, {"asset": asset, "probe_records": 0,
            "runtime_counter_max": 0, "candidates": [], "last_path": path, "last_line": number})
        item["probe_records"] += 1
        # Runtime summaries sample a global cumulative miss counter. The last
        # sampled host path does not own all those misses. Keep the published
        # counter separately rather than calling it this asset's attempt count.
        counts = [int(value) for value in COUNT.findall(line)]
        item["runtime_counter_max"] = max(item["runtime_counter_max"], *counts, 0)
        if path not in item["candidates"]:
            item["candidates"].append(path)
        item["last_path"], item["last_line"] = path, number
    return probes, errors, unclassified


def fallback_index(game_dir: Path, scan_archives: bool) -> tuple[dict, list, list]:
    available, inspected, unavailable = {}, [], []

    def add(asset, location):
        available.setdefault(asset.casefold(), []).append(location)

    def onerror(error):
        unavailable.append({"path": str(error.filename), "error": str(error)})

    # Only native lookup roots count. Backups or packages elsewhere inside
    # game_files can contain matching strings without being loadable fallbacks.
    for root in (game_dir / "Data/Shared", game_dir / "Data/XBox", game_dir / "TDATA"):
        try:
            root.stat()
        except FileNotFoundError:
            continue  # Optional loose-file/DLC roots may simply not exist.
        except OSError as error:
            onerror(error)
            continue
        for folder, _, names in os.walk(root, onerror=onerror):
            for name in names:
                path = Path(folder) / name
                relative = path.relative_to(game_dir).as_posix()
                asset = logical_asset(relative)
                if asset:
                    add(asset, relative)
    for name in ("Data_hd.zip", "Data_dvd.zip"):
        path = game_dir / "Data" / name
        if not scan_archives:
            unavailable.append({"path": str(path), "error": "archive scan skipped"})
            continue
        try:
            with zipfile.ZipFile(path) as archive:
                for member in archive.infolist():
                    if not member.is_dir():
                        add(member.filename.replace("\\", "/"), f"Data/{name}!{member.filename}")
            inspected.append(str(path))
        except (OSError, zipfile.BadZipFile, zipfile.LargeZipFile) as error:
            unavailable.append({"path": str(path), "error": str(error)})
    if not game_dir.is_dir():
        unavailable.append({"path": str(game_dir), "error": "game directory is missing"})
    return available, inspected, unavailable


def summarize(lines, game_dir: Path, *, scan_archives: bool = True) -> dict:
    probes, errors, unclassified = read_events(lines)
    available, inspected, unavailable = fallback_index(game_dir, scan_archives)
    assets = []
    for asset, item in sorted(probes.items()):
        fallbacks = available.get(asset, [])
        status = "resolved" if fallbacks else "not_verified" if unavailable else "unresolved"
        assets.append({**item, "status": status, "fallbacks": fallbacks})
    return {"game_dir": str(game_dir), "probe_records": sum(item["probe_records"] for item in assets),
        "logical_assets": len(assets), "statuses": dict(Counter(item["status"] for item in assets)),
        "assets": assets, "file_errors": errors, "unclassified_probes": unclassified,
        "archives_inspected": inspected, "locations_unavailable": unavailable,
        "note": "Resolved means a current fallback exists; past runtime load success is not verified. "
                "Rate-limited probe records may omit other attempted assets."}


def render(report: dict, limit: int = 12) -> str:
    counts = report["statuses"]
    out = [f"MM3 log: {report['probe_records']} probe records, {report['logical_assets']} logical assets observed; "
        f"{counts.get('resolved', 0)} with existing fallbacks, "
        f"{counts.get('unresolved', 0)} unresolved, {counts.get('not_verified', 0)} not verified.",
        f"File I/O errors: {len(report['file_errors'])}; unclassified probes: {len(report['unclassified_probes'])}.",
        report["note"]]
    for error in report["file_errors"][-limit:]:
        out.append(f"ERROR line {error['line']}: {error['message']}")
    for probe in report["unclassified_probes"][-limit:]:
        out.append(f"UNCLASSIFIED PROBE line {probe['line']}: {probe['message']}")
    # Missing/unverified assets precede the expected successful fallback groups.
    assets = sorted(report["assets"], key=lambda item: (item["status"] == "resolved", item["asset"]))
    for item in assets[:limit]:
        count = f", runtime miss counter={item['runtime_counter_max']}" if item["runtime_counter_max"] else ""
        out.append(f"{item['status'].upper()} {item['asset']} ({item['probe_records']} log records{count})")
        out.append("  " + ("fallback: " + "; ".join(item["fallbacks"]) if item["fallbacks"]
                            else "no fallback found in inspected locations"))
        out.append(f"  last probe (line {item['last_line']}): {item['last_path']}")
    if len(assets) > limit:
        out.append(f"{len(assets) - limit} additional assets omitted; use --limit or --json to see all.")
    for item in report["locations_unavailable"]:
        out.append(f"NOT INSPECTED {item['path']}: {item['error']}")
    return "\n".join(out)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path, help="Play log, runtime log, or pasted error text")
    parser.add_argument("--repo", type=Path, default=ROOT, help="Repository containing game_files")
    parser.add_argument("--game-dir", type=Path, help="Override the extracted game directory")
    parser.add_argument("--skip-archives", action="store_true", help="Inspect loose files only")
    parser.add_argument("--json", action="store_true", help="Emit the complete machine-readable report")
    parser.add_argument("--limit", type=int, default=12, help="Maximum asset/error examples (default: 12)")
    args = parser.parse_args(argv)
    if args.limit < 1:
        parser.error("--limit must be positive")
    game_dir = (args.game_dir or args.repo / "game_files").resolve()
    try:
        with args.log.open(encoding="utf-8-sig", errors="replace") as stream:
            report = summarize(stream, game_dir, scan_archives=not args.skip_archives)
    except OSError as error:
        print(f"MM3 log summary: cannot read {args.log}: {error}", file=sys.stderr)
        return 2
    print(json.dumps(report, indent=2) if args.json else render(report, args.limit))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
