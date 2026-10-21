"""Audit bounded AUDIO_PROFILE records and optional s16le stereo 48 kHz captures.

python tools/diagnostics/analyze_audio.py run/stderr.log --stress-report run/stress-report.json
    --premix premix.pcm --host host.pcm --output audio-analysis.json
Only complete log records and complete stereo PCM frames are read from a live snapshot.
"""
import argparse
from array import array
import hashlib
import json
import math
import os
from pathlib import Path
import re
import sys


RATE = 48000
FIELDS = re.compile(r'(\w+)=([^\s]+)')
FATAL = re.compile(r'\[CRASH(?:_[A-Z]+)*\]|\bFATAL\b|\bKeBugCheck\b|\bR6025\b|pure virtual function call', re.IGNORECASE)
INVALID_ICALL = re.compile(r'\[ICALL\]\s+(?:non-code target|Failed resolve)', re.IGNORECASE)
STARTUP_PADDING = re.compile(r'^\[XA2\] startup_padding_frames=(\d+) buffers=(\d+) frame_samples=(\d+)\s*$')
DEVICE = ('device_samples_submitted', 'device_samples_played', 'device_glitches_total')
NOTES = [
    'Queue-zero polls are observations, not an underrun or audible-corruption count.',
    'Engine glitches are the XAudio2 performance counter; zero does not establish continuous source-buffer supply, correct guest PCM or listening quality.',
    'Device snapshots occur near the report QPC endpoint. Counter deltas use only consecutive valid snapshots wholly inside a selected window.',
    'PCM offsets use submitted source samples, not wall-clock duration; mapping is disabled after an observed device counter reset.',
    'Clipping, large sample jumps and exact silence are signal statistics. Impacts, menus and pauses can cause them; none alone diagnoses corruption.',
    'Premix/host captures and tracing can perturb timing. Compare a separate run with light diagnostics for performance acceptance.',
    'Profile records after the first fatal or invalid-object ICALL log line are excluded. Raw whole-capture statistics can still include an unhealthy tail.',
    'Played-sample progress versus nominal 48 kHz wall time includes device-clock skew and snapshot/engine-quantum timing; a shortfall is not an exact audible-gap measurement.',
    'Premix/host equality excludes only an exact declared all-zero startup prefix: five 256-frame buffers add 26.667 ms once. Undeclared, nonzero or midstream padding is never silently ignored.',
]


def read_profiles(path):
    with Path(path).open('rb') as stream:
        data = stream.read(os.fstat(stream.fileno()).st_size)
    lines = data.decode('utf-8-sig', errors='replace').splitlines(keepends=True)
    partial = bool(lines and not lines[-1].endswith('\n'))
    if partial:
        lines.pop()
    records, malformed, gap = [], [], False
    fatal_line, invalid_line, after_fatal, after_invalid = None, None, 0, 0
    padding_frames, padding_line, padding_count, profile_seen = 0, None, 0, False
    padding_errors = []
    for line_number, line in enumerate(lines, 1):
        if fatal_line is None and FATAL.search(line):
            fatal_line = line_number
        if invalid_line is None and (FATAL.search(line) or INVALID_ICALL.search(line)):
            invalid_line = line_number
        if '[XA2] startup_padding_frames=' in line:
            padding_count += 1
            match = STARTUP_PADDING.fullmatch(line.strip())
            if padding_count > 1 or profile_seen or invalid_line is not None or not match:
                padding_errors.append({'line': line_number, 'reason': 'duplicate, malformed or midstream startup declaration'})
            else:
                frames, buffers, frame_samples = map(int, match.groups())
                if buffers not in (0, 5) or frame_samples != 256 or frames != buffers * frame_samples:
                    padding_errors.append({'line': line_number, 'reason': 'startup count must be 0 or five 256-frame buffers'})
                else:
                    padding_frames, padding_line = frames, line_number
        if '[AUDIO_PROFILE]' not in line:
            continue
        profile_seen = True
        if invalid_line is not None:
            after_invalid += 1
            after_fatal += fatal_line is not None
            continue
        try:
            fields = dict(FIELDS.findall(line.split('[AUDIO_PROFILE]', 1)[1]))
            values = {key: float(value) if '.' in value or 'e' in value.lower() else int(value)
                      for key, value in fields.items()}
            if any(not math.isfinite(value) or value < 0 for value in values.values()):
                raise ValueError('invalid numeric field')
            end, duration = values['qpc_ms'], values['window_ms']
            if duration <= 0 or end < duration:
                raise ValueError('invalid interval')
            valid = values.get('device_valid') == 1 and all(key in values for key in DEVICE)
            if valid and (any(not isinstance(values[key], int) for key in DEVICE) or
                          values['device_samples_played'] > values['device_samples_submitted']):
                raise ValueError('invalid device counters')
            records.append({'line': line_number, 'start': end - duration,
                            'end': end, 'values': values, 'device_valid': valid, 'gap_before': gap})
            gap = False
        except (KeyError, ValueError, OverflowError):
            malformed.append(line_number)
            gap = True
    return records, {'partial_trailing_line_ignored': partial, 'malformed_profile_lines': malformed,
                     'first_fatal_log_line': fatal_line, 'post_fatal_profiles_ignored': after_fatal,
                     'first_invalid_state_log_line': invalid_line,
                     'post_invalid_state_profiles_ignored': after_invalid,
                     'startup_padding_frames': padding_frames if not padding_errors else 0,
                     'startup_padding_line': padding_line,
                     'startup_padding_contract_errors': padding_errors}


def device_pairs(records):
    pairs, resets, previous, last_valid = [], [], None, None
    for record in records:
        if not record['device_valid']:
            previous = None
            continue
        after = record['values']
        reset = last_valid and (record['end'] <= last_valid['end'] or
                               any(after[key] < last_valid['values'][key] for key in DEVICE))
        if reset:
            resets.append({'previous_line': last_valid['line'], 'line': record['line']})
        elif previous and not record['gap_before']:
            pairs.append({'start': previous['end'], 'end': record['end'],
                          'before': previous['values'], 'after': after, 'line': record['line']})
        previous = last_valid = record
    return pairs, resets


def merge_ranges(ranges):
    merged = []
    for start, end in sorted(ranges):
        if start >= end:
            continue
        if merged and start <= merged[-1][1]:
            merged[-1][1] = max(merged[-1][1], end)
        else:
            merged.append([start, end])
    return merged


def summarize_window(records, pairs, window, mapping_valid):
    start, end = window['start'], window['end']
    selected = [row for row in records if start <= row['start'] and row['end'] <= end]
    measured = [pair for pair in pairs if start <= pair['start'] and pair['end'] <= end]
    valid = [row['values'] for row in records if row['device_valid'] and start <= row['end'] <= end]
    elapsed = sum(pair['end'] - pair['start'] for pair in measured)
    delta = lambda key: sum(pair['after'][key] - pair['before'][key] for pair in measured)
    result = {'name': window['name'], 'start_qpc_ms': start, 'end_qpc_ms': end,
              'profile_records_contained': len(selected), 'device_snapshot_count': len(valid),
              'device_delta_intervals': len(measured), 'device_delta_covered_ms': elapsed,
              'engine_glitches_observed': delta('device_glitches_total') if measured else None,
              'source_samples_submitted_delta': delta('device_samples_submitted') if measured else None,
              'source_samples_played_delta': delta('device_samples_played') if measured else None,
              'queue_zero_observations': sum(row['values'].get('queue_zero_observations', 0) for row in selected),
              'queue_full_observations': sum(row['values'].get('queue_full_observations', 0) for row in selected),
              'submit_failures': sum(row['values'].get('submit_failures', 0) for row in selected),
              'group_gap_max_ms': max((row['values'].get('group_gap_max_ms', 0) for row in selected), default=None),
              'deadline_late_max_ms': max((row['values'].get('deadline_late_max_ms', 0) for row in selected), default=None),
              'device_glitch_first_total': valid[0]['device_glitches_total'] if valid else None,
              'device_glitch_last_total': valid[-1]['device_glitches_total'] if valid else None}
    backlog = [(row['device_samples_submitted'] - row['device_samples_played']) / RATE * 1000
               for row in valid]
    result['source_backlog_min_ms'] = min(backlog, default=None)
    result['source_backlog_max_ms'] = max(backlog, default=None)
    shortfalls = [(pair['end'] - pair['start']) -
                  (pair['after']['device_samples_played'] - pair['before']['device_samples_played']) / RATE * 1000
                  for pair in measured]
    result['source_played_rate_hz'] = delta('device_samples_played') * 1000 / elapsed if elapsed else None
    result['source_progress_nominal_shortfall_ms'] = sum(shortfalls) if shortfalls else None
    result['source_progress_interval_shortfall_max_ms'] = max(shortfalls, default=None)
    result['pcm_source_ranges'] = merge_ranges(
        [(pair['before']['device_samples_submitted'], pair['after']['device_samples_submitted'])
         for pair in measured]) if mapping_valid else None
    return result


def compare_pcm(premix, host, host_prefix_frames=0):
    sizes = [Path(path).stat().st_size for path in (premix, host)]
    prefix_bytes = host_prefix_frames * 4
    prefix_valid = host_prefix_frames in (0, 1280)
    if prefix_valid:
        with Path(host).open('rb') as stream:
            prefix = stream.read(prefix_bytes)
        prefix_valid = len(prefix) == prefix_bytes and not any(prefix)
    offset = prefix_bytes if prefix_valid else 0
    effective_sizes = sizes[0], max(0, sizes[1] - offset)
    common = min(effective_sizes) // 4 * 4
    hashes = [hashlib.sha256(), hashlib.sha256()]
    mismatches = 0
    with Path(premix).open('rb') as left, Path(host).open('rb') as right:
        right.seek(offset)
        remaining = common
        while remaining:
            count = min(1024 * 1024, remaining)
            blocks = left.read(count), right.read(count)
            if any(len(block) != count for block in blocks):
                raise ValueError('PCM snapshot shrank during comparison')
            for digest, block in zip(hashes, blocks):
                digest.update(block)
            if blocks[0] != blocks[1]:
                mismatches += sum(a != b for a, b in zip(*blocks))
            remaining -= count
    return {'snapshot_bytes': sizes, 'common_complete_frame_bytes': common,
            'common_prefix_equal': mismatches == 0 and prefix_valid, 'mismatched_bytes': mismatches,
            'host_prefix_frames_declared': host_prefix_frames,
            'host_prefix_valid': prefix_valid, 'comparison_host_offset_bytes': offset,
            'intentional_startup_latency_ms': host_prefix_frames * 1000 / RATE,
            'equal_after_declared_padding': prefix_valid and mismatches == 0 and effective_sizes[0] == effective_sizes[1] and all(size % 4 == 0 for size in sizes),
            'common_prefix_sha256': [digest.hexdigest() for digest in hashes],
            'incomplete_trailing_bytes': [size % 4 for size in sizes],
            'uncompared_complete_frame_bytes': [size // 4 * 4 - common for size in effective_sizes]}


def pcm_metrics(path, ranges=None):
    snapshot_bytes = Path(path).stat().st_size
    available = snapshot_bytes // 4
    ranges = merge_ranges([[0, available]] if ranges is None else ranges)
    requested = sum(end - start for start, end in ranges)
    frames = clipped = jumps = silent = longest = current_silent = peak = total = squares = 0
    previous = None
    last_end = None
    with Path(path).open('rb') as stream:
        for start, end in ranges:
            if start != last_end:
                previous = None
                current_silent = 0
            stream.seek(min(start, available) * 4)
            remaining = max(0, min(end, available) - start)
            while remaining:
                count = min(4800, remaining)
                block = stream.read(count * 4)
                if len(block) != count * 4:
                    raise ValueError('PCM snapshot shrank during analysis')
                samples = array('h', block)
                if sys.byteorder != 'little':
                    samples.byteswap()
                for index in range(0, len(samples), 2):
                    pair = samples[index], samples[index + 1]
                    is_silent = pair == (0, 0)
                    silent += is_silent
                    current_silent = current_silent + 1 if is_silent else 0
                    longest = max(longest, current_silent)
                    for channel, value in enumerate(pair):
                        peak = max(peak, abs(value))
                        clipped += value == -32768 or value == 32767
                        squares += value * value
                        total += value
                        if previous is not None:
                            jumps += abs(value - previous[channel]) > 20000
                    previous = pair
                frames += count
                remaining -= count
            last_end = end
    return {'requested_frames': requested, 'analyzed_frames': frames,
            'unavailable_frames': requested - frames, 'duration_seconds': frames / RATE,
            'peak_absolute_sample': peak, 'fullscale_samples': clipped,
            'fullscale_sample_fraction': clipped / (frames * 2) if frames else None,
            'rms': math.sqrt(squares / (frames * 2)) if frames else None,
            'mean_sample': total / (frames * 2) if frames else None,
            'adjacent_channel_jumps_over_20000': jumps, 'exact_silent_frames': silent,
            'longest_exact_silence_seconds': longest / RATE,
            'incomplete_trailing_bytes_ignored': snapshot_bytes % 4}


def analyze(log, windows=None, premix=None, host=None):
    records, info = read_profiles(log)
    pairs, resets = device_pairs(records)
    mapping_valid = bool(pairs) and not resets
    if windows is None:
        windows = [{'name': 'whole_log', 'start': 0, 'end': records[-1]['end'] if records else 0}]
    for window in windows:
        if (not all(math.isfinite(window[key]) and window[key] >= 0 for key in ('start', 'end')) or
                window['end'] < window['start']):
            raise ValueError('Invalid QPC window')
    reports = [summarize_window(records, pairs, window, mapping_valid) for window in windows]
    overall = {'name': 'whole_log_before_invalid_state', 'start': 0, 'end': records[-1]['end'] if records else 0}
    result = {'log': str(log), 'profile_input': info, 'device_counter_resets': resets,
              'profile_summary': summarize_window(records, pairs, overall, mapping_valid),
              'pcm_mapping_valid': mapping_valid, 'windows': reports, 'notes': NOTES}
    if premix and host:
        result['pcm_comparison'] = compare_pcm(premix, host, info['startup_padding_frames'])
        result['pcm_comparison']['startup_padding_contract_valid'] = not info['startup_padding_contract_errors']
        if info['startup_padding_contract_errors']:
            result['pcm_comparison']['common_prefix_equal'] = False
            result['pcm_comparison']['equal_after_declared_padding'] = False
    result['intentional_startup_latency_ms'] = info['startup_padding_frames'] * 1000 / RATE
    for name, path in (('premix', premix), ('host', host)):
        if path:
            result[name + '_whole_capture'] = pcm_metrics(path)
            for report in reports:
                ranges = report['pcm_source_ranges']
                if name == 'premix' and ranges is not None:
                    if info['startup_padding_contract_errors'] or (premix and host and not result['pcm_comparison']['host_prefix_valid']):
                        ranges = None
                    else:
                        padding = info['startup_padding_frames']
                        ranges = merge_ranges([(max(0, start - padding), max(0, end - padding)) for start, end in ranges])
                    report['premix_source_ranges'] = ranges
                if ranges is not None:
                    report[name + '_pcm'] = pcm_metrics(path, ranges)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--stress-report', type=Path)
    parser.add_argument('--premix', type=Path)
    parser.add_argument('--host', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    windows = json.loads(args.stress_report.read_text(encoding='utf-8-sig'))['windows'] if args.stress_report else None
    report = analyze(args.log, windows, args.premix, args.host)
    rendered = json.dumps(report, indent=2, allow_nan=False) + '\n'
    if args.output:
        args.output.write_text(rendered, encoding='utf-8')
        print(args.output)
    else:
        print(rendered, end='')


if __name__ == '__main__':
    main()
