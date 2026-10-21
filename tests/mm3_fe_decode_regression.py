"""Compile real FE publisher, force contention and feed its pairs to raw SDK.

All compiler outputs and mutated negative controls are temporary. No main game
build, running game, save data or audio capture is accessed.
"""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
APU = ROOT / 'tools/xboxrecomp/src/apu'


def main():
    if not shutil.which('cl'):
        raise SystemExit('Run scripts/enter-env.ps1 first.')
    source = (APU / 'apu_fe_decode.h').read_text()
    anchor = '*decode_method = method;'
    assert source.count(anchor) == 1
    gated = source.replace(anchor, 'test_publish_gate(method, argument);\n        ' + anchor, 1)
    predicate = '''if (result.previous_mode != NV_PAPU_FECTL_FEMETHMODE_TRAPPED &&
        (method != SE2FE_IDLE_VOICE ||
         result.previous_mode == NV_PAPU_FECTL_FEMETHMODE_FREE_RUNNING))'''
    assert gated.count(predicate) == 1
    locked = '''    while (InterlockedCompareExchange(&publication->lock, 1, 0) != 0) {
        YieldProcessor();
    }'''
    assert gated.count(locked) == 1
    no_lock = gated.replace(locked, '    (void)publication;', 1).replace(
        '    InterlockedExchange(&publication->lock, 0);', '    (void)publication;', 1)
    controls = {
        'unfixed_pair_overwrite': gated.replace(predicate, 'if (1)', 1),
        'unserialized_first_publisher': no_lock,
        'idle_allowed_during_HALTED_ack': gated.replace(predicate,
            'if (result.previous_mode != NV_PAPU_FECTL_FEMETHMODE_TRAPPED)', 1),
    }
    results = {}
    with tempfile.TemporaryDirectory(prefix='mm3-fe-decode-') as temporary:
        folder = Path(temporary)

        def build(header, name):
            (folder / 'fe_decode_under_test.h').write_text(header)
            executable = folder / (name + '.exe')
            subprocess.run([
                'cl', '/nologo', '/w', '/O2', '/Ob1', '/std:c11',
                '/D_CRT_SECURE_NO_WARNINGS', '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX',
                '/I' + str(folder), '/I' + str(APU),
                str(ROOT / 'tests/mm3_fe_decode_contract.c'),
                '/Fo:' + str(folder / (name + '.obj')), '/Fe:' + str(executable),
            ], cwd=folder, check=True)
            return executable

        pairs = folder / 'production-pairs.bin'
        executable = build(gated, 'production')
        subprocess.run([str(executable), str(pairs)], check=True)
        pair_hash = hashlib.sha256(pairs.read_bytes()).hexdigest()
        subprocess.run([sys.executable, str(ROOT / 'tests/mm3_idle_trap_interleaving.py'),
                        '--production-pairs', str(pairs)], check=True)
        for name, control in controls.items():
            executable = build(control, name)
            run = subprocess.run([str(executable)], text=True, capture_output=True)
            assert run.returncode == 1 and 'FAIL FE pair line' in run.stdout, (name, run.returncode, run.stdout)
            results[name] = {'exit_code': run.returncode, 'failure': run.stdout.strip()}
            print('PASS negative control:', name, run.stdout.strip())
    receipt = {
        'status': 'PASS_BOUNDED_PRODUCTION_FE_PAIR_REGRESSION',
        'source': 'tools/xboxrecomp/src/apu/apu_fe_decode.h',
        'source_sha256': hashlib.sha256((APU / 'apu_fe_decode.h').read_bytes()).hexdigest(),
        'fixture_sha256': {name: hashlib.sha256((ROOT / 'tests' / name).read_bytes()).hexdigest()
                           for name in ('mm3_fe_decode_contract.c', 'mm3_fe_decode_regression.py',
                                        'mm3_idle_trap_interleaving.py')},
        'configuration': 'MSVC x64 /O2 /Ob1 /std:c11 precise default FP',
        'production_helper': 'Actual header compiled unchanged except a private test gate before decoder-method store. No production test hooks added.',
        'coverage': ['First idle publishes pair and TRAPPED atomically',
                     'Ordinary methods and repeated idle reports preserve pair while trapped',
                     'ISTS acknowledgement does not release pair',
                     'HALTED defers new idle until FREE; ordinary method behavior continues',
                     'Controlled ordinary writer holding publish lock blocks idle publisher',
                     'Controlled ordinary writer holding publish lock blocks FECTL mode writer',
                     'Host errno, LastError, fenv and MXCSR preserved'],
        'raw_sdk_receipt': 'logs/dc-smoothness-pass3-20261019/audio-idle-trap-interleaving.json',
        'production_pair_record_sha256': pair_hash,
        'negative_controls': results,
        'temporary_builds': 'Production/control executables, objects, header copies and pair records removed by TemporaryDirectory.',
        'limits': 'Atomic pending decoder metadata only. Ordinary method side effects continue and full hardware FE FIFO remains unimplemented. Native counterexamples demonstrate a defect but do not by themselves attribute the long captured fade.',
    }
    target = ROOT / 'logs/dc-smoothness-pass3-20261019/audio-fe-decode-validation.json'
    target.write_text(json.dumps(receipt, indent=2) + '\n')
    print(target.relative_to(ROOT))


if __name__ == '__main__':
    main()
