"""Device dropout counters, PCM offsets and signal metrics retain their limits."""
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('audio_analysis', ROOT / 'tools/diagnostics/analyze_audio.py')
audio = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audio)


class AudioAnalysisTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.folder = Path(temporary.name)
        self.log = self.folder / 'audio.log'

    def profile(self, end, submitted, played, glitches=0, zero=0, valid=1):
        return (f'[AUDIO_PROFILE] qpc_ms={end} window_ms=1000 queue_zero_observations={zero} '
                f'accepted=188 device_valid={valid} device_samples_submitted={submitted} '
                f'device_samples_played={played} device_glitches_total={glitches}\n')

    def test_zero_queue_observations_are_not_fabricated_glitches(self):
        self.log.write_text(self.profile(1000, 48000, 47900, zero=6) + self.profile(2000, 96000, 95900, zero=8))
        report = audio.analyze(self.log)['windows'][0]
        self.assertEqual(report['queue_zero_observations'], 14)
        self.assertEqual(report['engine_glitches_observed'], 0)
        self.assertEqual(report['source_samples_played_delta'], 48000)
        self.assertAlmostEqual(report['source_backlog_max_ms'], 100 / 48)
        self.assertEqual(report['source_progress_nominal_shortfall_ms'], 0)

    def test_engine_glitches_use_counter_delta_not_total_or_empty_queue(self):
        self.log.write_text(self.profile(1000, 48000, 47900, 7) + self.profile(2000, 96000, 95900, 10))
        report = audio.analyze(self.log)['windows'][0]
        self.assertEqual(report['engine_glitches_observed'], 3)
        self.assertEqual(report['device_glitch_first_total'], 7)
        self.assertEqual(report['queue_zero_observations'], 0)

    def test_source_progress_shortfall_is_separate_from_engine_glitch_counter(self):
        self.log.write_text(self.profile(1000, 48000, 47900) + self.profile(2000, 96000, 95420))
        result = audio.analyze(self.log)['profile_summary']
        self.assertEqual(result['engine_glitches_observed'], 0)
        self.assertEqual(result['source_played_rate_hz'], 47520)
        self.assertEqual(result['source_progress_nominal_shortfall_ms'], 10)

    def test_counter_reset_disables_pcm_mapping_and_does_not_wrap_delta(self):
        self.log.write_text(self.profile(1000, 48000, 47900, 7) + self.profile(2000, 100, 50) +
                            self.profile(3000, 48100, 48050, 1))
        result = audio.analyze(self.log)
        self.assertEqual(len(result['device_counter_resets']), 1)
        self.assertFalse(result['pcm_mapping_valid'])
        self.assertIsNone(result['windows'][0]['pcm_source_ranges'])
        self.assertEqual(result['windows'][0]['engine_glitches_observed'], 1)

    def test_old_or_invalid_device_records_leave_unknown_coverage(self):
        self.log.write_text('[AUDIO_PROFILE] qpc_ms=1000 window_ms=1000 queue_zero_observations=3\n' +
                            self.profile(2000, 96000, 95900) + self.profile(3000, 144000, 143900, valid=0) +
                            self.profile(4000, 192000, 191900))
        result = audio.analyze(self.log)
        self.assertIsNone(result['windows'][0]['engine_glitches_observed'])
        self.assertFalse(result['pcm_mapping_valid'])

    def test_window_contains_whole_delta_and_maps_submitted_source_samples(self):
        self.log.write_text(''.join(self.profile(end, end * 48, end * 48 - 100) for end in (1000, 2000, 3000, 4000)))
        result = audio.analyze(self.log, [{'name': 'driving', 'start': 1500, 'end': 3500}])['windows'][0]
        self.assertEqual(result['device_delta_intervals'], 1)
        self.assertEqual(result['device_delta_covered_ms'], 1000)
        self.assertEqual(result['pcm_source_ranges'], [[96000, 144000]])

    def test_partial_or_malformed_live_records_are_excluded(self):
        self.log.write_text(self.profile(1000, 48000, 47900) +
                            '[AUDIO_PROFILE] qpc_ms=nan window_ms=1000\n' +
                            self.profile(3000, 10, 11) + self.profile(4000, 192000, 191900).rstrip())
        result = audio.analyze(self.log)
        self.assertEqual(result['profile_input']['malformed_profile_lines'], [2, 3])
        self.assertTrue(result['profile_input']['partial_trailing_line_ignored'])
        self.assertEqual(result['windows'][0]['device_snapshot_count'], 1)
        json.dumps(result, allow_nan=False)

    def test_invalid_gap_still_detects_reset_but_never_bridges_counter_delta(self):
        self.log.write_text(self.profile(1000, 48000, 47900, 7) +
                            '[AUDIO_PROFILE] qpc_ms=nan window_ms=1000\n' +
                            self.profile(3000, 100, 50) + self.profile(4000, 48100, 48050, 1))
        report = audio.analyze(self.log)
        self.assertEqual(len(report['device_counter_resets']), 1)
        self.assertEqual(report['windows'][0]['device_delta_intervals'], 1)
        self.assertEqual(report['windows'][0]['engine_glitches_observed'], 1)

    def test_post_crash_profile_tail_is_excluded_without_mixing_uptime_and_qpc(self):
        self.log.write_text(self.profile(1000, 48000, 47900) +
                            '[CRASH] uptime_ms=999999 thread=10\n' + self.profile(2000, 96000, 95900, 5))
        result = audio.analyze(self.log, [])
        self.assertEqual(result['profile_input']['first_fatal_log_line'], 2)
        self.assertEqual(result['profile_input']['post_fatal_profiles_ignored'], 1)
        self.assertEqual(result['profile_summary']['device_snapshot_count'], 1)
        self.assertIsNone(result['profile_summary']['engine_glitches_observed'])
        self.assertEqual(result['windows'], [])

    def test_invalid_icall_cuts_off_profiles_even_without_crash_record(self):
        for marker in ('[ICALL] non-code target 0x12345678', '[ICALL] Failed resolve 0x12345678'):
            self.log.write_text(self.profile(1000, 48000, 47900) + marker + '\n' +
                                self.profile(2000, 96000, 95900, 5))
            result = audio.analyze(self.log, [{'name': 'driving', 'start': 0, 'end': 3000}])
            self.assertIsNone(result['profile_input']['first_fatal_log_line'])
            self.assertEqual(result['profile_input']['first_invalid_state_log_line'], 2)
            self.assertEqual(result['profile_input']['post_invalid_state_profiles_ignored'], 1)
            self.assertEqual(result['windows'][0]['device_snapshot_count'], 1)
            self.assertIsNone(result['windows'][0]['engine_glitches_observed'])

    def test_pcm_metrics_read_stereo_little_endian_and_keep_signal_labels(self):
        pcm = self.folder / 'host.pcm'
        pcm.write_bytes(struct.pack('<8h', 0, 0, 0, 0, 32767, -32768, -32768, 32767) + b'\x01')
        result = audio.pcm_metrics(pcm)
        self.assertEqual(result['analyzed_frames'], 4)
        self.assertEqual(result['fullscale_samples'], 4)
        self.assertEqual(result['peak_absolute_sample'], 32768)
        self.assertEqual(result['adjacent_channel_jumps_over_20000'], 4)
        self.assertEqual(result['longest_exact_silence_seconds'], 2 / 48000)
        self.assertEqual(result['incomplete_trailing_bytes_ignored'], 1)
        self.assertEqual(audio.pcm_metrics(pcm, [[2, 9]])['unavailable_frames'], 5)

    def test_pcm_comparison_excludes_incomplete_frames_and_reports_tail(self):
        premix, host = self.folder / 'premix.pcm', self.folder / 'host.pcm'
        premix.write_bytes(bytes(12) + b'\x12')
        host.write_bytes(bytes(8))
        result = audio.compare_pcm(premix, host)
        self.assertTrue(result['common_prefix_equal'])
        self.assertEqual(result['common_complete_frame_bytes'], 8)
        self.assertEqual(result['uncompared_complete_frame_bytes'], [4, 0])
        host.write_bytes(b'\x01' + bytes(7))
        self.assertEqual(audio.compare_pcm(premix, host)['mismatched_bytes'], 1)

    def test_declared_startup_silence_is_exactly_checked_and_excluded(self):
        premix, host = self.folder / 'premix.pcm', self.folder / 'host.pcm'
        source = struct.pack('<16h', *range(1, 17))
        premix.write_bytes(source)
        host.write_bytes(bytes(1280 * 4) + source)
        declaration = '[XA2] startup_padding_frames=1280 buffers=5 frame_samples=256\n'
        self.log.write_text(declaration + self.profile(1000, 1284, 1280) + self.profile(2000, 1288, 1284))
        result = audio.analyze(self.log, premix=premix, host=host)
        comparison = result['pcm_comparison']
        self.assertTrue(comparison['equal_after_declared_padding'])
        self.assertTrue(comparison['host_prefix_valid'])
        self.assertEqual(comparison['comparison_host_offset_bytes'], 5120)
        self.assertAlmostEqual(result['intentional_startup_latency_ms'], 1280 / 48)
        self.assertEqual(result['windows'][0]['pcm_source_ranges'], [[1284, 1288]])
        self.assertEqual(result['windows'][0]['premix_source_ranges'], [[4, 8]])
        self.assertEqual(result['windows'][0]['premix_pcm']['analyzed_frames'], 4)
        self.assertEqual(result['windows'][0]['host_pcm']['analyzed_frames'], 4)

    def test_undeclared_or_nonzero_prefix_is_never_ignored(self):
        premix, host = self.folder / 'premix.pcm', self.folder / 'host.pcm'
        source = struct.pack('<4h', 1, 2, 3, 4)
        premix.write_bytes(source)
        host.write_bytes(bytes(1280 * 4) + source)
        self.log.write_text('')
        self.assertFalse(audio.analyze(self.log, premix=premix, host=host)['pcm_comparison']['equal_after_declared_padding'])
        self.log.write_text('[XA2] startup_padding_frames=1280 buffers=5 frame_samples=256\n')
        host.write_bytes(bytes(5119) + b'\x01' + source)
        comparison = audio.analyze(self.log, premix=premix, host=host)['pcm_comparison']
        self.assertFalse(comparison['host_prefix_valid'])
        self.assertFalse(comparison['equal_after_declared_padding'])
        self.assertEqual(comparison['comparison_host_offset_bytes'], 0)

    def test_wrong_duplicate_or_midstream_padding_declaration_invalidates_comparison(self):
        premix, host = self.folder / 'premix.pcm', self.folder / 'host.pcm'
        source = struct.pack('<4h', 1, 2, 3, 4)
        premix.write_bytes(source)
        host.write_bytes(bytes(1280 * 4) + source)
        declaration = '[XA2] startup_padding_frames=1280 buffers=5 frame_samples=256\n'
        cases = ('[XA2] startup_padding_frames=1279 buffers=5 frame_samples=256\n',
                 '[XA2] startup_padding_frames=1280 buffers=4 frame_samples=320\n',
                 declaration + declaration,
                 self.profile(1000, 1282, 1280) + declaration)
        for lines in cases:
            with self.subTest(lines=lines):
                self.log.write_text(lines)
                result = audio.analyze(self.log, premix=premix, host=host)
                self.assertTrue(result['profile_input']['startup_padding_contract_errors'])
                self.assertFalse(result['pcm_comparison']['startup_padding_contract_valid'])
                self.assertFalse(result['pcm_comparison']['equal_after_declared_padding'])

    def test_explicit_disabled_padding_retains_byte_identical_comparison(self):
        premix, host = self.folder / 'premix.pcm', self.folder / 'host.pcm'
        source = struct.pack('<4h', 1, 2, 3, 4)
        premix.write_bytes(source)
        host.write_bytes(source)
        self.log.write_text('[XA2] startup_padding_frames=0 buffers=0 frame_samples=256\n')
        result = audio.analyze(self.log, premix=premix, host=host)
        self.assertTrue(result['pcm_comparison']['equal_after_declared_padding'])
        self.assertEqual(result['intentional_startup_latency_ms'], 0)


if __name__ == '__main__':
    unittest.main()
