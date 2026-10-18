"""Runtime-entry generation is explicit, transactional and incremental-safe."""
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    'runtime_entries', ROOT / 'scripts/Generate-MM3RuntimeEntries.py')
runtime_entries = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runtime_entries)

OLD = {'start': '0x00001000', 'end': '0x00001010'}
NEW = {'start': '0x00002000', 'end': '0x00002010'}
NEWER = {'start': '0x00003000', 'end': '0x00003010'}
OLD_BODY = 'void sub_00001000(void)\n{\n    /* retain exact custom repair */\n    eax = 0x1234;\n    return;\n}'
NEW_BODY = 'void sub_00002000(void)\n{\n    eax = 2;\n    return;\n}'
NEWER_BODY = 'void sub_00003000(void)\n{\n    eax = 3;\n    return;\n}'


class RuntimeEntryGeneration(unittest.TestCase):
    def baseline(self):
        return runtime_entries.render_full([OLD], {0x1000: OLD_BODY})

    def test_incremental_keeps_existing_body_and_lookup_bytes(self):
        source = self.baseline()
        result = runtime_entries.render_incremental(source, [NEW], {0x2000: NEW_BODY})
        prefix, suffix = source.split(runtime_entries.LOOKUP_TYPE)
        self.assertTrue(result.startswith(prefix))
        self.assertIn(OLD_BODY, result)
        self.assertIn('    case 0x00001000u: return sub_00001000;\n', result)
        self.assertIn('    case 0x00002000u: return sub_00002000;\n', result)
        self.assertIn(NEW_BODY, result)
        # Removing only the additions recovers every original byte.
        result = result.replace('void sub_00002000(void);\n' + NEW_BODY + '\n\n', '')
        result = result.replace('    case 0x00002000u: return sub_00002000;\n', '')
        self.assertEqual(result, source)

    def test_incremental_is_idempotent_even_without_new_translation(self):
        result = runtime_entries.render_incremental(self.baseline(), [NEW], {0x2000: NEW_BODY})
        self.assertEqual(runtime_entries.render_incremental(result, [NEW], {}), result)
        self.assertEqual(result.count('void sub_00002000(void)\n{'), 1)

    def test_preserves_crlf_bytes(self):
        source = self.baseline().replace('\n', '\r\n')
        result = runtime_entries.render_incremental(source, [NEW], {0x2000: NEW_BODY})
        self.assertEqual(result.replace('\r\n', ''), result.replace('\r', '').replace('\n', ''))
        self.assertTrue(result.startswith(source.split(runtime_entries.LOOKUP_TYPE)[0]))
        self.assertEqual(runtime_entries.render_incremental(result, [NEW], {}), result)

    def test_missing_lookup_can_be_restored_without_replacing_body(self):
        source = self.baseline().replace('    case 0x00001000u: return sub_00001000;\n', '')
        result = runtime_entries.render_incremental(source, [OLD], {})
        self.assertIn(OLD_BODY, result)
        self.assertEqual(result.count('void sub_00001000(void)\n{'), 1)
        self.assertIn('case 0x00001000u: return sub_00001000;', result)

    def test_invalid_or_ambiguous_generated_layout_is_rejected(self):
        source = self.baseline()
        broken = [source.replace(runtime_entries.LOOKUP_TYPE, ''),
                  source + runtime_entries.LOOKUP_TYPE,
                  source.replace(runtime_entries.LOOKUP_DEFAULT, ''),
                  source.replace('return sub_00001000;', 'return sub_00001001;'),
                  source + '\n' + OLD_BODY]
        for item in broken:
            with self.subTest(source=item[-60:]):
                with self.assertRaises(ValueError):
                    runtime_entries.render_incremental(item, [OLD], {})

    def test_entry_selection_is_explicit_and_uses_manifest_order(self):
        self.assertEqual(runtime_entries.select_entries([OLD, NEW], ['2000', '1000']), [OLD, NEW])
        for entries in (['1000', '0x1000'], ['9999']):
            with self.assertRaises(ValueError):
                runtime_entries.select_entries([OLD, NEW], entries)
        for entries in ([OLD, OLD], [{'start': '1000', 'end': '1000'}],
                        [{'start': '-1', 'end': '1000'}]):
            with self.assertRaises(ValueError):
                runtime_entries.validate_entries(entries)

    def test_function_metadata_overlay_preserves_unrelated_fields(self):
        old = {'start': '0x00001000', 'end': '0x00001010', 'custom': ['keep', 12]}
        stale = {'start': '0x2000', 'end': '0x2001', 'custom': 'replace'}
        result = runtime_entries.overlay_function_metadata([old, stale], [NEW])
        self.assertEqual(result[0], old)
        self.assertEqual(result[1]['end'], '0x00002010')
        self.assertEqual(result[1]['size'], 16)
        self.assertEqual(result[1]['detection_method'], 'runtime_indirect_call')

    def test_local_extended_precision_and_unwind_repairs_are_shared(self):
        entry = dict(NEW, x87_extended_loads=['0x200000'], x87_helper_unwinds_caller=True)
        body = NEW_BODY.replace('eax = 2;', 'fp_push(MEMF(0x200000)); /* fld */\n'
                                '    RECOMP_ABI_CALL(0x0009BC52u, sub_0009BC52);')
        repaired = runtime_entries.repair_runtime_body(body, entry)
        self.assertIn('fp_push(MEMFP80(0x200000)); /* fld tbyte */', repaired)
        self.assertIn('if (esp == math_sp + 4u) return;', repaired)
        with self.assertRaises(RuntimeError):
            runtime_entries.repair_runtime_body(NEW_BODY, entry)
        with self.assertRaises(RuntimeError):
            runtime_entries.repair_runtime_body(None, NEW)

    def make_fixture(self, folder):
        out = folder / 'out'
        out.mkdir()
        manifest = folder / 'manifest.json'
        manifest.write_text(json.dumps([OLD, NEW, NEWER]))
        (out / 'runtime_entry_functions.json').write_text(json.dumps([dict(OLD, custom='preserve')]))
        (out / 'recomp_runtime_entries.c').write_bytes(self.baseline().encode())
        return out, manifest

    def test_failed_translation_does_not_change_either_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            out, manifest = self.make_fixture(Path(tmp))
            before = {path.name: path.read_bytes() for path in out.iterdir()}
            with patch.object(runtime_entries, 'translate_bodies', side_effect=RuntimeError('lifter failed')):
                with self.assertRaises(RuntimeError):
                    runtime_entries.main([str(out), '--manifest', str(manifest), '--incremental', '--entries', '2000'])
            self.assertEqual(before, {path.name: path.read_bytes() for path in out.iterdir()})

    def test_cli_incremental_only_lifts_new_selected_entries(self):
        with tempfile.TemporaryDirectory() as tmp:
            out, manifest = self.make_fixture(Path(tmp))
            args = [str(out), '--manifest', str(manifest), '--incremental', '--entries', '1000', '2000']
            with patch.object(runtime_entries, 'translate_bodies', return_value={0x2000: NEW_BODY}) as lift:
                runtime_entries.main(args)
                self.assertEqual(lift.call_args.args[2], [NEW])
            once = (out / 'recomp_runtime_entries.c').read_bytes()
            outputs = [out / 'recomp_runtime_entries.c', out / 'runtime_entry_functions.json']
            for output in outputs:
                os.utime(output, ns=(946684800000000000, 946684800000000000))
            mtimes = {output: output.stat().st_mtime_ns for output in outputs}
            with patch.object(runtime_entries, 'translate_bodies', return_value={}) as lift:
                runtime_entries.main(args)
                self.assertEqual(lift.call_args.args[2], [])
            self.assertEqual((out / 'recomp_runtime_entries.c').read_bytes(), once)
            self.assertEqual({output: output.stat().st_mtime_ns for output in outputs}, mtimes)
            functions = json.loads((out / 'runtime_entry_functions.json').read_text())
            self.assertNotIn('0x00003000', [item['start'] for item in functions])

    def test_full_mode_regenerates_all_manifest_entries(self):
        with tempfile.TemporaryDirectory() as tmp:
            out, manifest = self.make_fixture(Path(tmp))
            bodies = {0x1000: OLD_BODY, 0x2000: NEW_BODY, 0x3000: NEWER_BODY}
            with patch.object(runtime_entries, 'translate_bodies', return_value=bodies) as lift:
                runtime_entries.main([str(out), '--manifest', str(manifest)])
                self.assertEqual(lift.call_args.args[2], [OLD, NEW, NEWER])
            self.assertEqual((out / 'recomp_runtime_entries.c').read_text(),
                             runtime_entries.render_full([OLD, NEW, NEWER], bodies))


if __name__ == '__main__':
    unittest.main(verbosity=2)
