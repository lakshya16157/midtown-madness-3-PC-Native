"""Cache merges preserve existing bytecode and every current manifest key."""
import hashlib
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("shader_cache_merge", ROOT / "tools/diagnostics/merge_shader_cache.py")
merge = importlib.util.module_from_spec(spec)
spec.loader.exec_module(merge)
FAMILY = bytes.fromhex("c" * 64)
MANIFEST = "c" * 64 + ".psm"


def record(value, variant=0):
    words = [value] * merge.WORDS
    words[41] = variant
    return struct.pack("<54I", *words)


def bytecode(key, payload=b"DXBC fixture"):
    return merge.BYTECODE_HEADER.pack(b"NVSCACHE", 1, len(payload), bytes.fromhex(key),
                                      hashlib.sha256(payload).digest()) + payload


class CacheMergeTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source, self.target = self.root / "source", self.root / "target"
        self.source.mkdir()
        self.target.mkdir()

    def manifest(self, folder, records):
        path = folder / MANIFEST
        path.write_bytes(merge.encode_manifest(FAMILY, records))
        return path

    def test_preview_is_read_only_and_apply_preserves_bytecode_and_ordered_keys(self):
        existing_key, new_key = "a" * 64, "b" * 64
        existing = bytecode(existing_key, b"DXBC existing valid compiler output")
        (self.target / (existing_key + ".bin")).write_bytes(existing)
        (self.source / (existing_key + ".bin")).write_bytes(bytecode(existing_key))
        (self.source / (new_key + ".bin")).write_bytes(bytecode(new_key))
        original = self.manifest(self.target, [record(0), record(1)]).read_bytes()
        self.manifest(self.source, [record(1), record(2)])
        preview = merge.merge_caches(self.source, self.target)
        self.assertFalse(preview["applied"])
        self.assertEqual(preview["missing_bytecodes"], 1)
        self.assertEqual((self.target / MANIFEST).read_bytes(), original)
        self.assertFalse((self.target / (new_key + ".bin")).exists())
        receipt = merge.merge_caches(self.source, self.target, apply=True)
        self.assertEqual((self.target / (existing_key + ".bin")).read_bytes(), existing)
        self.assertEqual(merge.read_manifest(self.target / MANIFEST)[2], [record(0), record(1), record(2)])
        self.assertEqual(Path(receipt["manifest_results"][0]["backup"]).read_bytes(), original)
        self.assertEqual(merge.read_bytecode(self.target / (new_key + ".bin")), bytecode(new_key))
        repeated = merge.merge_caches(self.source, self.target, apply=True)
        self.assertEqual(repeated["copied_bytecodes"], [])
        self.assertEqual(repeated["manifest_results"], [])

    def test_invalid_bytecode_aborts_before_any_cache_mutation(self):
        (self.source / ("a" * 64 + ".bin")).write_bytes(bytecode("a" * 64))
        invalid = bytearray(bytecode("b" * 64))
        invalid[-1] ^= 1
        (self.source / ("b" * 64 + ".bin")).write_bytes(invalid)
        with self.assertRaisesRegex(ValueError, "checksum"):
            merge.merge_caches(self.source, self.target, apply=True)
        self.assertEqual(list(self.target.iterdir()), [])
        bad_key = self.source / ("b" * 64 + ".bin")
        bad_key.write_bytes(bytecode("c" * 64))
        with self.assertRaisesRegex(ValueError, "key"):
            merge.read_bytecode(bad_key)

    def test_manifest_schema_family_checksum_variant_and_truncation_are_rejected(self):
        valid = merge.encode_manifest(FAMILY, [record(1)])
        mutations = []
        for offset in (8, 12, 16, 20, 24, 56, 90):
            data = bytearray(valid)
            data[offset] ^= 1
            mutations.append(bytes(data))
        mutations += [valid[:-1], valid + b"x", merge.encode_manifest(FAMILY, [record(1, variant=4)])]
        path = self.source / MANIFEST
        for data in mutations:
            with self.subTest(data=data[:25]), self.assertRaises(ValueError):
                path.write_bytes(data)
                merge.read_manifest(path)

    def test_union_overflow_never_drops_current_keys_or_copies_pending_bytecode(self):
        original = self.manifest(self.target, [record(i) for i in range(512)]).read_bytes()
        self.manifest(self.source, [record(512)])
        (self.source / ("a" * 64 + ".bin")).write_bytes(bytecode("a" * 64))
        with self.assertRaisesRegex(ValueError, "512-state"):
            merge.merge_caches(self.source, self.target, apply=True)
        self.assertEqual((self.target / MANIFEST).read_bytes(), original)
        self.assertEqual(len(list(self.target.iterdir())), 1)

    def test_reread_after_backup_preserves_a_concurrently_added_state(self):
        target = self.manifest(self.target, [record(0)])
        self.manifest(self.source, [record(1)])
        real_backup = merge.write_backup
        changed = False
        def racing_backup(folder, name, data):
            nonlocal changed
            backup = real_backup(folder, name, data)
            if not changed:
                changed = True
                target.write_bytes(merge.encode_manifest(FAMILY, [record(0), record(2)]))
            return backup
        with patch.object(merge, "write_backup", side_effect=racing_backup):
            receipt = merge.merge_caches(self.source, self.target, apply=True)
        self.assertEqual(merge.read_manifest(target)[2], [record(0), record(2), record(1)])
        backup = Path(receipt["manifest_results"][0]["backup"])
        self.assertEqual(backup.read_bytes(), merge.encode_manifest(FAMILY, [record(0), record(2)]))

    def test_missing_manifest_is_published_atomically_and_same_cache_is_rejected(self):
        self.manifest(self.source, [record(2)])
        receipt = merge.merge_caches(self.source, self.target, apply=True)
        self.assertEqual(merge.read_manifest(self.target / MANIFEST)[2], [record(2)])
        self.assertIsNone(receipt["manifest_results"][0]["backup"])
        self.assertFalse(list(self.target.glob("*.tmp")))
        with self.assertRaisesRegex(ValueError, "distinct"):
            merge.merge_caches(self.source, self.source, apply=True)

    def test_report_cannot_overwrite_a_cache_or_backup_file(self):
        backups = self.root / "backups"
        for path in (self.source / MANIFEST, self.target / MANIFEST, backups / "original.bak"):
            with self.subTest(path=path), self.assertRaisesRegex(ValueError, "Report must be outside"):
                merge.validate_report_path(path, self.source, self.target, backups)
        merge.validate_report_path(self.root / "report.json", self.source, self.target, backups)


if __name__ == "__main__":
    unittest.main()
