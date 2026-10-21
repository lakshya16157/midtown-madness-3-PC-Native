"""Validate and merge native shader caches; default is a read-only preview.

Run only after every game using the destination cache has stopped. Existing
bytecode is never replaced. Compatible pixel-state manifests are merged as an
ordered union, backed up, checked again immediately before atomic publication.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import tempfile
import uuid

BYTECODE_HEADER = struct.Struct("<8sII32s32s")
MANIFEST_HEADER = struct.Struct("<8sIIII32s32s")
SCHEMA, WORDS, LIMIT = 1, 54, 512
RECORD_BYTES = WORDS * 4
MAX_CODE = 16 * 1024 * 1024


def filename_key(path, suffix):
    if not re.fullmatch(r"[0-9a-fA-F]{64}" + re.escape(suffix), path.name):
        raise ValueError(f"Invalid cache filename: {path.name}")
    return bytes.fromhex(path.stem)


def read_bytecode(path):
    key = filename_key(path, ".bin")
    if path.is_symlink():
        raise ValueError(f"Cache files must be ordinary files: {path}")
    size = path.stat().st_size
    if not BYTECODE_HEADER.size < size <= BYTECODE_HEADER.size + MAX_CODE:
        raise ValueError(f"Invalid bytecode file size: {path}")
    data = path.read_bytes()
    magic, schema, length, recorded_key, checksum = BYTECODE_HEADER.unpack_from(data)
    payload = data[BYTECODE_HEADER.size:]
    if (magic != b"NVSCACHE" or schema != SCHEMA or not length or length > MAX_CODE or
            len(data) != BYTECODE_HEADER.size + length or recorded_key != key or
            hashlib.sha256(payload).digest() != checksum):
        raise ValueError(f"Invalid bytecode header/key/length/checksum: {path}")
    return data


def read_manifest(path):
    family = filename_key(path, ".psm")
    if path.is_symlink():
        raise ValueError(f"Cache files must be ordinary files: {path}")
    if not MANIFEST_HEADER.size <= path.stat().st_size <= MANIFEST_HEADER.size + LIMIT * RECORD_BYTES:
        raise ValueError(f"Invalid manifest file size: {path}")
    data = path.read_bytes()
    magic, schema, width, count, reserved, recorded_family, checksum = MANIFEST_HEADER.unpack_from(data)
    payload = data[MANIFEST_HEADER.size:]
    if (magic != b"NVPSMAN1" or schema != SCHEMA or width != RECORD_BYTES or count > LIMIT or
            reserved or family != recorded_family or len(payload) != count * RECORD_BYTES or
            hashlib.sha256(payload).digest() != checksum):
        raise ValueError(f"Invalid manifest header/family/length/checksum: {path}")
    records = [payload[offset:offset + RECORD_BYTES] for offset in range(0, len(payload), RECORD_BYTES)]
    if any(struct.unpack_from("<I", record, 41 * 4)[0] > 3 for record in records):
        raise ValueError(f"Invalid shader variant: {path}")
    return data, family, records


def encode_manifest(family, records):
    if len(records) > LIMIT or any(len(record) != RECORD_BYTES for record in records):
        raise ValueError("Manifest union exceeds the native record format or 512-state limit")
    payload = b"".join(records)
    return MANIFEST_HEADER.pack(b"NVPSMAN1", SCHEMA, RECORD_BYTES, len(records), 0,
                                family, hashlib.sha256(payload).digest()) + payload


def ordered_union(existing, incoming):
    records, seen = list(existing), set(existing)
    for record in incoming:
        if record not in seen:
            records.append(record)
            seen.add(record)
    if len(records) > LIMIT:
        raise ValueError("Manifest union exceeds the native 512-state limit; no keys may be dropped")
    return records


def cache_plan(source, destination):
    source, destination = Path(source).resolve(strict=True), Path(destination).resolve()
    if not source.is_dir() or source == destination or (destination.exists() and not destination.is_dir()):
        raise ValueError("Source and destination must be distinct cache directories")
    binaries, manifests, unchanged_bins = [], [], 0
    # Validate the complete planned transaction before writing any cache file.
    for path in sorted(source.glob("*.bin")):
        data = read_bytecode(path)
        target = destination / path.name
        if target.exists():
            read_bytecode(target)
            unchanged_bins += 1
        else:
            binaries.append((path.name, data))
    for path in sorted(source.glob("*.psm")):
        _, family, incoming = read_manifest(path)
        target = destination / path.name
        current = read_manifest(target)[2] if target.exists() else []
        union = ordered_union(current, incoming)
        if union != current or not target.exists():
            manifests.append((path.name, family, incoming, len(current), len(union)))
    return {"source": source, "destination": destination, "binaries": binaries,
            "manifests": manifests, "existing_bytecodes_preserved": unchanged_bins}


def stage_bytes(folder, data):
    descriptor, name = tempfile.mkstemp(prefix=".shader-merge-", suffix=".tmp", dir=folder)
    path = Path(name)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
    except BaseException:
        path.unlink(missing_ok=True)
        raise
    return path


def write_backup(folder, original_name, data):
    folder.mkdir(parents=True, exist_ok=True)
    path = folder / (original_name + "." + uuid.uuid4().hex + ".bak")
    with path.open("xb") as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())
    return path


def merge_manifest(target, family, incoming, backups):
    for _ in range(5):
        if target.exists():
            before, observed_family, current = read_manifest(target)
            if observed_family != family:
                raise ValueError(f"Manifest family changed: {target}")
        else:
            before, current = None, []
        union = ordered_union(current, incoming)
        if before is not None and union == current:
            return {"file": target.name, "action": "preserved", "states": len(current)}
        temporary = stage_bytes(target.parent, encode_manifest(family, union))
        try:
            latest = read_manifest(target)[0] if target.exists() else None
            if latest != before:
                continue
            backup = write_backup(backups, target.name, before) if before is not None else None
            # Re-read after backup creation too: another writer may have gained
            # keys while we were staging. A quiescent game is still required;
            # ordinary file replacement offers no compare-and-swap primitive.
            latest = read_manifest(target)[0] if target.exists() else None
            if latest != before:
                continue
            if before is None:
                try:
                    os.link(temporary, target)  # Atomic exclusive publication.
                except FileExistsError:
                    continue
            else:
                os.replace(temporary, target)
            _, _, verified = read_manifest(target)
            if verified != union or not set(current) <= set(verified):
                raise RuntimeError(f"Manifest post-commit verification failed: {target}")
            return {"file": target.name, "action": "merged", "previous_states": len(current),
                    "states": len(verified), "added_states": len(verified) - len(current),
                    "backup": str(backup) if backup else None}
        finally:
            temporary.unlink(missing_ok=True)
    raise RuntimeError(f"Destination manifest kept changing; stop all games before merging: {target}")


def merge_caches(source, destination, apply=False, backup_dir=None):
    plan = cache_plan(source, destination)
    target = plan["destination"]
    receipt = {"source": str(plan["source"]), "destination": str(target), "applied": apply,
               "existing_bytecodes_preserved": plan["existing_bytecodes_preserved"],
               "missing_bytecodes": len(plan["binaries"]),
               "missing_bytecode_bytes": sum(len(data) for _, data in plan["binaries"]),
               "planned_manifest_unions": [{"file": name, "previous_states": count, "union_states": union}
                                           for name, _, _, count, union in plan["manifests"]],
               "copied_bytecodes": [], "manifest_results": [],
               "limits": ["All games using the destination must be stopped during apply.",
                          "Existing bytecode files are preserved, including independently valid compiler output with the same key.",
                          "Content-addressed identities and native format/checksum validation establish cache compatibility; runtime shader creation still performs its own validation."]}
    if not apply:
        return receipt
    target.mkdir(parents=True, exist_ok=True)
    backups = Path(backup_dir).resolve() if backup_dir else target / ".merge-backups"
    for name, data in plan["binaries"]:
        destination_file = target / name
        temporary = stage_bytes(target, data)
        try:
            try:
                os.link(temporary, destination_file)  # Fails instead of replacing any existing file.
                receipt["copied_bytecodes"].append(name)
            except FileExistsError:
                read_bytecode(destination_file)
            if read_bytecode(destination_file) != data and name in receipt["copied_bytecodes"]:
                raise RuntimeError(f"Copied bytecode changed after publication: {destination_file}")
        finally:
            temporary.unlink(missing_ok=True)
    for name, family, incoming, _, _ in plan["manifests"]:
        receipt["manifest_results"].append(merge_manifest(target / name, family, incoming, backups))
    return receipt


def validate_report_path(report, source, destination, backup_dir=None):
    if report is None:
        return
    report = Path(report).resolve()
    protected = [Path(source).resolve(), Path(destination).resolve()]
    protected.append(Path(backup_dir).resolve() if backup_dir else protected[1] / ".merge-backups")
    for folder in protected:
        try:
            report.relative_to(folder)
        except ValueError:
            continue
        raise ValueError("Report must be outside source, destination and backup cache directories")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--apply", action="store_true", help="Write the validated merge after all games have stopped")
    parser.add_argument("--backup-dir", type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    try:
        validate_report_path(args.report, args.source, args.destination, args.backup_dir)
        receipt = merge_caches(args.source, args.destination, args.apply, args.backup_dir)
    except (OSError, ValueError, RuntimeError) as error:
        parser.exit(2, f"Shader cache merge: {error}\n")
    encoded = json.dumps(receipt, indent=2) + "\n"
    if args.report:
        args.report.write_text(encoded, encoding="utf-8")
    print(encoded, end="")


if __name__ == "__main__":
    main()
