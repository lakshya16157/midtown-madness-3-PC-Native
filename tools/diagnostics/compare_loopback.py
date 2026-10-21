"""Offline comparison of submitted PCM and process-only Windows loopback.

Requires numpy/scipy. Run only after the timed game/capture have stopped.
Capture packet timestamps select actual stress windows; waveform correlation
aligns source samples independently of wall-clock startup/endpoint latency.
Candidate intervals require review and are never labelled audible corruption.
"""
import argparse
import csv
import gzip
import hashlib
import json
from pathlib import Path
import wave

import numpy as np
from scipy.signal import correlate

RATE = 48000
LIMITS = [
    'Process loopback captures only the target process tree Windows render stream; it is not a microphone/speaker recording or proof of physical endpoint quality.',
    'Windows gain, mixing and sample conversion can change samples. Correlation/gain/residual comparison is used instead of requiring byte equality.',
    'Packet discontinuity/timestamp flags can indicate capture faults; a flagged or missing capture packet alone does not establish an audible game fault.',
    'Low-confidence alignment, native silence, clipped impacts and user/application volume changes can produce candidates. Inspect their context before attributing corruption.',
    'Bounded local alignment follows small clock drift. A large inserted/deleted interval may leave later alignment uncertain; it is reported rather than silently treated as clean.',
    'Metrics cover only complete analysis blocks wholly inside timestamped selected windows. They do not establish listening quality or rule out rarer events.',
]


def normalized_match(search, query):
    """Best positive Pearson correlation, without an O(N*window) search."""
    if len(search) < len(query) or len(query) < 2:
        return None, None
    search = np.asarray(search, dtype=np.float64)
    query = np.asarray(query, dtype=np.float64)
    query = query - query.mean()
    power = np.dot(query, query)
    if power <= 1e-10:
        return None, None
    count = len(query)
    sums = np.r_[0., np.cumsum(search)]
    squares = np.r_[0., np.cumsum(search * search)]
    variance = squares[count:] - squares[:-count] - (sums[count:] - sums[:-count]) ** 2 / count
    numerator = correlate(search, query, mode='valid', method='fft')
    scores = np.full(len(numerator), -1.)
    valid = variance > 1e-8
    scores[valid] = numerator[valid] / np.sqrt(variance[valid] * power)
    index = int(np.argmax(scores))
    return index, float(np.clip(scores[index], -1., 1.))


def packet_time(packets, offset, endpoint=False):
    if endpoint and offset:
        value = packet_time(packets, offset - 1)
        return None if value is None else value + 1000 / RATE
    starts = packets['sample_offset']
    index = int(np.searchsorted(starts, offset, side='right')) - 1
    if index < 0 or offset >= starts[index] + packets['frames'][index]:
        return None
    if packets['flags'][index] & 4 or packets['qpc_100ns'][index] <= 0:
        return None
    return float(packets['qpc_100ns'][index]) / 10000 + (offset - int(starts[index])) * 1000 / RATE


def merge_candidates(blocks):
    result = []
    for block in blocks:
        if not block['candidate']:
            continue
        if result and block['capture_start_frame'] == result[-1]['capture_end_frame']:
            item = result[-1]
            item['capture_end_frame'] = block['capture_end_frame']
            item['end_qpc_ms'] = block['end_qpc_ms']
            item['reasons'] = sorted(set(item['reasons'] + [block['candidate']]))
            item['capture_warning'] |= block['capture_warning']
        else:
            result.append({'capture_start_frame': block['capture_start_frame'],
                           'capture_end_frame': block['capture_end_frame'],
                           'start_qpc_ms': block['start_qpc_ms'], 'end_qpc_ms': block['end_qpc_ms'],
                           'reasons': [block['candidate']], 'capture_warning': block['capture_warning']})
    for item in result:
        item['captured_duration_seconds'] = (item['capture_end_frame'] - item['capture_start_frame']) / RATE
    return result


def compare_arrays(source, captured, packets, windows, block_frames=12000, minimum_rms=100):
    if source.ndim != 2 or captured.ndim != 2 or source.shape[1] != 2 or captured.shape[1] != 2:
        raise ValueError('Expected stereo samples')
    starts = packets['sample_offset']
    if len(starts) == 0 or starts[0] != 0 or np.any(starts[1:] != starts[:-1] + packets['frames'][:-1]):
        raise ValueError('Packet offsets are not a complete contiguous capture index')
    if int(starts[-1] + packets['frames'][-1]) != len(captured):
        raise ValueError('Packet frame count does not match captured WAV')
    time_gaps = []
    for index in range(1, len(starts)):
        if (packets['flags'][index] | packets['flags'][index - 1]) & 4:
            continue
        before, after = int(packets['qpc_100ns'][index - 1]), int(packets['qpc_100ns'][index])
        if before <= 0 or after <= 0:
            continue
        gap_ms = (after - before) / 10000 - int(packets['frames'][index - 1]) * 1000 / RATE
        if abs(gap_ms) > 2:
            time_gaps.append({'packet': index, 'capture_offset': int(starts[index]),
                              'qpc_ms': after / 10000, 'gap_relative_to_frames_ms': gap_ms})
    if not windows:
        first = packet_time(packets, 0)
        last = packet_time(packets, len(captured), endpoint=True)
        if first is None or last is None:
            raise ValueError('Explicit windows needed when boundary timestamps are invalid')
        windows = [{'name': 'whole_capture', 'start': first, 'end': last}]
    selected = []
    for start in range(0, len(captured) - block_frames + 1, block_frames):
        end = start + block_frames
        first, last = packet_time(packets, start), packet_time(packets, end, endpoint=True)
        if first is None or last is None:
            continue
        names = [w['name'] for w in windows if w['start'] <= first and last <= w['end']]
        if names:
            selected.append((start, end, first, last, names))
    anchors = []
    for start, end, first, last, names in selected:
        if start + RATE > len(captured):
            continue
        query = captured[start:start + RATE, 0].astype(np.float64)
        rms = float(np.sqrt(np.mean(query * query)))
        if rms >= minimum_rms:
            anchors.append((rms, start))
    # Three energetic independent anchors help reject silence/corruption as
    # the sole alignment seed. Searching is offline, never during gameplay.
    chosen = []
    for rms, start in sorted(anchors, reverse=True):
        if all(abs(start - old) >= RATE * 3 for old in chosen):
            chosen.append(start)
            if len(chosen) == 3:
                break
    alignments = []
    for start in chosen:
        index, score = normalized_match(source[::8, 0], captured[start:start + RATE:8, 0])
        if index is not None:
            alignments.append({'capture_frame': start, 'source_frame': index * 8,
                               'source_minus_capture_frames': index * 8 - start, 'correlation': score})
    if not alignments or max(a['correlation'] for a in alignments) < .75:
        return {'alignment_status': 'unresolved', 'alignment_anchors': alignments,
                'reason': 'No sufficiently correlated energetic source/capture anchor; no quality pass is inferred.',
                'windows': [], 'notes': LIMITS}
    seed = max(alignments, key=lambda row: row['correlation'])
    offset = seed['source_minus_capture_frames']
    initial_offset = offset
    blocks = []
    for start, end, first, last, names in selected:
        expected = start + offset
        lower, upper = max(0, expected - 4800), min(len(source), expected + block_frames + 4800)
        query = captured[start:end, 0]
        index, score = normalized_match(source[lower:upper, 0], query)
        match = lower + index if index is not None and score >= .8 else expected
        if match < 0 or match + block_frames > len(source):
            continue
        if index is not None and score >= .8:
            offset = match - start
        original = source[match:match + block_frames].astype(np.float64)
        observed = captured[start:end].astype(np.float64)
        source_rms = float(np.sqrt(np.mean(original * original)))
        capture_rms = float(np.sqrt(np.mean(observed * observed)))
        energy = float(np.sum(original * original))
        gain = float(np.sum(original * observed) / energy) if energy > 0 else 0.
        residual = float(np.sqrt(np.mean((observed - gain * original) ** 2)))
        relative_residual = residual / max(capture_rms, 1.)
        candidate = None
        if source_rms >= minimum_rms:
            if capture_rms < max(8., source_rms * .02):
                candidate = 'capture_silence_with_energetic_source'
            elif score is None or score < .8:
                candidate = 'waveform_or_alignment_mismatch'
            elif relative_residual > .2:
                candidate = 'large_gain_fitted_waveform_residual'
        elif (capture_rms >= max(minimum_rms, source_rms * 4) and
              (score is None or score < .8 or relative_residual > .2)):
            # A corrupted burst can replace a quiet source. Do not exempt it
            # merely because there is little expected source energy. Small
            # conversion/dither and a correlated gain increase remain excluded.
            candidate = 'capture_noise_with_quiet_source'
        overlap = (starts < end) & (starts + packets['frames'] > start)
        flags = packets['flags'][overlap]
        block = {'capture_start_frame': start, 'capture_end_frame': end,
                 'start_qpc_ms': first, 'end_qpc_ms': last, 'windows': names,
                 'matched_source_frame': match, 'source_minus_capture_frames': match - start,
                 'correlation': score, 'source_rms': source_rms, 'capture_rms': capture_rms,
                 'fitted_gain': gain, 'relative_residual': relative_residual,
                 'capture_warning': bool(np.any(flags & 5)) or any(start <= gap['capture_offset'] < end for gap in time_gaps),
                 'candidate': candidate}
        blocks.append(block)
    reports = []
    for window in windows:
        rows = [b for b in blocks if window['name'] in b['windows']]
        reports.append({'name': window['name'], 'start_qpc_ms': window['start'], 'end_qpc_ms': window['end'],
                        'compared_blocks': len(rows), 'compared_capture_seconds': len(rows) * block_frames / RATE,
                        'candidate_blocks': sum(b['candidate'] is not None for b in rows),
                        'minimum_block_correlation': min((b['correlation'] for b in rows if b['correlation'] is not None), default=None),
                        'maximum_relative_residual': max((b['relative_residual'] for b in rows), default=None),
                        'capture_warning_blocks': sum(b['capture_warning'] for b in rows),
                        'packet_qpc_gap_events': [gap for gap in time_gaps if window['start'] <= gap['qpc_ms'] <= window['end']],
                        'candidate_intervals': merge_candidates(rows)})
    device_gaps = []
    # This Windows process-loopback endpoint returns zero device positions
    # while supplying valid per-packet QPC timestamps. Constant zero is absent
    # position data, not thousands of negative device jumps.
    position_available = bool(np.any(packets['device_position']))
    if position_available:
        for index in range(1, len(starts)):
            gap = int(packets['device_position'][index]) - int(packets['device_position'][index - 1] + packets['frames'][index - 1])
            if gap:
                device_gaps.append({'packet': index, 'capture_offset': int(starts[index]), 'device_frame_gap': gap,
                                    'flags': int(packets['flags'][index])})
    return {'alignment_status': 'matched', 'alignment_anchors': alignments,
            'initial_source_minus_capture_frames': initial_offset, 'block_frames': block_frames,
            'minimum_source_rms_for_candidates': minimum_rms, 'windows': reports,
            'packet_device_position_status': 'available' if position_available else 'unavailable_constant_zero',
            'packet_qpc_gap_events': time_gaps,
            'packet_device_position_gaps': device_gaps, 'blocks': blocks, 'notes': LIMITS}


def analyze(source_path, wav_path, packet_path, receipt_path, stress_report=None):
    receipt = json.loads(Path(receipt_path).read_text(encoding='utf-8-sig'))
    if receipt.get('scope') != 'include_target_process_tree_only' or receipt.get('system_loopback_fallback') is not False:
        raise ValueError('Capture receipt does not establish process-only scope')
    if receipt.get('sample_rate') != RATE or receipt.get('channels') != 2 or receipt.get('bits_per_sample') != 16:
        raise ValueError('Capture must be stereo s16le48000 Hz')
    if receipt.get('capture_hresult') != 0 or receipt.get('stop_hresult') != 0 or receipt.get('data_written') is not True:
        raise ValueError('Capture did not complete successfully; preserve it as incomplete evidence')
    with wave.open(str(wav_path), 'rb') as stream:
        if (stream.getnchannels(), stream.getsampwidth(), stream.getframerate(), stream.getcomptype()) != (2, 2, RATE, 'NONE'):
            raise ValueError('Unexpected WAV format')
        captured = np.frombuffer(stream.readframes(stream.getnframes()), dtype='<i2').reshape(-1, 2)
    opener = gzip.open if str(source_path).endswith('.gz') else open
    with opener(source_path, 'rb') as stream:
        raw = stream.read()
    if len(raw) % 4:
        raise ValueError('Source PCM ends inside a complete stereo frame')
    source = np.frombuffer(raw, dtype='<i2').reshape(-1, 2)
    with Path(packet_path).open(newline='', encoding='utf-8') as stream:
        rows = list(csv.DictReader(stream))
    packets = {key: np.array([int(row[key]) for row in rows], dtype=np.int64)
               for key in ('sample_offset', 'frames', 'flags', 'device_position', 'qpc_100ns')}
    windows = json.loads(Path(stress_report).read_text(encoding='utf-8-sig'))['windows'] if stress_report else None
    result = compare_arrays(source, captured, packets, windows)
    result.update({'source': str(source_path), 'loopback_wav': str(wav_path), 'capture_receipt': receipt,
                   'source_pcm_bytes': len(raw), 'source_pcm_sha256': hashlib.sha256(raw).hexdigest(),
                   'loopback_pcm_frames': len(captured), 'loopback_pcm_sha256': hashlib.sha256(captured.tobytes()).hexdigest()})
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--loopback', required=True, type=Path)
    parser.add_argument('--packets', required=True, type=Path)
    parser.add_argument('--receipt', required=True, type=Path)
    parser.add_argument('--stress-report', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    result = analyze(args.source, args.loopback, args.packets, args.receipt, args.stress_report)
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False) + '\n', encoding='utf-8')
    print(args.output)


if __name__ == '__main__':
    main()
