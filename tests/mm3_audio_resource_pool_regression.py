"""Exercise the production audio resource factory and real deque helpers.

Compare nonempty and active-reuse calls with original XBE instructions in
Unicorn. Empty calls must use the existing null-result ABI without reading a
stale allocator header, modifying the pool, or allocating a wrapper. The old
factory is a required failing control for that contract.
"""
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile

import mm3_x87_exp_regression as common

ROOT = common.ROOT
ENTRY, STACK, RETURN = 0x17BB4B, 0x700000, 0x600000
MANAGER, SENTINEL, MAP, BLOCKS = 0x720000, 0x721000, 0x722000, 0x723000
OBJECT, VTABLE, ACTIVE, NODE = 0x740000, 0x741000, 0x742000, 0x743000
COORD1, COORD2, ALLOCATION = 0x750000, 0x750040, 0x760000
METHOD1, METHOD2, METHOD3 = 0x13000, 0x13100, 0x13200
REGS = ('EAX', 'ECX', 'EDX', 'ESP', 'EBX', 'ESI', 'EDI', 'EBP')
INITIAL = (0x11223344, 0x22334455, 0x33445566, STACK,
           0x12345678, 0x23456789, 0x3456789A, 0x456789AB)
CASES = (
    ('last', 1, 0, 8, False, False, False),
    ('block_edge', 5, 3, 8, False, False, False),
    ('map_wrap', 8, 28, 8, False, False, False),
    ('full_retail_pool', 64, 0, 16, False, False, False),
    ('last_offset', 1, 31, 8, False, False, False),
    ('null_slot', 1, 0, 8, False, False, True),
    ('empty_header', 0, 0, 8, False, False, False),
    ('empty_stale_slot', 0, 1, 8, False, False, False),
    ('empty_unmapped', 0, 0, 8, False, False, False),
    ('active_reuse_empty', 0, 0, 8, True, True, False),
    ('active_miss_empty', 0, 0, 8, True, False, False),
    ('active_reuse_nonempty', 3, 1, 8, True, True, False),
    ('active_miss_nonempty', 3, 1, 8, True, False, False),
)
ENTRIES = (ENTRY, 0x79626, 0x42921, 0x4294E)

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
static unsigned allocations,inserts,empty_calls;
void mm3_stress_audio_pool_empty(void){++empty_calls;}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"audio pool line%d: %s\n",__LINE__,#x);exit(2);}}while(0)
static void method1(void){g_eax=MEM32(g_ecx+0x10);g_esp+=4;}
static void method2(void){g_eax=MEM32(g_ecx+0x14);g_esp+=4;}
static void method3(void){g_fp_top=(g_fp_top+7)&7;g_fp_stack[g_fp_top]=MEMF(g_ecx+0x18);g_esp+=4;}
void sub_0002D2F6(void){SET_LO8(g_eax,g_edx==g_ecx);g_esp+=4;}
void sub_0016FF04(void){CHECK(MEM32(g_esp+4)==12);++allocations;g_eax=0x760000;g_esp+=8;}
void sub_0003F85B(void){CHECK(MEM32(g_esp+4)==0x720000&&MEM32(g_esp+8)==0x721000);
 CHECK(MEM32(MEM32(g_esp+12))==0x760000);++inserts;g_esp+=16;}
recomp_func_t recomp_lookup(uint32_t va){return va==0x13000?method1:va==0x13100?method2:va==0x13200?method3:NULL;}
recomp_func_t recomp_lookup_manual(uint32_t va){(void)va;return NULL;}
recomp_func_t recomp_lookup_kernel(uint32_t va){(void)va;return NULL;}
void recomp_icall_not_code_log(uint32_t va){(void)va;CHECK(0);}
void recomp_icall_fail_log(uint32_t va){(void)va;CHECK(0);}
#include "body.inc"
int main(int argc,char **argv){
 CHECK(argc==5);unsigned expect=(unsigned)strtoul(argv[2],NULL,0);
 unsigned char *ram=malloc(0x800000);CHECK(ram);
 FILE *in=fopen(argv[1],"rb");CHECK(in&&fread(ram,1,0x800000,in)==0x800000);fclose(in);
 g_xbox_mem_offset=(ptrdiff_t)ram;
 uint32_t *regs=(uint32_t *)(ram+0x770000);
 g_eax=regs[0];g_ecx=regs[1];g_edx=regs[2];g_esp=regs[3];
 g_ebx=regs[4];g_esi=regs[5];g_edi=regs[6];g_ebp=g_seh_ebp=regs[7];
 g_fp_control_word=0x37F;
 sub_0017BB4B();
 if(g_esp!=0x700014||g_ebx!=0x12345678||g_esi!=0x23456789||g_edi!=0x3456789A||g_ebp!=0x456789AB)
  fprintf(stderr,"registers esp=%08X ebx=%08X esi=%08X edi=%08X ebp=%08X\n",g_esp,g_ebx,g_esi,g_edi,g_ebp);
 CHECK(g_esp==0x700014&&g_ebx==0x12345678&&g_esi==0x23456789&&g_edi==0x3456789A&&g_ebp==0x456789AB);
 CHECK(g_eax==expect);
 CHECK(empty_calls==(expect==0 && MEM32(0x72001C)==0));
 CHECK(allocations==(expect==0x760000)&&inserts==allocations);
 if(expect==0)CHECK(MEM32(0x72001C)==0 || MEM32(0x723000)==0);
 if(expect==0x760000)CHECK(MEM32(expect)==0x740000&&MEM32(expect+4)==1&&MEM8(expect+8)==0);
 uint32_t outregs[]={g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp};
 FILE *out=fopen(argv[3],"wb");CHECK(out&&fwrite(ram,1,0x800000,out)==0x800000);fclose(out);
 out=fopen(argv[4],"wb");CHECK(out&&fwrite(outregs,1,sizeof outregs,out)==sizeof outregs);fclose(out);
 free(ram);return 0;
}
'''


def bodies():
    found = {}
    for path in (ROOT / 'src/recomp/gen').glob('recomp_*.c'):
        text = path.read_text()
        for va in ENTRIES:
            match = re.search(rf'void sub_{va:08X}\(void\)\n\{{.*?\n\}}', text, re.S)
            if match:
                assert va not in found
                found[va] = match.group()
    assert set(found) == set(ENTRIES)
    declarations = '\n'.join(f'void sub_{va:08X}(void);' for va in ENTRIES)
    return found, declarations


def overlay(directory, original):
    script = directory / 'seam.cmake'
    script.write_text(f'''include("{(ROOT / 'scripts/mm3_audio_resource_pool_seam.cmake').as_posix()}")
file(READ "${{INPUT}}" source)
mm3_guard_audio_resource_pool(source)
file(WRITE "${{OUTPUT}}" "${{source}}")
''')
    inp, out = directory / 'input.c', directory / 'fixed.c'
    addition = ('    /* Native PC: an exhausted free deque has no resource to return. */\n'
                '    if (MEM32(ebx + 0x1C) == 0) {\n'
                '        mm3_stress_audio_pool_empty(); /* opt-in host counter, no guest writes */\n'
                '        goto loc_0017BC03;\n    }\n')
    include = '#include "mm3_stress_debug.h"\n'
    for newline in ('\n', '\r\n'):
        inp.write_bytes(original.replace('\n', newline).encode())
        subprocess.run(['cmake', '-DINPUT=' + str(inp), '-DOUTPUT=' + str(out), '-P', str(script)], check=True)
        fixed = out.read_text()
        assert fixed.count(include) == 1 and fixed.count(addition) == 1 and fixed.replace(include, '').replace(addition, '') == original
    for invalid in (original.replace('edi = ebx + 0xC;', 'edi = ebx + 0x10;', 1),
                    original.replace('0x00079626u', '0x00079627u'),
                    original.replace('esp += 20; return;', 'esp += 16; return;'),
                    original + '\n' + original, fixed):
        inp.write_text(invalid)
        result = subprocess.run(['cmake', '-DINPUT=' + str(inp), '-DOUTPUT=' + str(out), '-P', str(script)], capture_output=True, text=True)
        assert result.returncode and 'MM3 audio resource pool:' in result.stderr
    return fixed


def fixture(image, case):
    _, count, offset, mapsize, active, match, null_slot = case
    ram = bytearray(0x800000)
    for va, raw, size, _ in image.secs:
        ram[va:va + size] = image.d[raw:raw + size]
    put = lambda va, *values: struct.pack_into('<' + 'I' * len(values), ram, va, *values)
    put(STACK, RETURN, MANAGER, COORD1, COORD2, 0x40000000)
    put(MANAGER + 4, SENTINEL)
    put(MANAGER + 0x10, MAP, mapsize, offset, count)
    if case[0] == 'empty_unmapped':
        put(MANAGER + 0x10, 0xFFFF0000)  # The guard must not touch the empty deque's map.
    put(SENTINEL, NODE if active else SENTINEL)
    put(NODE, SENTINEL, SENTINEL, ACTIVE)
    put(ACTIVE, OBJECT, 7)
    put(OBJECT, VTABLE)
    put(OBJECT + 0x10, COORD1 if match else COORD1 + 4, COORD2, 0x40000000)
    put(VTABLE + 0x24, METHOD1, METHOD2, METHOD3)
    for index in range(mapsize):
        block = BLOCKS + index * 0x100
        put(MAP + index * 4, block)
        put(block - 4, 0x04000358)  # Real failure: a CRT prev-link mistaken for an object.
        put(block, *([OBJECT] * 4))
    if null_slot:
        put(BLOCKS, 0)
    ram[ALLOCATION:ALLOCATION + 16] = bytes([0xA5]) * 16
    put(0x770000, *INITIAL)
    # Controlled allocator/list stubs have identical ABI in both engines.
    stubs = {0x16FF04: b'\xB8' + struct.pack('<I', ALLOCATION) + b'\xC2\x04\x00',
             0x3F85B: b'\xC2\x0C\x00',
             0x2D2F6: b'\x39\xCA\x0F\x94\xC0\xC3',
             METHOD1: b'\x8B\x41\x10\xC3', METHOD2: b'\x8B\x41\x14\xC3',
             METHOD3: b'\xD9\x41\x18\xC3'}
    for va, code in stubs.items():
        ram[va:va + len(code)] = code
    return ram


def original_result(ram):
    machine = common.unicorn.Uc(common.unicorn.UC_ARCH_X86, common.unicorn.UC_MODE_32)
    machine.mem_map(0, len(ram)); machine.mem_write(0, bytes(ram))
    for name, value in zip(REGS, INITIAL):
        machine.reg_write(getattr(common.x86, 'UC_X86_REG_' + name), value)
    machine.mem_write(RETURN + 0x100, b'\xDB\xE3')
    machine.emu_start(RETURN + 0x100, RETURN + 0x102)
    machine.emu_start(ENTRY, RETURN, count=2000)
    assert machine.reg_read(common.x86.UC_X86_REG_EIP) == RETURN
    return tuple(machine.reg_read(getattr(common.x86, 'UC_X86_REG_' + name)) for name in REGS), bytes(machine.mem_read(0, len(ram)))


def main():
    if not shutil.which('cl'):
        raise SystemExit('Run scripts/enter-env.ps1 first.')
    image = common.Image(str(ROOT / 'game_files/default.xbe'))
    assert image.d[image.raw(0x17BBB1):image.raw(0x17BBB1) + 5] == b'\x8D\x7B\x0C\x8B\xCF'
    assert image.d[image.raw(0x17BC03):image.raw(0x17BC03) + 9] == b'\x33\xC0\x5F\x5E\x5B\x5D\xC2\x10\x00'
    found, declarations = bodies()
    with tempfile.TemporaryDirectory(prefix='mm3-audio-pool-') as temporary:
        directory = Path(temporary)
        fixed = overlay(directory, found[ENTRY])
        executables = []
        for label, factory in [('original', found[ENTRY]), ('fixed', fixed)]:
            folder = directory / label; folder.mkdir()
            # EBP is a generated C local; publish its final value only after
            # the real POP so the harness can compare it with the x86 register.
            assert factory.count('esp += 20; return; /* ret 16 */') == 1
            factory = factory.replace('esp += 20; return; /* ret 16 */',
                                      'g_ebp = ebp; esp += 20; return; /* ret 16 */')
            body = declarations + '\n' + '\n'.join(factory if va == ENTRY else found[va] for va in ENTRIES)
            (folder / 'body.inc').write_text(body)
            source, exe = folder / 'contract.c', folder / 'contract.exe'; source.write_text(HARNESS)
            subprocess.run(['cl', '/nologo', '/O2', '/MD', '/std:c11', '/D_CRT_SECURE_NO_WARNINGS',
                '/I' + str(ROOT / 'src'), '/I' + str(ROOT / 'tools/xboxrecomp/include'),
                '/Fo:' + str(folder / 'contract.obj'), '/Fe:' + str(exe), str(source)], cwd=folder, check=True)
            executables.append(exe)
        raw_comparisons = negative_controls = 0
        for case in CASES:
            name, count, offset, _, active, match, null_slot = case
            empty = not count and not (active and match)
            expected = ACTIVE if active and match else 0 if empty or null_slot else ALLOCATION
            ram = fixture(image, case)
            inp, out, regs = directory / 'image.bin', directory / 'result.bin', directory / 'regs.bin'
            inp.write_bytes(ram)
            subprocess.run([str(executables[1]), str(inp), str(expected), str(out), str(regs)], check=True)
            actual_ram = out.read_bytes()
            if empty:
                assert actual_ram[MANAGER:MANAGER + 0x20] == ram[MANAGER:MANAGER + 0x20]
                assert actual_ram[MAP:BLOCKS + 0x1000] == ram[MAP:BLOCKS + 0x1000]
                assert actual_ram[ALLOCATION:ALLOCATION + 16] == ram[ALLOCATION:ALLOCATION + 16]
                if name == 'empty_unmapped':
                    continue
                raw_regs, raw_ram = original_result(ram)
                assert raw_regs[0] == ALLOCATION
                assert struct.unpack_from('<I', raw_ram, ALLOCATION)[0] == (0x04000358 if not offset else OBJECT)
                broken = subprocess.run([str(executables[0]), str(inp), '0', str(out), str(regs)], capture_output=True, text=True)
                assert broken.returncode == 2 and 'g_eax==expect' in broken.stderr, name
                negative_controls += 1
            else:
                expected_regs, expected_ram = original_result(ram)
                assert struct.unpack('<8I', regs.read_bytes()) == expected_regs, name
                for start, end in ((STACK - 0x100, STACK + 0x20), (MANAGER, MANAGER + 0x20),
                                   (MAP, BLOCKS + 0x1000), (ACTIVE, ACTIVE + 16),
                                   (ALLOCATION, ALLOCATION + 16)):
                    assert actual_ram[start:end] == expected_ram[start:end], (name, hex(start))
                raw_comparisons += 1
        print(f'PASS: {raw_comparisons} original-XBE factory/deque paths, {negative_controls} empty-pool failing old controls, unreadable empty map; exact saved registers/ESP, no empty-pool allocation/mutation, strict overlay anchors')


if __name__ == '__main__':
    main()
