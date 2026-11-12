"""Compare per-frame timing files (RECOMP_FRAME_CSV) of the same race on two renderers.

  python tools/bench/compare_frames.py logs/bench-dx11.csv logs/bench-vulkan.csv [seconds]

Stats cover the last `seconds` (default 120) of each file minus its final second (the race, then closing the game).
"""
import csv
import sys

secs = float(sys.argv[3]) if len(sys.argv) > 3 else 120.0
for path in sys.argv[1:3]:
    rows = list(csv.DictReader(open(path)))
    t = [float(r['qpc_ms']) for r in rows]
    f = [float(r['frame_ms']) for r in rows]
    end = t[-1]
    race = [(q, x) for q, x in zip(t, f) if end - secs * 1000 <= q <= end - 1000]
    if len(race) < 100:
        print(f'{path}: too few frames ({len(race)})')
        continue
    ft = sorted(x for _, x in race)
    pick = lambda p: ft[min(len(ft) - 1, int(p * len(ft)))]
    med = pick(0.5)
    print(f'{path}: {len(race)} frames, {1000 * len(race) / sum(ft):.1f} fps | p50 {med:.2f} p99 {pick(.99):.2f} '
          f'p99.9 {pick(.999):.2f} worst {ft[-1]:.1f} ms | >2x median {sum(x > 2 * med for x in ft)}, '
          f'>4x {sum(x > 4 * med for x in ft)}')
    worst = sorted(race, key=lambda r: -r[1])[:8]
    print('  worst frames (seconds before end, ms):', ', '.join(f'{(end - q) / 1000:.0f}s {x:.0f}ms' for q, x in sorted(worst)))
