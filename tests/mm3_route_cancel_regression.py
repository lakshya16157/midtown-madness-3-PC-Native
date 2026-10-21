"""Production cancellation/map/deque bodies versus native XBE lifecycle calls.

Only payload-copy/destruction, node allocation, and worker start/step are
controlled. Native map lookup/insertion/erasure, deque traversal/removal, and
worker busy/reset methods run unchanged. The old same-key submit must leave a
freed node queued; the checked overlay must keep node and worker ownership safe.
"""
import re
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path

import mm3_x87_exp_regression as common

ROOT = common.ROOT
S, STACK, RETURN, HEADER = 0x720000, 0x700000, 0x600000, 0x721000
NODE, NEW, OTHER, THIRD = 0x770000, 0x771000, 0x772000, 0x773000
MAP, BLOCKS, WORKER, VTABLE = 0x730000, 0x740000, 0x750000, 0x751000
KEY, OTHER_KEY, REQUEST, OUTPUT, GRAPH, META = 0x760000, 0x760010, 0x761000, 0x762000, 0x763000, 0x780000
REGS = ('EAX', 'ECX', 'EDX', 'ESP', 'EBX', 'ESI', 'EDI', 'EBP')
ENTRIES = (0x24FA01, 0x24F837, 0x24F917, 0x24F964)
TERMINALS = {0x5B6DA, 0x14EBDA, 0x5B311, 0x170ED1, 0x5BB15,
             0x14E9B7, 0x443B9, 0x170EEE, 0x125EA, 0x13BA6, 0x97AC3}
# name, operation (0/1=resubmit; 2=cancel; 3=step active then cancel), queue,
# offset, current worker record node, missing lookup key
CASES = (
    ('same_key_resubmit', 0, [NODE], 0, 0, False),
    ('active_same_key_resubmit', 1, [NODE], 0, NODE, False),
    ('queued_front', 2, [NODE, OTHER], 0, 0, False),
    ('active_front_two_steps', 3, [NODE, OTHER], 0, NODE, False),
    ('queued_middle_active_other', 2, [OTHER, NODE, THIRD], 0, OTHER, False),
    ('wrapped_front', 2, [NODE, OTHER], 31, NODE, False),
    ('wrapped_middle', 2, [OTHER, NODE, THIRD], 31, OTHER, False),
    ('completed_empty', 2, [], 0, 0, False),
    ('completed_other_active', 2, [OTHER], 0, OTHER, False),
    ('absent_key', 2, [NODE, OTHER], 0, NODE, True),
)

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
#define M 0x780000u
#define CHECK(x) do{if(!(x)){fprintf(stderr,"route cancel line%d: %s\n",__LINE__,#x);exit(2);}}while(0)
void sub_0013A8BD(void);void sub_0013A8C2(void);void sub_0013A8D0(void);
static void start(void){CHECK(g_ecx==0x750000);MEM32(g_ecx+0x1C)=MEM32(g_esp+4);
 MEM32(g_ecx+0x20)=1;MEM32(g_ecx+0x24)=1;MEM32(M+12)=MEM32(M+16);g_eax=1;g_esp+=8;}
static void step(void){CHECK(g_ecx==0x750000&&MEM32(g_ecx+0x1C));
 if(MEM32(M+12))--MEM32(M+12);g_eax=MEM32(M+12)!=0;g_esp+=4;}
static void reset(void){++MEM32(M+4);sub_0013A8BD();}
recomp_func_t recomp_lookup(uint32_t va){return va==0x13000?start:va==0x13100?step:
 va==0x13A8BD?reset:va==0x13A8C2?sub_0013A8C2:va==0x13A8D0?sub_0013A8D0:NULL;}
recomp_func_t recomp_lookup_manual(uint32_t va){(void)va;return NULL;}
recomp_func_t recomp_lookup_kernel(uint32_t va){(void)va;return NULL;}
void recomp_icall_not_code_log(uint32_t va){(void)va;CHECK(0);}
void recomp_icall_fail_log(uint32_t va){(void)va;CHECK(0);}
void sub_0005B6DA(void){memcpy((void *)XBOX_PTR(g_ebx),(void *)XBOX_PTR(g_eax),0x5C);g_esp+=4;}
void sub_0014EBDA(void){g_esp+=4;}
void sub_0005B311(void){g_esp+=8;}
void sub_00170ED1(void){uint32_t node=MEM32(g_esp+4);CHECK(node==0x770000||node==0x771000);
 CHECK(MEM32(0x75001C)!=node+0x10);unsigned count=MEM32(M);CHECK(count<8);
 MEM32(M+0x100+count*4)=node;MEM32(M)=count+1;memset((void *)XBOX_PTR(node),3,0x6E);g_esp+=4;}
void sub_0005BB15(void){uint32_t source=MEM32(g_esp+20),parent=MEM32(g_esp+12);
 CHECK(!MEM32(M+8));++MEM32(M+8);MEM32(0x771000)=0x721000;MEM32(0x771004)=parent;MEM32(0x771008)=0x721000;
 memcpy((void *)XBOX_PTR(0x77100C),(void *)XBOX_PTR(source),0x60);MEM8(0x77106C)=MEM8(0x77106D)=0;
 g_eax=0x771000;g_esp+=28;}
void sub_0014E9B7(void){CHECK(0);}
void sub_000443B9(void){CHECK(0);}
void sub_00170EEE(void){CHECK(0);}
void sub_000125EA(void){CHECK(0);}
void sub_00013BA6(void){CHECK(0);}
void sub_00097AC3(void){CHECK(0);}
#include "body.inc"
static void call(recomp_func_t fn,const uint32_t *args,unsigned count,uint32_t entry_esi){
 g_eax=0x11223344;g_ecx=0x22334455;g_edx=0x33445566;g_esp=0x700000;
 g_ebx=0x12345678;g_esi=entry_esi;g_edi=0x3456789A;g_ebp=g_seh_ebp=0x456789AB;
 MEM32(g_esp)=0x600000;for(unsigned i=0;i<count;++i)MEM32(g_esp+4+i*4)=args[i];fn();
 CHECK(g_esp==0x700004+count*4&&g_ebx==0x12345678&&g_esi==entry_esi&&g_edi==0x3456789A&&g_ebp==0x456789AB);
 uint32_t regs[]={g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp};
 unsigned index=MEM32(M+0x2FC)++;CHECK(index<8);memcpy((void *)XBOX_PTR(M+0x200+index*32),regs,sizeof regs);
}
int main(int argc,char **argv){CHECK(argc==4);unsigned char *ram=malloc(0x800000);CHECK(ram);
 FILE *in=fopen(argv[1],"rb");CHECK(in&&fread(ram,1,0x800000,in)==0x800000);fclose(in);g_xbox_mem_offset=(ptrdiff_t)ram;
 unsigned operation=MEM32(M+0x30);uint32_t key=MEM32(M+0x34);
 const uint32_t submit[]={0x720000,0x761000,0x760000,0},drive[]={0},retrieve[]={0x720000,0x760000,0x762000},cancel[]={0x720000,key};
 if(operation<2){
  call(sub_0024FA01,submit,4,0x23456789);MEM32(M+0x14)=MEM32(0x720020);
  for(unsigned i=0;i<MEM32(M+16);++i)call(sub_0024F837,drive,1,0x720000);
  MEM32(M+0x18)=MEM32(0x720020);call(sub_0024F917,retrieve,3,0x23456789);
 }else{
  if(operation==3){call(sub_0024F837,drive,1,0x720000);MEM32(M+0x14)=MEM32(0x720020);CHECK(MEM32(0x75001C)==0x770010);}
  call(sub_0024F964,cancel,2,0x23456789);
 }
 MEM32(M+0x1C)=MEM32(0x720020);unsigned offset=MEM32(0x72001C),size=MEM32(0x720018);
 uint32_t front=MEM32(0x720020)?MEM32(MEM32(0x730000+((offset/4)%size)*4)+(offset%4)*4):0;
 MEM32(M+0x20)=front;for(unsigned i=0;i<MEM32(M);++i)if(MEM32(M+0x100+i*4)==front)MEM32(M+0x24)=1;
 MEM32(M+0x28)=MEM32(0x75001C);
 FILE *out=fopen(argv[2],"wb");CHECK(out&&fwrite(ram,1,0x800000,out)==0x800000);fclose(out);
 if(atoi(argv[3]))CHECK(!MEM32(M+0x24));free(ram);return 0;
}
'''


def production_bodies():
    all_bodies = {}
    for path in (ROOT / 'src/recomp/gen').glob('recomp_*.c'):
        for match in re.finditer(r'void sub_([0-9A-F]+)\(void\)\n\{.*?\n\}', path.read_text(), re.S):
            all_bodies[int(match.group(1), 16)] = match.group()
    pending, found = list(ENTRIES) + [0x13A8BD, 0x13A8C2, 0x13A8D0], {}
    while pending:
        va = pending.pop()
        if va in found or va in TERMINALS:
            continue
        found[va] = all_bodies[va]
        pending.extend(int(value, 16) for value in re.findall(r'RECOMP_ABI_CALL\(0x([0-9A-F]+)u', found[va]))
    assert len(found) == 28
    return found


def apply_overlay(directory, original):
    script = directory / 'seam.cmake'
    script.write_text(f'''include("{(ROOT / 'scripts/mm3_route_cancel_seam.cmake').as_posix()}")
include("{(ROOT / 'scripts/mm3_route_diagnostics_seam.cmake').as_posix()}")
file(READ "${{INPUT}}" source)
if(OBSERVE)
 mm3_add_route_diagnostics(source sub_0024F964)
endif()
mm3_fix_route_cancellation(source)
file(WRITE "${{OUTPUT}}" "${{source}}")
''')
    inp, out = directory / 'input.c', directory / 'fixed.c'
    before = '    if (CMP_NE(_fa, _fb)) goto loc_0024F9F8; /* jne: not equal / not zero */'
    after = '    /* Native PC: remove a found pending entry; never erase the end iterator. */\n    if (CMP_EQ(_fa, _fb)) goto loc_0024F9E9;'
    assert original.count(before) == 2
    for newline in ('\n', '\r\n'):
        inp.write_bytes(original.replace('\n', newline).encode())
        subprocess.run(['cmake', '-DINPUT=' + str(inp), '-DOUTPUT=' + str(out), '-P', str(script)], check=True)
        fixed = out.read_text()
        assert fixed.count(after) == 1 and fixed.replace(after, before) == original
        subprocess.run(['cmake', '-DOBSERVE=1', '-DINPUT=' + str(inp), '-DOUTPUT=' + str(out), '-P', str(script)], check=True)
        observed = out.read_text()
        assert after in observed and observed.count('mm3_route_lifetime(') == 2
    for invalid in (original.replace('loc_0024F9B7: ;', 'loc_0024F9B8: ;'),
                    original.replace('0x0005BCAFu', '0x0005BCAEu'),
                    original.replace('0x0005B400u', '0x0005B401u'),
                    original.replace('esp += 12; return;', 'esp += 8; return;'),
                    original + '\n' + original, fixed):
        inp.write_text(invalid)
        result = subprocess.run(['cmake', '-DINPUT=' + str(inp), '-DOUTPUT=' + str(out), '-P', str(script)], capture_output=True, text=True)
        assert result.returncode and 'MM3 route cancellation:' in result.stderr
    return fixed


def fixture(image, case):
    _, operation, queue, offset, active, missing = case
    ram = bytearray(0x800000)
    for va, raw, size, _ in image.secs:
        ram[va:va + size] = image.d[raw:raw + size]
    put = lambda va, *values: struct.pack_into('<' + 'I' * len(values), ram, va, *values)
    put(S, WORKER); put(S + 8, HEADER, 1); put(HEADER, NODE, NODE, NODE)
    ram[HEADER + 0x6C:HEADER + 0x6E] = b'\x01\x01'
    put(NODE, HEADER, HEADER, HEADER, KEY)
    ram[NODE + 0x6C:NODE + 0x6E] = b'\x01\x00'
    put(NODE + 0x40, GRAPH)
    put(S + 0x14, MAP, 8, offset, len(queue), GRAPH)
    for index in range(8):
        put(MAP + index * 4, BLOCKS + index * 0x100)
    for index, node in enumerate(queue):
        slot = (offset + index) % 32
        put(BLOCKS + (slot // 4) * 0x100 + (slot % 4) * 4, node)
        if node != NODE:
            put(node + 0xC, OTHER_KEY)
    put(WORKER, VTABLE)
    put(VTABLE + 4, 0x13000, 0x13100, 0x13A8D0, 0x13A8C2, 0x13A8BD)
    if active:
        put(WORKER + 0x1C, active + 0x10, 1, 1)
    steps = 2 if operation in (1, 3) else 1
    put(META + 12, steps, steps); put(META + 0x30, operation, OTHER_KEY if missing else KEY)
    put(0x3A79C0, 1)
    return ram


class Native:
    def __init__(self, ram, fixed):
        self.m = common.unicorn.Uc(common.unicorn.UC_ARCH_X86, common.unicorn.UC_MODE_32)
        self.m.mem_map(0, len(ram)); self.m.mem_write(0, bytes(ram))
        if fixed:
            assert bytes(self.m.mem_read(0x24F9BA, 2)) == b'\x75\x3C'
            self.m.mem_write(0x24F9BA, b'\x74\x2D')
        self.m.hook_add(common.unicorn.UC_HOOK_CODE, self.hook)

    def get(self, address):
        return struct.unpack('<I', self.m.mem_read(address, 4))[0]

    def put(self, address, *values):
        self.m.mem_write(address, struct.pack('<' + 'I' * len(values), *values))

    def reg(self, name):
        return self.m.reg_read(getattr(common.x86, 'UC_X86_REG_' + name))

    def setreg(self, name, value):
        self.m.reg_write(getattr(common.x86, 'UC_X86_REG_' + name), value)

    def ret(self, cleanup):
        sp = self.reg('ESP'); target = self.get(sp)
        self.setreg('ESP', sp + 4 + cleanup); self.setreg('EIP', target)

    def hook(self, machine, va, size, unused):
        sp = self.reg('ESP')
        if va == 0x5B6DA:
            machine.mem_write(self.reg('EBX'), bytes(machine.mem_read(self.reg('EAX'), 0x5C))); self.ret(0)
        elif va == 0x14EBDA:
            self.ret(0)
        elif va == 0x5B311:
            self.ret(4)
        elif va == 0x170ED1:
            node = self.get(sp + 4); count = self.get(META)
            assert node in (NODE, NEW) and self.get(WORKER + 0x1C) != node + 0x10 and count < 8
            self.put(META + 0x100 + count * 4, node); self.put(META, count + 1)
            machine.mem_write(node, bytes([3]) * 0x6E); self.ret(0)
        elif va == 0x5BB15:
            assert not self.get(META + 8)
            self.put(META + 8, 1); self.put(NEW, HEADER, self.get(sp + 12), HEADER)
            machine.mem_write(NEW + 0xC, bytes(machine.mem_read(self.get(sp + 20), 0x60)))
            machine.mem_write(NEW + 0x6C, b'\0\0'); self.setreg('EAX', NEW); self.ret(24)
        elif va == 0x13000:
            assert self.reg('ECX') == WORKER
            self.put(WORKER + 0x1C, self.get(sp + 4), 1, 1)
            self.put(META + 12, self.get(META + 16)); self.setreg('EAX', 1); self.ret(4)
        elif va == 0x13100:
            assert self.reg('ECX') == WORKER and self.get(WORKER + 0x1C)
            remaining = max(0, self.get(META + 12) - 1)
            self.put(META + 12, remaining); self.setreg('EAX', int(remaining != 0)); self.ret(0)
        elif va == 0x13A8BD:
            self.put(META + 4, self.get(META + 4) + 1)  # Observe real reset, then execute it.
        elif va in TERMINALS:
            raise AssertionError(('unexpected native boundary', hex(va)))

    def call(self, entry, args, esi=0x23456789):
        values = (0x11223344, 0x22334455, 0x33445566, STACK, 0x12345678, esi, 0x3456789A, 0x456789AB)
        for name, value in zip(REGS, values):
            self.setreg(name, value)
        self.put(STACK, RETURN, *args); self.m.emu_start(entry, RETURN, count=30000)
        assert self.reg('EIP') == RETURN and self.reg('ESP') == STACK + 4 + len(args) * 4
        assert tuple(self.reg(name) for name in REGS[4:]) == values[4:]
        index = self.get(META + 0x2FC); self.put(META + 0x2FC, index + 1)
        self.put(META + 0x200 + index * 32, *(self.reg(name) for name in REGS))

    def run(self):
        operation, key = self.get(META + 0x30), self.get(META + 0x34)
        if operation < 2:
            self.call(0x24FA01, [S, REQUEST, KEY, 0]); self.put(META + 0x14, self.get(S + 0x20))
            for _ in range(self.get(META + 16)):
                self.call(0x24F837, [0], S)
            self.put(META + 0x18, self.get(S + 0x20)); self.call(0x24F917, [S, KEY, OUTPUT])
        else:
            if operation == 3:
                self.call(0x24F837, [0], S); self.put(META + 0x14, self.get(S + 0x20))
                assert self.get(WORKER + 0x1C) == NODE + 0x10
            self.call(0x24F964, [S, key])
        count, offset, size = self.get(S + 0x20), self.get(S + 0x1C), self.get(S + 0x18)
        front = self.get(self.get(MAP + ((offset // 4) % size) * 4) + (offset % 4) * 4) if count else 0
        self.put(META + 0x1C, count, front)
        self.put(META + 0x24, int(front in [self.get(META + 0x100 + n * 4) for n in range(self.get(META))]))
        self.put(META + 0x28, self.get(WORKER + 0x1C))
        return bytes(self.m.mem_read(0, 0x800000))


def validate(ram, case, fixed):
    get = lambda address: struct.unpack_from('<I', ram, address)[0]
    name, operation, queue, offset, active, missing = case
    count, remaining, worker = get(META + 0x1C), get(META + 0x20), get(META + 0x28)
    if operation < 2:
        assert get(META + 0x14) == (1 if fixed else 2), name
        assert count == (0 if fixed else 1) and bool(get(META + 0x24)) != fixed, name
        assert worker == 0 and get(META + 0x18) == count, name
    elif fixed:
        expected = queue if missing else [node for node in queue if node != NODE]
        actual = []
        for index in range(count):
            slot = (get(S + 0x1C) + index) % 32
            actual.append(get(BLOCKS + (slot // 4) * 0x100 + (slot % 4) * 4))
        assert actual == expected, (name, actual, expected)
        assert worker == (active + 0x10 if active and (active != NODE or missing) else 0), name
        assert get(META) == int(not missing), name
        assert get(META + 4) == int(not missing and bool(queue) and queue[0] == NODE), name
        assert not get(META + 0x24), name
    if operation == 3:
        assert get(META + 0x14) == len(queue), 'Accepted multi-step work must stay queued until completion.'


def main():
    if not shutil.which('cl'):
        raise SystemExit('Run scripts/enter-env.ps1 first.')
    image = common.Image(str(ROOT / 'game_files/default.xbe'))
    assert image.d[image.raw(0x24F9BA):image.raw(0x24F9BA) + 2] == b'\x75\x3C'
    found = production_bodies()
    with tempfile.TemporaryDirectory(prefix='mm3-route-cancel-') as temporary:
        directory = Path(temporary); fixed_body = apply_overlay(directory, found[0x24F964])
        executables = []
        declarations = '\n'.join(f'void sub_{va:08X}(void);' for va in found)
        for fixed in (False, True):
            folder = directory / ('fixed' if fixed else 'original'); folder.mkdir()
            body = []
            for va, original in found.items():
                source = fixed_body if fixed and va == 0x24F964 else original
                if va in ENTRIES:
                    # Publish the final generated C-local guest EBP for exact x86 comparison.
                    source, count = re.subn(r'(esp \+= \d+; return; /\* ret(?: \d+)? \*/)', r'g_ebp = ebp; \1', source)
                    assert count == 1
                body.append(source)
            (folder / 'body.inc').write_text(declarations + '\n' + '\n'.join(body))
            source, exe = folder / 'contract.c', folder / 'contract.exe'; source.write_text(HARNESS)
            subprocess.run(['cl', '/nologo', '/O2', '/MD', '/std:c11', '/D_CRT_SECURE_NO_WARNINGS',
                '/I' + str(ROOT / 'src'), '/I' + str(ROOT / 'tools/xboxrecomp/include'),
                '/Fo:' + str(folder / 'contract.obj'), '/Fe:' + str(exe), str(source)], cwd=folder, check=True)
            executables.append(exe)
        for case in CASES:
            ram = fixture(image, case); inp, out = directory / 'image.bin', directory / 'result.bin'; inp.write_bytes(ram)
            for fixed in (False, True):
                expected = Native(ram, fixed).run(); validate(expected, case, fixed)
                subprocess.run([str(executables[int(fixed)]), str(inp), str(out), str(int(fixed))], check=True)
                actual = out.read_bytes()
                if actual[META:META + 0x38] != expected[META:META + 0x38]:
                    print('metadata', case[0], fixed, struct.unpack_from('<14I', actual, META), struct.unpack_from('<14I', expected, META), flush=True)
                validate(actual, case, fixed)
                for start, end in ((STACK - 0x300, STACK + 0x30), (S, S + 0x30), (HEADER, HEADER + 0x70),
                                   (MAP, MAP + 32), (BLOCKS, BLOCKS + 0x800), (WORKER, WORKER + 0x30),
                                   (NODE, THIRD + 0x80), (META, META + 0x300)):
                    assert actual[start:end] == expected[start:end], (case[0], fixed, hex(start))
            if case[1] < 2:
                broken = subprocess.run([str(executables[0]), str(inp), str(out), '1'], capture_output=True, text=True)
                assert broken.returncode == 2 and '!MEM32(M+0x24)' in broken.stderr
        print('PASS: 10 original/fixed native-XBE and production C lifecycles; duplicate/freed-queue old controls, active multi-step reset, front/middle/wrapped/absent/completed cancellation, unrelated worker ownership, exact registers/ESP and diagnostic-overlay composition')


if __name__ == '__main__':
    main()
