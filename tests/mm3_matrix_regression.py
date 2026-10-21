"""Differential-test the native matrix leaf against generated and raw Xbox code.

Run after scripts/enter-env.ps1; --benchmark reports bounded helper throughput.
Temporary executables and cases are deleted. No running game or saves accessed.
"""
import argparse
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile

import mm3_x87_exp_regression as common

ROOT = common.ROOT
REGS = ('EAX', 'ECX', 'EDX', 'ESP', 'EBX', 'ESI', 'EDI', 'EBP')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--benchmark', action='store_true')
    args = parser.parse_args()
    if not shutil.which('cl'):
        raise SystemExit('Run scripts/enter-env.ps1 first so cl.exe is available.')
    bodies = []
    for path in (ROOT / 'src/recomp/gen').glob('*.c'):
        match = re.search(r'void sub_00259982\(void\)\n\{.*?\n\}', path.read_text(), re.S)
        if match:
            bodies.append(match.group())
    assert len(bodies) == 1, f'Expected one generated matrix leaf, found {len(bodies)}'
    oracle = bodies[0].replace('sub_00259982', 'matrix_oracle', 1)
    image = common.Image(str(ROOT / 'game_files/default.xbe'))
    # Assert the original frame/64-byte scratch and RET12 contract.
    assert image.d[image.raw(0x259982):image.raw(0x259982) + 6] == b'\x55\x8b\xec\x83\xec\x40'
    assert image.d[image.raw(0x259A5E):image.raw(0x259A5E) + 7] == b'\x5f\x5e\x5b\xc9\xc2\x0c\x00'
    with tempfile.TemporaryDirectory(prefix='mm3-matrix-') as temporary:
        folder = Path(temporary)
        (folder / 'matrix_oracle.inc').write_text(oracle)
        executable = folder / 'contract.exe'
        subprocess.run([
            'cl', '/nologo', '/w', '/O2', '/Ob1', '/std:c11',
            '/D_CRT_SECURE_NO_WARNINGS', '/DRECOMP_GENERATED_CODE',
            '/I' + str(folder), '/I' + str(ROOT / 'src/recomp/gen'),
            str(ROOT / 'tests/mm3_matrix_contract.c'),
            '/Fo:' + str(folder / 'contract.obj'), '/Fe:' + str(executable),
        ], check=True, cwd=folder)
        subprocess.run([str(executable)] + (['--benchmark'] if args.benchmark else []), check=True)
        subprocess.run([str(executable), '--raw', str(folder)], check=True)
        ram = bytearray(0x800000)
        for va, raw, size, _ in image.secs:
            ram[va:va + size] = image.d[raw:raw + size]
        for index in range(128):
            stem = f'{index:03}'
            before = (folder / (stem + '.before')).read_bytes()
            registers = struct.unpack('<10I', (folder / (stem + '.registers')).read_bytes())
            machine = common.unicorn.Uc(common.unicorn.UC_ARCH_X86, common.unicorn.UC_MODE_32)
            machine.mem_map(0, len(ram)); machine.mem_write(0, bytes(ram))
            machine.mem_write(0, before)
            for name, value in zip(REGS, registers):
                machine.reg_write(getattr(common.x86, 'UC_X86_REG_' + name), value)
            machine.reg_write(common.x86.UC_X86_REG_EFLAGS, 0x402 if registers[8] else 2)
            machine.mem_write(0x600100, b'\xdb\xe3\xd9\x2d\x20\x01\x60\x00')
            machine.mem_write(0x600120, struct.pack('<H', registers[9]))
            machine.emu_start(0x600100, 0x600108)
            machine.emu_start(0x259982, 0x600000, count=10000)
            assert machine.reg_read(common.x86.UC_X86_REG_EIP) == 0x600000, index
            expected_regs = struct.unpack('<10I', (folder / (stem + '.expected')).read_bytes())[:8]
            actual_regs = tuple(machine.reg_read(getattr(common.x86, 'UC_X86_REG_' + name)) for name in REGS)
            assert actual_regs == expected_regs, (index, actual_regs, expected_regs)
            actual = bytes(machine.mem_read(0, len(before)))
            expected = (folder / (stem + '.after')).read_bytes()
            assert actual == expected, (index, [(n, a, b) for n, (a, b) in enumerate(zip(actual, expected)) if a != b][:8])
        print('PASS: 128 original-XBE matrix executions, including every alias order, partial overlap and both DF values; exact RAM/GPR/ESP')


if __name__ == '__main__':
    main()
