"""Check scalar masked visibility against generated and original XBE code.

Run after scripts/enter-env.ps1. --benchmark measures only this leaf, not FPS.
The complete generated fallback is fingerprinted by the production CMake seam;
fixtures verify fail-closed regeneration and preserve the diagnostics wrapper.
All executable/object files and raw cases live in one temporary directory.
"""
import argparse
import hashlib
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile

import mm3_x87_exp_regression as common

ROOT = common.ROOT
BASE = 0x10000
ENTRY, RETURN = 0x1C7B15, 0x600000
REGS = ('EAX', 'ECX', 'EDX', 'ESP', 'EBX', 'ESI', 'EDI', 'EBP')


def seam_fixture(folder, body):
    source, output, script = (folder / name for name in ('input.c', 'output.c', 'seam.cmake'))
    script.write_text(
        f'include("{(ROOT / "scripts/mm3_masked_visibility_seam.cmake").as_posix()}")\n'
        f'file(READ "{source.as_posix()}" content)\n'
        'mm3_keep_masked_visibility_fallback(content)\n'
        f'file(WRITE "{output.as_posix()}" "${{content}}")\n')
    hooked = body.replace('void sub_001C7B15(void)', 'void sub_001C7B15_original(void)', 1)
    neighbors = '\nvoid neighbor(void) { sub_001C7B15(); sub_001C7B15_original(); }\n'
    for newline in ('\n', '\r\n'):
        value = (hooked + neighbors).replace('\n', newline)
        source.write_bytes(value.encode())
        subprocess.run(['cmake', '-P', str(script)], check=True, capture_output=True)
        expected = value.replace('void sub_001C7B15_original(void)',
                                 'void sub_001C7B15_generated(void)', 1)
        # CMake file(READ) normalizes CRLF on this host; the contract and every
        # neighboring token must remain identical after newline normalization.
        assert output.read_bytes().decode().replace('\r\n', '\n') == expected.replace('\r\n', '\n')
    negative = {
        'missing': neighbors,
        'duplicate': hooked + '\n' + hooked,
        'already_applied': hooked.replace('void sub_001C7B15_original(void)',
                                          'void sub_001C7B15_generated(void)', 1),
        'arithmetic': hooked.replace('fp_st1() + fp_top()', 'fp_st1() - fp_top()', 1),
        'precision': hooked.replace('RECOMP_FP_PC(fp_top() * MEMF(ecx))',
                                    '(fp_top() * MEMF(ecx))', 1),
        'comparison': hooked.replace('CMP_B(_fa, _fb)', 'CMP_BE(_fa, _fb)', 1),
        'scratch_store': hooked.replace('MEMF(ebp + -8) =', 'MEMF(ebp + -12) =', 1),
        'return': hooked.replace('esp += 8; return;', 'esp += 12; return;', 1),
    }
    for name, value in negative.items():
        source.write_text(value)
        result = subprocess.run(['cmake', '-P', str(script)], capture_output=True)
        assert result.returncode != 0, f'Accepted stale contract: {name}'
    wrapper = (ROOT / 'src/mm3_building_diagnostics.c').read_text()
    match = re.search(r'void sub_001C7B15\(void\)\n\{.*?\n\}', wrapper, re.S)
    assert match and match.group().count('sub_001C7B15_original();') == 1
    assert 'caller == 0x00168A92u' in match.group()
    assert 'caller == 0x0016886Au' in match.group()
    (folder / 'masked_visibility_wrapper.inc').write_text(match.group())
    print('PASS: LF/CRLF definition-only seam, unchanged wrapper/calls and eight stale/duplicate/reapply rejection controls')


def raw_fixture(folder, executable, image):
    subprocess.run([str(executable), '--raw', str(folder)], check=True)
    ram = bytearray(0x800000)
    for va, raw, size, _ in image.secs:
        ram[va:va + size] = image.d[raw:raw + size]
    for index in range(256):
        stem = f'{index:03}'
        before = (folder / (stem + '.before')).read_bytes()
        registers = struct.unpack('<10I', (folder / (stem + '.registers')).read_bytes())
        machine = common.unicorn.Uc(common.unicorn.UC_ARCH_X86, common.unicorn.UC_MODE_32)
        machine.mem_map(0, len(ram)); machine.mem_write(0, bytes(ram))
        machine.mem_write(BASE, before)
        for name, value in zip(REGS, registers):
            machine.reg_write(getattr(common.x86, 'UC_X86_REG_' + name), value)
        machine.reg_write(common.x86.UC_X86_REG_EFLAGS, 0x402 if registers[8] else 2)
        # Set the native precision control and the same incoming TOP. The leaf
        # does not read incoming live x87 values, only its own seven slots.
        init = b'\xdb\xe3\xd9\x2d\x20\x01\x60\x00' + b'\xd9\xf7' * (index % 8)
        machine.mem_write(0x600100, init)
        machine.mem_write(0x600120, struct.pack('<H', registers[9]))
        machine.emu_start(0x600100, 0x600100 + len(init))
        machine.emu_start(ENTRY, RETURN, count=10000)
        assert machine.reg_read(common.x86.UC_X86_REG_EIP) == RETURN, index
        expected_regs = struct.unpack('<10I', (folder / (stem + '.expected')).read_bytes())[:8]
        actual_regs = tuple(machine.reg_read(getattr(common.x86, 'UC_X86_REG_' + name)) for name in REGS)
        assert actual_regs == expected_regs, (index, actual_regs, expected_regs)
        actual = bytes(machine.mem_read(BASE, len(before)))
        expected = (folder / (stem + '.after')).read_bytes()
        assert actual == expected, (index, [(n, a, b) for n, (a, b) in enumerate(zip(actual, expected)) if a != b][:8])
    print('PASS: 256 original-XBE executions; exact RAM/GPR/ESP across masks, counts 0..32, tangency, overlap, all incoming TOPs, PC24/53 and every RC')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--benchmark', action='store_true')
    args = parser.parse_args()
    if not shutil.which('cl'):
        raise SystemExit('Run scripts/enter-env.ps1 first so cl.exe is available.')
    owners = []
    for path in (ROOT / 'src/recomp/gen').glob('*.c'):
        match = re.search(r'void sub_001C7B15\(void\)\n\{.*?\n\}', path.read_text(), re.S)
        if match:
            owners.append(match.group())
    assert len(owners) == 1, f'Expected one generated leaf, found {len(owners)}'
    body = owners[0]
    oracle = body.replace('sub_001C7B15', 'masked_visibility_oracle', 1)
    image = common.Image(str(ROOT / 'game_files/default.xbe'))
    assert image.d[image.raw(ENTRY):image.raw(ENTRY) + 6] == b'\x55\x8b\xec\x83\xec\x2c'
    assert image.d[image.raw(0x1C7C20):image.raw(0x1C7C20) + 10] == b'\xdd\xd8\x5f\xdd\xd8\x5e\xc9\xc2\x04\x00'
    print('Generated leaf SHA256:', hashlib.sha256(body.encode()).hexdigest())
    print('Native helper SHA256:', hashlib.sha256((ROOT / 'src/mm3_masked_visibility.c').read_bytes()).hexdigest())
    with tempfile.TemporaryDirectory(prefix='mm3-masked-visibility-') as temporary:
        folder = Path(temporary)
        seam_fixture(folder, body)
        (folder / 'masked_visibility_oracle.inc').write_text(oracle)
        executable = folder / 'contract.exe'
        subprocess.run([
            'cl', '/nologo', '/w', '/O2', '/Ob1', '/std:c11',
            '/D_CRT_SECURE_NO_WARNINGS', '/DRECOMP_GENERATED_CODE',
            '/I' + str(folder), '/I' + str(ROOT / 'src/recomp/gen'),
            str(ROOT / 'tests/mm3_masked_visibility_contract.c'),
            '/Fo:' + str(folder / 'contract.obj'), '/Fe:' + str(executable),
        ], check=True, cwd=folder)
        subprocess.run([str(executable)] + (['--benchmark'] if args.benchmark else []), check=True)
        raw_fixture(folder, executable, image)
        # A plausible but incorrect optimization rounds held axis1 Z to float.
        # Ensure the fixture actually detects that loss of native precision.
        source = (ROOT / 'src/mm3_masked_visibility.c').read_text()
        before = 'f2 = RECOMP_FP_PC(f6 * MEMF(plane + 8));'
        assert source.count(before) == 1
        bad_source = folder / 'bad.c'
        bad_source.write_text(source.replace(before,
            'f2 = RECOMP_FP_PC((double)(float)f6 * MEMF(plane + 8));', 1))
        contract = (ROOT / 'tests/mm3_masked_visibility_contract.c').read_text()
        contract = contract.replace('#include "../src/mm3_masked_visibility.c"',
                                    f'#include "{bad_source.as_posix()}"', 1)
        bad_contract = folder / 'bad_contract.c'
        bad_contract.write_text(contract)
        subprocess.run([
            'cl', '/nologo', '/w', '/O2', '/Ob1', '/std:c11',
            '/D_CRT_SECURE_NO_WARNINGS', '/DRECOMP_GENERATED_CODE',
            '/I' + str(folder), '/I' + str(ROOT / 'src/recomp/gen'),
            str(bad_contract), '/Fo:' + str(folder / 'bad.obj'),
            '/Fe:' + str(folder / 'bad.exe'),
        ], check=True, cwd=folder)
        bad = subprocess.run([str(folder / 'bad.exe')], capture_output=True, text=True)
        assert bad.returncode == 1 and 'FAIL case ' in bad.stdout, bad.stdout
        print('PASS: deliberately rounded-held-axis negative control failed the complete-state regression')


if __name__ == '__main__':
    main()
