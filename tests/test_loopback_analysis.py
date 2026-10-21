import sys
from pathlib import Path
import unittest
import numpy as np
from scipy.signal import lfilter
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/diagnostics'))
import compare_loopback as analyzer


class LoopbackAnalysis(unittest.TestCase):
    def setUp(self):
        rng = np.random.default_rng(921)
        left = lfilter([1.], [1., -.82], rng.normal(0, 1200, 48000 * 8))
        right = lfilter([1.], [1., -.7], rng.normal(0, 900, len(left)))
        self.source = np.column_stack((left, right)).astype(np.int16)
        self.padding = 9600
        self.captured = np.vstack((np.zeros((self.padding, 2)), self.source * .4)).astype(np.int16)
        offsets = np.arange(0, len(self.captured), 480, dtype=np.int64)
        self.packets = {'sample_offset': offsets, 'frames': np.full(len(offsets), 480),
                        'flags': np.zeros(len(offsets), dtype=np.int64), 'device_position': offsets.copy(),
                        'qpc_100ns': 10000000 + offsets * 10000000 // 48000}
        self.windows = [{'name': 'drive', 'start': 1500., 'end': 8800.}]

    def compare(self):
        return analyzer.compare_arrays(self.source, self.captured, self.packets, self.windows)

    def test_gain_and_start_delay(self):
        result = self.compare()
        self.assertEqual(result['alignment_status'], 'matched')
        self.assertAlmostEqual(result['initial_source_minus_capture_frames'], -self.padding, delta=8)
        self.assertEqual(result['windows'][0]['candidate_blocks'], 0)
        self.assertGreater(result['windows'][0]['minimum_block_correlation'], .999)

    def test_one_and_half_second_silence_not_called_clean(self):
        self.captured[48000 * 3:48000 * 9 // 2] = 0
        result = self.compare()
        intervals = result['windows'][0]['candidate_intervals']
        silent = [r for r in intervals if 'capture_silence_with_energetic_source' in r['reasons']]
        self.assertTrue(silent)
        self.assertAlmostEqual(silent[0]['captured_duration_seconds'], 1.5)

    def test_distorted_second_and_capture_fault_are_visible(self):
        rng = np.random.default_rng(2)
        self.captured[48000 * 4:48000 * 5] = rng.integers(-5000, 5000, (48000, 2), dtype=np.int16)
        self.packets['flags'][400] = 1
        self.packets['device_position'][400:] += 480
        result = self.compare()
        self.assertGreaterEqual(result['windows'][0]['candidate_blocks'], 4)
        self.assertTrue(any(r['capture_warning'] for r in result['windows'][0]['candidate_intervals']))
        self.assertEqual(result['packet_device_position_gaps'][0]['device_frame_gap'], 480)

    def test_one_and_half_second_render_buzz_is_visible(self):
        start, end = 48000 * 3, 48000 * 9 // 2
        phase = np.arange(end - start) * (2 * np.pi * 3200 / 48000)
        buzz = np.rint(2500 * np.sin(phase)).astype(np.int32)
        mixed = self.captured[start:end].astype(np.int32) + buzz[:, None]
        self.captured[start:end] = np.clip(mixed, -32768, 32767).astype(np.int16)
        result = self.compare()
        self.assertGreaterEqual(result['windows'][0]['candidate_blocks'], 4)
        self.assertTrue(any(r['captured_duration_seconds'] >= 1.
                            for r in result['windows'][0]['candidate_intervals']))

    def test_one_and_half_second_render_click_train_is_visible(self):
        start, end = 48000 * 3, 48000 * 9 // 2
        mixed = self.captured[start:end].astype(np.int32)
        mixed[::480] += np.array([15000, -15000], dtype=np.int32)
        self.captured[start:end] = np.clip(mixed, -32768, 32767).astype(np.int16)
        result = self.compare()
        self.assertGreaterEqual(result['windows'][0]['candidate_blocks'], 4)
        self.assertTrue(any(r['captured_duration_seconds'] >= 1.
                            for r in result['windows'][0]['candidate_intervals']))

    def test_buzz_already_in_source_is_outside_transport_verdict(self):
        start, end = 48000 * 3, 48000 * 9 // 2
        phase = np.arange(end - start) * (2 * np.pi * 3200 / 48000)
        buzz = np.rint(6000 * np.sin(phase)).astype(np.int32)
        mixed = self.source[start:end].astype(np.int32) + buzz[:, None]
        self.source[start:end] = np.clip(mixed, -32768, 32767).astype(np.int16)
        self.captured = np.vstack((np.zeros((self.padding, 2)), self.source * .4)).astype(np.int16)
        # A faithful render can contain source-generated corruption. Matching
        # transport is deliberately not a source audio-quality verdict.
        self.assertEqual(self.compare()['windows'][0]['candidate_blocks'], 0)

    def test_loud_noise_replacing_quiet_source_is_visible(self):
        start, end = 48000 * 3, 48000 * 9 // 2
        self.source[start - self.padding:end - self.padding] = 0
        self.captured[start:end] = np.random.default_rng(83).integers(
            -5000, 5000, (end - start, 2), dtype=np.int16)
        result = self.compare()
        noisy = [r for r in result['windows'][0]['candidate_intervals']
                 if 'capture_noise_with_quiet_source' in r['reasons']]
        self.assertTrue(noisy)
        self.assertAlmostEqual(noisy[0]['captured_duration_seconds'], 1.5)

    def test_quiet_source_with_low_level_dither_is_not_a_noise_burst(self):
        start, end = 48000 * 3, 48000 * 9 // 2
        self.source[start - self.padding:end - self.padding] = 0
        self.captured[start:end] = np.random.default_rng(84).integers(
            -1, 2, (end - start, 2), dtype=np.int16)
        self.assertEqual(self.compare()['windows'][0]['candidate_blocks'], 0)

    def test_correlated_quiet_source_with_large_gain_is_not_noise(self):
        start, end = 48000 * 3, 48000 * 9 // 2
        quiet = (self.source[start - self.padding:end - self.padding] * .01).astype(np.int16)
        self.source[start - self.padding:end - self.padding] = quiet
        self.captured[start:end] = quiet * 12
        result = self.compare()
        self.assertEqual(result['windows'][0]['candidate_blocks'], 0)
        boosted = [b for b in result['blocks'] if start <= b['capture_start_frame'] < end]
        self.assertTrue(all(b['source_rms'] < 100 and b['capture_rms'] >= 100 for b in boosted))
        self.assertTrue(all(b['fitted_gain'] > 4 for b in boosted))

    def test_no_signal_does_not_infer_quality_pass(self):
        self.captured[:] = 0
        self.assertEqual(self.compare()['alignment_status'], 'unresolved')

    def test_constant_zero_device_positions_are_not_invented_gaps(self):
        self.packets['device_position'][:] = 0
        result = self.compare()
        self.assertEqual(result['packet_device_position_status'], 'unavailable_constant_zero')
        self.assertEqual(result['packet_device_position_gaps'], [])
        self.assertEqual(result['windows'][0]['candidate_blocks'], 0)

    def test_qpc_jump_is_a_capture_timing_warning(self):
        self.packets['qpc_100ns'][400:] += 1000000
        result = self.compare()
        self.assertEqual(len(result['packet_qpc_gap_events']), 1)
        self.assertAlmostEqual(result['packet_qpc_gap_events'][0]['gap_relative_to_frames_ms'], 100.)
        self.assertGreater(result['windows'][0]['capture_warning_blocks'], 0)

    def test_truncated_packet_receipt_rejected(self):
        self.packets['frames'][-1] -= 1
        with self.assertRaisesRegex(ValueError, 'frame count'):
            self.compare()


if __name__ == '__main__':
    unittest.main()
