"""Compare the production CRT remainder tail with the original Xbox x87 code.

Run under scripts/enter-env.ps1. The former generated loop is a negative
control: an unrelated unordered comparison must reproduce its timeout.
No running game, profile or save is read or modified.
"""
import ctypes
import math
import re
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path

import mm3_x87_exp_regression as common

ROOT = common.ROOT
ENTRY, RETURN, STACK, DATA, INIT = 0x253748, 0x600000, 0x700000, 0x610000, 0x620000


def native_source(old=False):
    if old:
        path = next(p for p in (ROOT / 'src/recomp/gen').glob('recomp_*.c')
                    if 'void sub_00253748(void)' in p.read_text())
        body = re.search(r'void sub_00253748\(void\)\n\{.*?\n\}',
                         path.read_text(), re.S).group()
        # Only unreachable embedded-data tails follow the real RET.
        body = body[:body.index('    eax = 0x3867D8;')] + '\n}\n'
        body = re.sub(r'\b(eax|esp|ebp)\b', r'g_\1', body)
    else:
        body = re.search(r'void sub_00253748\(void\)\n\{.*?\n\}',
                         (ROOT / 'src/mm3_cpu_compat.c').read_text(), re.S).group()
    return r'''
#define RECOMP_GENERATED_CODE
#include <stdio.h>
#include <stdlib.h>
#include "mm3_recomp_types.h"
RECOMP_TLS uint32_t g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp,g_seh_ebp,g_fs_base,g_eflags;
RECOMP_TLS double g_fp_stack[8];
RECOMP_TLS int g_fp_top,g_fp_cmp;
RECOMP_TLS uint16_t g_fp_cc,g_fp_control_word;
''' + body + r'''
int main(int argc,char**argv){
 FILE*in=fopen(argv[1],"rb");if(!in)return 2;
 for(;;){
  double dividend,divisor;uint32_t top;int previous;
  if(fread(&dividend,8,1,in)!=1)break;
  if(fread(&divisor,8,1,in)!=1||fread(&top,4,1,in)!=1||fread(&previous,4,1,in)!=1)return 3;
  for(int i=0;i<8;i++)g_fp_stack[i]=11.25+i;
  g_fp_stack[top]=divisor;g_fp_stack[(top+1)&7]=dividend;
  g_fp_top=top;g_fp_cmp=previous;g_fp_cc=0x4500;g_fp_control_word=0x37f;
  g_eax=0x12340000;g_esp=0x700000;
  sub_00253748();
  printf("%a %08X %u %u %u %d",g_fp_stack[g_fp_top],g_eax,g_esp,g_fp_top,g_fp_cc,g_fp_cmp);
  for(int i=1;i<7;i++)printf(" %a",g_fp_stack[(g_fp_top+i)&7]);
  puts("");
 }
 fclose(in);return 0;
}
'''


def original_machine(image):
    machine = common.unicorn.Uc(common.unicorn.UC_ARCH_X86, common.unicorn.UC_MODE_32)
    machine.mem_map(0, 0x800000)
    section = next(s for s in image.secs if s[0] <= ENTRY < s[0] + s[2])
    offset = section[1] + ENTRY - section[0]
    machine.mem_write(ENTRY, image.d[offset:offset + 0x10])
    return machine


class HardwareRemainder:
    """Run the retail x87 instructions on the host, saving its FP context."""
    def __init__(self, image):
        section = next(s for s in image.secs if s[0] <= ENTRY < s[0] + s[2])
        offset = section[1] + ENTRY - section[0]
        body = image.d[offset:offset + 13]  # Through FSTP ST(1), before RET.
        assert body.hex() == 'd9c9d9f89bdfe09b9e7af7ddd9'
        self.kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        self.kernel.VirtualAlloc.argtypes = [ctypes.c_void_p, ctypes.c_size_t,
                                            ctypes.c_uint32, ctypes.c_uint32]
        self.kernel.VirtualAlloc.restype = ctypes.c_void_p
        self.kernel.VirtualFree.argtypes = [ctypes.c_void_p, ctypes.c_size_t,
                                           ctypes.c_uint32]
        self.blocks, self.functions = [], []
        for top in range(8):
            code = b'\x48\x81\xec\x18\x02\x00\x00'  # Aligned FXSAVE area.
            code += b'\x48\x0f\xae\x04\x24\xdb\xe3'
            code += b''.join(b'\xdd\x41' + bytes([8 * i]) for i in reversed(range(8)))
            code += b'\xd9\xf7' * top
            code += b'\xb8\x00\x00\x34\x12' + body
            code += b'\x89\x42\x38\xdd\x7a\x3c'  # EAX, final FNSTSW.
            code += b''.join(b'\xdd\x5a' + bytes([8 * i]) for i in range(7))
            code += b'\x48\x0f\xae\x0c\x24\x48\x81\xc4\x18\x02\x00\x00\xc3'
            block = self.kernel.VirtualAlloc(None, len(code), 0x3000, 0x40)
            if not block:
                raise ctypes.WinError(ctypes.get_last_error())
            self.blocks.append(block)
            ctypes.memmove(block, code, len(code))
            self.functions.append(ctypes.CFUNCTYPE(None, ctypes.c_void_p,
                                                   ctypes.c_void_p)(block))

    def result(self, dividend, divisor, top):
        inputs = (ctypes.c_double * 8)(*[11.25 + i for i in range(8)])
        inputs[top], inputs[(top + 1) & 7] = divisor, dividend
        output = ctypes.create_string_buffer(64)
        self.functions[top](inputs, output)
        eax, status = struct.unpack_from('<IH', output.raw, 56)
        return eax, STACK + 4, (status >> 11) & 7, struct.unpack('<7d', output.raw[:56])

    def close(self):
        for block in self.blocks:
            self.kernel.VirtualFree(block, 0, 0x8000)


def original_result(machine, dividend, divisor, top):
    values = [11.25 + i for i in range(8)]
    values[top], values[(top + 1) & 7] = divisor, dividend
    machine.mem_write(DATA, struct.pack('<8d', *values))
    code = b'\xdb\xe3'  # FNINIT; fill all physical registers, TOP=0.
    for i in reversed(range(8)):
        code += b'\xdd\x05' + struct.pack('<I', DATA + 8 * i)
    code += b'\xd9\xf7' * top  # FINCSTP to the requested physical TOP.
    machine.mem_write(INIT, code)
    machine.emu_start(INIT, INIT + len(code))
    machine.mem_write(STACK, struct.pack('<I', RETURN))
    machine.reg_write(common.x86.UC_X86_REG_ESP, STACK)
    machine.reg_write(common.x86.UC_X86_REG_EAX, 0x12340000)
    machine.emu_start(ENTRY, RETURN, count=10000)
    assert machine.reg_read(common.x86.UC_X86_REG_EIP) == RETURN
    eax = machine.reg_read(common.x86.UC_X86_REG_EAX)
    esp = machine.reg_read(common.x86.UC_X86_REG_ESP)
    final_top = (machine.reg_read(common.x86.UC_X86_REG_FPSW) >> 11) & 7
    stores = b''.join(b'\xdd\x1d' + struct.pack('<I', DATA + i * 8) for i in range(7))
    machine.mem_write(RETURN, stores)
    machine.emu_start(RETURN, RETURN + len(stores))
    return eax, esp, final_top, struct.unpack('<7d', machine.mem_read(DATA, 56))


def main():
    if not shutil.which('cl'):
        raise SystemExit('Run scripts/enter-env.ps1 first.')
    # Includes every quotient low-bit combination, both signs, signed zeros,
    # subnormal inputs, a huge exponent gap requiring multiple retail FPREMs,
    # and the exact finite values present in the captured taxi-pickup hang.
    capture_dividend = struct.unpack('<d', bytes.fromhex('00000020a62cc13f'))[0]
    pairs = [(capture_dividend, .5), (0., 2.), (-0., 2.), (1., math.inf)]
    pairs += [(sign * (q * 2. + .5), divisor)
              for q in range(8) for sign in (-1, 1) for divisor in (-2., 2.)]
    pairs += [(1.e300, 3.), (-1.e300, 3.), (1., 1.e-300),
              (math.ldexp(1., -1074), 2.), (1., math.ldexp(1., -1074))]
    cases = [(a, b, top, cmp) for top in range(8) for cmp in (-1, 0, 1, 2)
             for a, b in pairs]
    image = common.Image(str(ROOT / 'game_files/default.xbe'))
    machine = original_machine(image)
    hardware = HardwareRemainder(image)
    with tempfile.TemporaryDirectory(prefix='fprem-', dir=ROOT / 'conformance_tmp') as path:
        directory = Path(path)
        executables = []
        for old in (False, True):
            name = 'old' if old else 'fixed'
            source, executable = directory / (name + '.c'), directory / (name + '.exe')
            source.write_text(native_source(old))
            subprocess.run(['cl', '/nologo', '/O2', '/MD', '/std:c11',
                            '/I' + str(ROOT / 'src'),
                            '/I' + str(ROOT / 'tools/xboxrecomp/include'),
                            '/Fo' + str(directory / (name + '.obj')),
                            '/Fe' + str(executable), str(source)], check=True,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            executables.append(executable)
        inputs = directory / 'cases.bin'
        inputs.write_bytes(b''.join(struct.pack('<ddIi', *case) for case in cases))
        rows = subprocess.check_output([str(executables[0]), str(inputs)], text=True,
                                       timeout=20).splitlines()
        assert len(rows) == len(cases)
        for case, line in zip(cases, rows):
            values = line.split()
            remainder = float.fromhex(values[0])
            eax, esp, top, cc = int(values[1], 16), *map(int, values[2:5])
            expected_eax, expected_esp, expected_top, expected_values = hardware.result(*case[:3])
            # Sticky exception bits are not represented in the title's
            # double-backed x87 model; compare its TOP and C0/C1/C2/C3 bits.
            expected_eax &= 0xFFFF7F00
            # Unicorn loses signed zero and miscomputes some partial
            # reductions/subnormal divisors. Hardware is authoritative there.
            if abs(math.frexp(case[0])[1] - math.frexp(case[1])[1]) < 64:
                unicorn_result = original_result(machine, *case[:3])
                assert (unicorn_result[0] & 0xFFFF7F00, *unicorn_result[1:3]) == (expected_eax, expected_esp, expected_top), case
                assert unicorn_result[3] == expected_values, case
            assert struct.pack('<d', remainder) == struct.pack('<d', expected_values[0]), (case, line, expected_values)
            assert (eax, esp, top) == (expected_eax, expected_esp, expected_top), (case, line, hex(expected_eax))
            assert cc & 0x0400 == 0 and int(values[5]) != 2, (case, line)
            assert tuple(map(float.fromhex, values[6:])) == expected_values[1:], case
        negative = directory / 'unordered.bin'
        negative.write_bytes(struct.pack('<ddIi', capture_dividend, .5, 5, 2))
        try:
            subprocess.run([str(executables[1]), str(negative)], timeout=1,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
        except subprocess.TimeoutExpired:
            pass
        else:
            raise AssertionError('Former generated FPREM loop did not reproduce the hang')
    hardware.close()
    print(f'PASS: {len(cases)} hardware original-x87 comparisons, with bounded Unicorn cross-checks; former unordered loop times out')


if __name__ == '__main__':
    main()
