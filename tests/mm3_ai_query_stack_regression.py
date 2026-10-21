"""Run the actual AI query body through its checked CMake recovery overlay.

Resolved decline/success paths are compared with the original XBE in Unicorn.
Missing/noncode dispatch must log and retain the exact stack and saved registers.
The unchanged generated body is a required failing recovery control.
"""
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile

import mm3_x87_exp_regression as common

ROOT = common.ROOT
ENTRY, STACK, RETURN = 0x13AB27, 0x700000, 0x600000
OWNER, RECORD, PROVIDER, VTABLE = 0x720000, 0x730000, 0x740000, 0x750000
FIRST, SECOND, METHOD, ACQUIRE = 0x760000, 0x760020, 0x13000, 0x13100
REGS = ('EAX', 'ECX', 'EDX', 'ESP', 'EBX', 'ESI', 'EDI', 'EBP')
INITIAL = (0x11223344, OWNER, 0x55667788, STACK,
           0x12345678, 0x23456789, 0x3456789A, 0x456789AB)

HARNESS = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define RECOMP_GENERATED_CODE
#include "mm3_recomp_types.h"
ptrdiff_t g_xbox_mem_offset;
uint32_t g_xbox_code_lo=0x12000,g_xbox_code_hi=0x260000;
RECOMP_TLS uint32_t g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp,g_seh_ebp,g_fs_base,g_eflags;
RECOMP_TLS int g_fp_top,g_df,g_fp_cmp;
RECOMP_TLS uint16_t g_fp_control_word,g_fp_cc;
RECOMP_TLS double g_fp_stack[8];
RECOMP_TLS RecompXmm g_xmm0,g_xmm1,g_xmm2,g_xmm3,g_xmm4,g_xmm5,g_xmm6,g_xmm7;
volatile uint32_t g_icall_trace[ICALL_TRACE_SIZE],g_icall_trace_idx;
volatile uint64_t g_icall_count;
static unsigned mode,calls,logs,acquired;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"AI query line%d: %s\n",__LINE__,#x);exit(2);}}while(0)
static void method(void){
 CHECK(g_ecx==0x740000 && MEM32(g_esp+4)==(calls?0x73000C:0x730000));
 ++calls;g_eax=mode==0?0:calls==1?0x760000:mode==1?0:0x760020;
 if(mode==5)MEM32(0x750004)=0;
 g_esp+=8;
}
static void acquire(void){CHECK(g_ecx==0x740000);++acquired;g_esp+=4;}
void sub_00065947(void){MEM32(g_esi+0x10)=0;g_esp+=4;}
void sub_000658D1(void){CHECK(MEM32(g_esp+4)==0x720020);g_esp+=8;}
recomp_func_t recomp_lookup(uint32_t va){return va==0x13000?method:va==0x13100?acquire:NULL;}
recomp_func_t recomp_lookup_manual(uint32_t va){(void)va;return NULL;}
recomp_func_t recomp_lookup_kernel(uint32_t va){(void)va;return NULL;}
void recomp_icall_not_code_log(uint32_t va){CHECK(va==0);++logs;}
void recomp_icall_fail_log(uint32_t va){CHECK(va==0x13200);++logs;}
#include "body.inc"
int main(int argc,char **argv){
 CHECK(argc==5);mode=(unsigned)strtoul(argv[2],NULL,0);
 unsigned char *ram=malloc(0x800000);CHECK(ram);
 FILE *in=fopen(argv[1],"rb");CHECK(in&&fread(ram,1,0x800000,in)==0x800000);fclose(in);
 g_xbox_mem_offset=(ptrdiff_t)ram;
 uint32_t *regs=(uint32_t *)(ram+0x770000);
 g_eax=regs[0];g_ecx=regs[1];g_edx=regs[2];g_esp=regs[3];
 g_ebx=regs[4];g_esi=regs[5];g_edi=regs[6];g_ebp=g_seh_ebp=regs[7];
 g_fp_control_word=0x37F;
 sub_0013AB27();
 CHECK(g_esp==0x700008&&g_ebx==0x12345678&&g_esi==0x23456789&&g_edi==0x3456789A);
 CHECK(g_ebp==0x456789AB);
 CHECK(logs==(mode>=3?1u:0u));
 CHECK(acquired==(mode==2?1u:0u));
 if(mode!=2)CHECK(!(g_eax&255));
 CHECK(MEM32(0x730030)==(mode==6?3u:0x740000u)); /* Bad input is not hidden or rewritten. */
 uint32_t outregs[]={g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp};
 FILE *out=fopen(argv[3],"wb");CHECK(out&&fwrite(ram,1,0x800000,out)==0x800000);fclose(out);
 out=fopen(argv[4],"wb");CHECK(out&&fwrite(outregs,1,sizeof outregs,out)==sizeof outregs);fclose(out);
 free(ram);return 0;
}
'''


def body_source():
    matches = []
    for path in (ROOT / 'src/recomp/gen').glob('recomp_*.c'):
        match = re.search(r'void sub_0013AB27\(void\)\n\{.*?\n\}', path.read_text(), re.S)
        if match:
            matches.append(match.group())
    assert len(matches) == 1
    return matches[0]


def overlay(directory, original):
    script = directory / 'seam.cmake'
    script.write_text(f'''include("{(ROOT / 'scripts/mm3_ai_query_stack_seam.cmake').as_posix()}")
file(READ "${{INPUT}}" source)
mm3_fix_ai_query_stack(source)
file(WRITE "${{OUTPUT}}" "${{source}}")
''')
    inp, out = directory / 'input.c', directory / 'fixed.c'
    for newline in ('\n', '\r\n'):
        inp.write_bytes(original.replace('\n', newline).encode())
        subprocess.run(['cmake', '-DINPUT=' + str(inp), '-DOUTPUT=' + str(out), '-P', str(script)], check=True)
        fixed = out.read_text()
        assert fixed.count('uint32_t _icall_esp = g_esp;') == original.count('uint32_t _icall_esp = g_esp;')
        assert re.sub(r'    \{ uint32_t _icall_esp = g_esp;\n', '', fixed) == re.sub(
            r'    \{ uint32_t _icall_esp = g_esp;\n', '', original)
    for invalid in (original.replace('PUSH32(esp, edi);', 'PUSH32(esp, ecx);', 1),
                    original.replace('0x0013AB36u', '0x0013AB37u'),
                    original.replace('esp += 8; return;', 'esp += 4; return;'),
                    original + '\n' + original, fixed):
        inp.write_text(invalid)
        result = subprocess.run(['cmake', '-DINPUT=' + str(inp), '-DOUTPUT=' + str(out), '-P', str(script)], capture_output=True, text=True)
        assert result.returncode and 'MM3 AI query stack:' in result.stderr
    return fixed


def fixture(image, mode):
    ram = bytearray(0x800000)
    for va, raw, size, _ in image.secs:
        ram[va:va + size] = image.d[raw:raw + size]
    put = lambda va, *values: struct.pack_into('<' + 'I' * len(values), ram, va, *values)
    put(STACK, RETURN, RECORD)
    put(RECORD + 0x30, 3 if mode == 6 else PROVIDER)
    put(PROVIDER, VTABLE)
    put(VTABLE, ACQUIRE, 0 if mode in (3, 6) else 0x13200 if mode == 4 else METHOD)
    put(0x770000, *INITIAL)
    ram[OWNER:OWNER + 0x30] = bytes([0xA5]) * 0x30
    ram[FIRST:FIRST + 0x40] = bytes([0xA7]) * 0x40
    # Controlled downstream callbacks have identical ABI and effects in both engines.
    first, second = (0 if mode == 0 else FIRST), (0 if mode == 1 else SECOND)
    callback = (b'\x81\x7C\x24\x04' + struct.pack('<I', RECORD) + b'\x75\x08\xB8'
        + struct.pack('<I', first) + b'\xC2\x04\x00\xB8' + struct.pack('<I', second) + b'\xC2\x04\x00')
    ram[METHOD:METHOD + len(callback)] = callback
    ram[ACQUIRE] = 0xC3
    ram[0x65947:0x6594F] = b'\xC7\x46\x10\x00\x00\x00\x00\xC3'
    ram[0x658D1:0x658D4] = b'\xC2\x04\x00'
    return ram


def original_result(ram):
    machine = common.unicorn.Uc(common.unicorn.UC_ARCH_X86, common.unicorn.UC_MODE_32)
    machine.mem_map(0, len(ram)); machine.mem_write(0, bytes(ram))
    for name, value in zip(REGS, INITIAL):
        machine.reg_write(getattr(common.x86, 'UC_X86_REG_' + name), value)
    machine.mem_write(RETURN + 0x100, b'\xDB\xE3')
    machine.emu_start(RETURN + 0x100, RETURN + 0x102)
    machine.emu_start(ENTRY, RETURN, count=1000)
    assert machine.reg_read(common.x86.UC_X86_REG_EIP) == RETURN
    return tuple(machine.reg_read(getattr(common.x86, 'UC_X86_REG_' + name)) for name in REGS), bytes(machine.mem_read(0, len(ram)))


def main():
    if not shutil.which('cl'):
        raise SystemExit('Run scripts/enter-env.ps1 first.')
    image = common.Image(str(ROOT / 'game_files/default.xbe'))
    # Assert the original callee-save prologue, argument push, call and RET4.
    assert image.d[image.raw(ENTRY):image.raw(ENTRY) + 3] == b'\x53\x56\x57'
    assert image.d[image.raw(0x13AB35):image.raw(0x13AB35) + 4] == b'\x57\xFF\x50\x04'
    assert image.d[image.raw(0x13AB8E):image.raw(0x13AB8E) + 6] == b'\x5F\x5E\x5B\xC2\x04\x00'
    with tempfile.TemporaryDirectory(prefix='mm3-ai-query-') as temporary:
        directory = Path(temporary)
        original = body_source(); fixed = overlay(directory, original)
        executables = []
        for label, body in [('original', original), ('fixed', fixed)]:
            folder = directory / label; folder.mkdir()
            (folder / 'body.inc').write_text(body)
            source, exe = folder / 'contract.c', folder / 'contract.exe'; source.write_text(HARNESS)
            subprocess.run(['cl', '/nologo', '/O2', '/MD', '/std:c11', '/D_CRT_SECURE_NO_WARNINGS',
                '/I' + str(ROOT / 'src'), '/I' + str(ROOT / 'tools/xboxrecomp/include'),
                '/Fo:' + str(folder / 'contract.obj'), '/Fe:' + str(exe), str(source)], cwd=folder, check=True)
            executables.append(exe)
        for mode in range(7):
            ram = fixture(image, mode)
            inp, out, regs = directory / 'image.bin', directory / 'result.bin', directory / 'regs.bin'
            inp.write_bytes(ram)
            subprocess.run([str(executables[1]), str(inp), str(mode), str(out), str(regs)], check=True)
            if mode < 3:
                expected_regs, expected_ram = original_result(ram)
                assert struct.unpack('<8I', regs.read_bytes()) == expected_regs, mode
                actual_ram = out.read_bytes()
                for start, end in ((STACK - 0x100, STACK + 0x20), (OWNER, OWNER + 0x30), (FIRST, FIRST + 0x40)):
                    assert actual_ram[start:end] == expected_ram[start:end], (mode, hex(start))
            if mode in (3, 4, 6):
                broken = subprocess.run([str(executables[0]), str(inp), str(mode), str(out), str(regs)], capture_output=True, text=True)
                assert broken.returncode == 2 and 'g_esp==0x700008' in broken.stderr
        print('PASS: 3 original-XBE resolved paths, 4 logged dispatch failures, 3 failing old-stack controls; exact saved registers/ESP and strict overlay anchors')


if __name__ == '__main__':
    main()
