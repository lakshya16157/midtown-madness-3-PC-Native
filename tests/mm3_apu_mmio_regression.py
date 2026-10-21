"""Compare the two production APU seams with original XBE MMIO sequences.

Only lock/payload helpers and the MMIO device are controlled. All guest loops,
branches, recursion, voice selection and FIFO reads execute from actual native
bytes or transformed generated C. The device can change voice counts mid-loop.
"""
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile

import mm3_x87_exp_regression as common

ROOT = common.ROOT
ENTRIES = (0x27DCDA, 0x27DD72)
STACK, RETURN, OWNER, PROPS, META = 0x700000, 0x600000, 0x720000, 0x730000, 0x780000
TRACE = META + 0x1000
FIFO, SELECT, UPDATE, X, Y, Z, SCALAR = 0xFE820010, 0xFE8202F8, 0xFE8202FC, 0xFE820360, 0xFE820364, 0xFE820368, 0xFE82037C
REGS = ('EAX', 'ECX', 'EDX', 'ESP', 'EBX', 'ESI', 'EDI', 'EBP')
INITIAL = (0x11223344, OWNER, 0x55667788, STACK, 0x12345678, 0x23456789, 0x3456789A, 0x456789AB)
# name, entry, voices, enabled, mutation (0=no change), container, immediate FIFO
CASES = (
    ('position_inactive', ENTRIES[0], 3, False, 0, False, False),
    ('position_zero', ENTRIES[0], 0, True, 0, False, False),
    ('position_one', ENTRIES[0], 1, True, 0, False, False),
    ('position_three_fifo_wait', ENTRIES[0], 3, True, 0, False, False),
    ('position_shrink_after_first', ENTRIES[0], 3, True, 1, False, False),
    ('position_grow_after_first', ENTRIES[0], 1, True, 3, False, False),
    ('position_immediate_fifo', ENTRIES[0], 3, True, 0, False, True),
    ('scalar_inactive', ENTRIES[1], 3, False, 0, False, False),
    ('scalar_zero', ENTRIES[1], 0, True, 0, False, False),
    ('scalar_one', ENTRIES[1], 1, True, 0, False, False),
    ('scalar_three_fifo_wait', ENTRIES[1], 3, True, 0, False, False),
    ('scalar_shrink_after_first', ENTRIES[1], 3, True, 1, False, False),
    ('scalar_grow_after_first', ENTRIES[1], 1, True, 3, False, False),
    ('scalar_empty_container', ENTRIES[1], 0, True, 0, True, False),
    ('scalar_recursive_two_children', ENTRIES[1], 2, True, 0, True, False),
)

HARNESS = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define RECOMP_GENERATED_CODE
#include "mm3_recomp_types.h"
ptrdiff_t g_xbox_mem_offset;
uint32_t g_xbox_code_lo=0x12000,g_xbox_code_hi=0x350000;
RECOMP_TLS uint32_t g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp,g_seh_ebp,g_fs_base,g_eflags;
RECOMP_TLS int g_fp_top,g_df,g_fp_cmp;
RECOMP_TLS uint16_t g_fp_control_word,g_fp_cc;
RECOMP_TLS double g_fp_stack[8];
RECOMP_TLS RecompXmm g_xmm0,g_xmm1,g_xmm2,g_xmm3,g_xmm4,g_xmm5,g_xmm6,g_xmm7;
#define M 0x780000u
#define T (M+0x1000u)
#define CHECK(v) do{if(!(v)){fprintf(stderr,"APU MMIO line%d: %s\n",__LINE__,#v);exit(2);}}while(0)
static void event(uint32_t kind,uint32_t address,uint32_t value,uint32_t extra){
 unsigned count=MEM32(M+4);CHECK(count<128);uint32_t p=T+count*16;
 MEM32(p)=kind;MEM32(p+4)=address;MEM32(p+8)=value;MEM32(p+12)=extra;MEM32(M+4)=count+1;
}
uint32_t mm3_apu_mmio_read32(uint32_t address){
 CHECK(address==0xFE820010);uint32_t index=MEM32(M+8),count=MEM32(M+12);
 uint32_t value=MEM32(M+0x100+(index<count?index:count-1)*4);MEM32(M+8)=index+1;
 event(1,address,value,0);return value;
}
void mm3_apu_mmio_write32(uint32_t address,uint32_t value){
 CHECK(address==0xFE8202F8||address==0xFE8202FC||address==0xFE820360||
       address==0xFE820364||address==0xFE820368||address==0xFE82037C);
 event(2,address,value,0);
 if(MEM32(M+0x1C)&&!MEM32(M+0x20)&&
    ((address==0xFE8202FC&&value==0)||address==0xFE82037C)){
  MEM8(MEM32(M+0x18)+0x64)=(uint8_t)MEM32(M+0x1C);MEM32(M+0x20)=1;
 }
}
void sub_002764D0(void){
 event(3,0x2764D0,g_ecx,0);MEM32(g_ecx)=0xACED1234;g_eax=0x10101010;g_ecx=0x20202020;g_edx=0x30303030;g_esp+=4;
}
void sub_00276503(void){
 event(3,0x276503,g_ecx,0);MEM32(g_ecx)=0xBAD01234;g_eax=0x40404040;g_ecx=0x50505050;g_edx=0x60606060;g_esp+=4;
}
void sub_0027CC4F(void){
 uint32_t output=MEM32(g_esp+4);event(3,0x27CC4F,g_ecx,output);
 for(unsigned i=0;i<9;++i)MEM32(output+i*4)=0x3F000000u+(g_ecx-0x720000)+i*0x8000;
 g_eax=0x11111111;g_ecx=0x22222222;g_edx=0x33333333;g_esp+=8;
}
void sub_0027CE4C(void){
 uint32_t output=MEM32(g_esp+4);event(3,0x27CE4C,g_ecx,output);MEM32(output)=0x40490FDBu+(g_ecx-0x720000);
 g_eax=0x44444444;g_ecx=0x55555555;g_edx=0x66666666;g_esp+=8;
}
#include "body.inc"
int main(int argc,char **argv){
 CHECK(argc==4);unsigned char *ram=malloc(0x800000);CHECK(ram);
 FILE *in=fopen(argv[1],"rb");CHECK(in&&fread(ram,1,0x800000,in)==0x800000);fclose(in);g_xbox_mem_offset=(ptrdiff_t)ram;
 g_eax=0x11223344;g_ecx=0x720000;g_edx=0x55667788;g_esp=0x700000;
 g_ebx=0x12345678;g_esi=0x23456789;g_edi=0x3456789A;g_ebp=g_seh_ebp=0x456789AB;
 g_fs_base=0x123000;g_eflags=0x55AA;g_df=1;g_fp_top=3;g_fp_cmp=2;g_fp_control_word=0x27F;g_fp_cc=0x4500;
 for(unsigned i=0;i<8;++i)g_fp_stack[i]=100.25+i;
 RecompXmm initial_xmm;memset(&initial_xmm,0xA5,sizeof initial_xmm);
 g_xmm0=g_xmm1=g_xmm2=g_xmm3=g_xmm4=g_xmm5=g_xmm6=g_xmm7=initial_xmm;
 if(MEM32(M)==0x27DCDA)sub_0027DCDA();else sub_0027DD72();
 CHECK(g_eax==0&&g_esp==0x700004&&g_ebx==0x12345678&&g_esi==0x23456789&&g_edi==0x3456789A&&g_ebp==0x456789AB);
 CHECK(g_fs_base==0x123000&&g_eflags==0x55AA&&g_df==1&&g_fp_top==3&&g_fp_cmp==2&&g_fp_control_word==0x27F&&g_fp_cc==0x4500);
 CHECK(g_seh_ebp==0x6FFFFC);
 for(unsigned i=0;i<8;++i)CHECK(g_fp_stack[i]==100.25+i);
 RecompXmm actual_xmm[]={g_xmm0,g_xmm1,g_xmm2,g_xmm3,g_xmm4,g_xmm5,g_xmm6,g_xmm7};
 for(unsigned i=0;i<8;++i)CHECK(!memcmp(&actual_xmm[i],&initial_xmm,sizeof initial_xmm));
 FILE *out=fopen(argv[2],"wb");CHECK(out&&fwrite(ram,1,0x800000,out)==0x800000);fclose(out);
 uint32_t regs[]={g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp};
 out=fopen(argv[3],"wb");CHECK(out&&fwrite(regs,1,sizeof regs,out)==sizeof regs);fclose(out);free(ram);return 0;
}
'''


def bodies():
    result = {}
    for path in (ROOT / 'src/recomp/gen').glob('recomp_*.c'):
        source = path.read_text()
        for address in ENTRIES:
            match = re.search(rf'void sub_{address:08X}\(void\)\n\{{.*?\n\}}', source, re.S)
            if match:
                assert address not in result
                result[address] = match.group()
    assert len(result) == 2
    return result


def transform(folder, originals):
    script = folder / 'seam.cmake'
    script.write_text(f'''include("{(ROOT / 'scripts/mm3_apu_mmio_seam.cmake').as_posix()}")
file(READ "${{INPUT}}" source)
foreach(owner sub_0027DCDA sub_0027DD72)
 mm3_add_apu_mmio(source ${{owner}})
endforeach()
file(WRITE "${{OUTPUT}}" "${{source}}")
''')
    original = '\n\n'.join(originals.values())
    inp, out = folder / 'input.c', folder / 'fixed.c'
    fixed = None
    reverse = {
        'edx = mm3_apu_mmio_read32(0xFE820010u);': 'edx = MEM32(-25034736);',
        'mm3_apu_mmio_write32(0xFE8202F8u, edx);': 'MEM32(-25033992) = edx;',
        'mm3_apu_mmio_write32(0xFE8202FCu, 1);': 'MEM32(-25033988) = 1;',
        'mm3_apu_mmio_write32(0xFE8202FCu, 0);': 'MEM32(-25033988) = 0;',
        'mm3_apu_mmio_write32(0xFE820360u, edx);': 'MEM32(-25033888) = edx;',
        'mm3_apu_mmio_write32(0xFE820364u, edx);': 'MEM32(-25033884) = edx;',
        'mm3_apu_mmio_write32(0xFE820368u, edx);': 'MEM32(-25033880) = edx;',
        'mm3_apu_mmio_write32(0xFE82037Cu, edx);': 'MEM32(-25033860) = edx;',
    }
    for newline in ('\n', '\r\n'):
        inp.write_bytes(original.replace('\n', newline).encode())
        subprocess.run(['cmake', '-DINPUT=' + str(inp), '-DOUTPUT=' + str(out), '-P', str(script)], check=True)
        fixed = out.read_text()
        assert fixed.count('#include "mm3_apu_mmio.h"') == 1
        assert fixed.count('mm3_apu_mmio_read32(') == 2 and fixed.count('mm3_apu_mmio_write32(') == 8
        assert not re.search(r'MEM32\(-\d+\)', fixed)
        restored = fixed.replace('#include "mm3_apu_mmio.h"\n', '', 1)
        for after, before in reverse.items():
            restored = restored.replace(after, before)
        assert restored == original, 'Seam changed more than the ten constant accesses.'
        raw = out.read_bytes()
        if newline == '\r\n':
            assert b'\n' not in raw.replace(b'\r\n', b'')
    invalid = [original.replace('MEM32(-25034736)', 'MEM32(-25034732)', 1),
               original.replace('MEM32(-25033988) = 1', 'MEM32(-25033988) = 2'),
               original.replace('MEM32(-25033860)', 'MEM16(-25033860)'),
               original.replace('esp += 4; return; /* ret */', 'esp += 8; return; /* ret */', 1),
               original.replace('0x0027CC4Fu', '0x0027CC4Eu'),
               original.replace('loc_0027DD09: ;', 'loc_0027DD0A: ;'),
               original + '\n' + originals[ENTRIES[0]], fixed,
               original.replace('    MEM32(-25033860) = edx;', '    MEM32(-25033860) = edx;\n    MEM32(-25033860) = edx;')]
    for source in invalid:
        inp.write_text(source)
        result = subprocess.run(['cmake', '-DINPUT=' + str(inp), '-DOUTPUT=' + str(out), '-P', str(script)], capture_output=True, text=True)
        assert result.returncode and 'MM3 APU MMIO:' in result.stderr
    return fixed


def fixture(image, case):
    _, entry, voices, enabled, mutation, container, immediate = case
    ram = bytearray(0x800000)
    for address, raw, size, _ in image.secs:
        ram[address:address + size] = image.d[raw:raw + size]
    put = lambda address, *values: struct.pack_into('<' + 'I' * len(values), ram, address, *values)
    put(STACK, RETURN)
    put(META, entry, 0, 0, 1 if immediate else 4)
    put(META + 0x18, OWNER, mutation)
    put(META + 0x100, *( [0xFFFFFFFF] if immediate else [0, 4, 16, 128]))
    def owner(address, count, active, flags=0):
        for index in range(3):
            struct.pack_into('<H', ram, address + 0xC + index*2, (0xFE10 + index*101 + (address - OWNER)//16) & 0xFFFF)
        ram[address + 0x12] = int(active)
        ram[address + 0x64] = count
        props = PROPS + address - OWNER
        put(address + 0x78, props)
        put(props + 8, flags)
    owner(OWNER, voices, enabled, 0x182000 if container else 0)
    if container:
        if voices:
            first, second = OWNER + 0x1000, OWNER + 0x2000
            owner(first, 2, True); owner(second, 3, True)
            put(OWNER + 0x44, first + 0x5C)
            put(first + 0x5C, second + 0x5C)
            put(second + 0x5C, OWNER + 0x44)
        else:
            put(OWNER + 0x44, OWNER + 0x44)
    return ram


class Native:
    def __init__(self, ram):
        self.m = common.unicorn.Uc(common.unicorn.UC_ARCH_X86, common.unicorn.UC_MODE_32)
        self.m.mem_map(0, len(ram)); self.m.mem_write(0, bytes(ram))
        self.m.mem_map(0xFE820000, 0x1000)
        self.m.hook_add(common.unicorn.UC_HOOK_CODE, self.code)
        self.m.hook_add(common.unicorn.UC_HOOK_MEM_READ, self.read_mmio, begin=0xFE820000, end=0xFE820FFF)
        self.m.hook_add(common.unicorn.UC_HOOK_MEM_WRITE, self.write_mmio, begin=0xFE820000, end=0xFE820FFF)

    def get(self, address):
        return struct.unpack('<I', self.m.mem_read(address, 4))[0]

    def put(self, address, *values):
        self.m.mem_write(address, struct.pack('<' + 'I' * len(values), *values))

    def reg(self, name):
        return self.m.reg_read(getattr(common.x86, 'UC_X86_REG_' + name))

    def setreg(self, name, value):
        self.m.reg_write(getattr(common.x86, 'UC_X86_REG_' + name), value)

    def event(self, kind, address, value, extra=0):
        count = self.get(META + 4); assert count < 128
        self.put(TRACE + count*16, kind, address, value, extra); self.put(META + 4, count + 1)

    def ret(self, cleanup):
        sp = self.reg('ESP'); target = self.get(sp)
        self.setreg('ESP', sp + 4 + cleanup); self.setreg('EIP', target)

    def read_mmio(self, machine, access, address, size, unused_value, unused):
        assert address == FIFO and size == 4
        index, count = self.get(META + 8), self.get(META + 12)
        value = self.get(META + 0x100 + min(index, count - 1)*4)
        self.put(META + 8, index + 1); self.put(address, value); self.event(1, address, value)

    def write_mmio(self, machine, access, address, size, value, unused):
        assert size == 4 and address in (SELECT, UPDATE, X, Y, Z, SCALAR)
        self.event(2, address, value)
        if self.get(META + 0x1C) and not self.get(META + 0x20) and ((address == UPDATE and value == 0) or address == SCALAR):
            self.m.mem_write(self.get(META + 0x18) + 0x64, bytes([self.get(META + 0x1C)]))
            self.put(META + 0x20, 1)

    def code(self, machine, address, size, unused):
        frame, sp = self.reg('ECX'), self.reg('ESP')
        if address in (0x2764D0, 0x276503):
            self.event(3, address, frame)
            self.put(frame, 0xACED1234 if address == 0x2764D0 else 0xBAD01234)
            values = (0x10101010, 0x20202020, 0x30303030) if address == 0x2764D0 else (0x40404040, 0x50505050, 0x60606060)
            for name, value in zip(('EAX', 'ECX', 'EDX'), values): self.setreg(name, value)
            self.ret(0)
        elif address in (0x27CC4F, 0x27CE4C):
            output = self.get(sp + 4); self.event(3, address, frame, output)
            if address == 0x27CC4F:
                self.put(output, *(0x3F000000 + frame - OWNER + index*0x8000 for index in range(9)))
                values = (0x11111111, 0x22222222, 0x33333333)
            else:
                self.put(output, 0x40490FDB + frame - OWNER)
                values = (0x44444444, 0x55555555, 0x66666666)
            for name, value in zip(('EAX', 'ECX', 'EDX'), values): self.setreg(name, value)
            self.ret(4)

    def run(self):
        for name, value in zip(REGS, INITIAL): self.setreg(name, value)
        self.m.emu_start(self.get(META), RETURN, count=10000)
        assert self.reg('EIP') == RETURN
        return tuple(self.reg(name) for name in REGS), bytes(self.m.mem_read(0, 0x800000))


def main():
    if not shutil.which('cl'):
        raise SystemExit('Run scripts/enter-env.ps1 first.')
    image = common.Image(str(ROOT / 'game_files/default.xbe'))
    # Exact native first FIFO read addresses, voice select writes and RETs.
    assert image.d[image.raw(0x27DD09):image.raw(0x27DD09)+6] == b'\x8B\x15\x10\x00\x82\xFE'
    assert image.d[image.raw(0x27DDC3):image.raw(0x27DDC3)+6] == b'\x8B\x15\x10\x00\x82\xFE'
    assert image.d[image.raw(0x27DD71)] == image.d[image.raw(0x27DE03)] == 0xC3
    with tempfile.TemporaryDirectory(prefix='mm3-apu-mmio-') as temporary:
        folder = Path(temporary); fixed = transform(folder, bodies())
        # Generated EBP is a C-local architectural register. Publish it only in
        # the fixture after LEAVE so the raw-XBE register dump is comparable.
        fixed, count = re.subn(r'(POP32\(esp, ebp\); /\* leave \*/)', r'\1\n    g_ebp = ebp;', fixed)
        assert count == 2
        (folder / 'body.inc').write_text(fixed)
        (folder / 'mm3_apu_mmio.h').write_text('#include <stdint.h>\nuint32_t mm3_apu_mmio_read32(uint32_t);\nvoid mm3_apu_mmio_write32(uint32_t,uint32_t);\n')
        source, executable = folder / 'contract.c', folder / 'contract.exe'; source.write_text(HARNESS)
        for optimization in ('/Od', '/O2'):
            subprocess.run(['cl', '/nologo', '/W4', '/wd4102', '/wd4189', optimization, '/std:c11', '/D_CRT_SECURE_NO_WARNINGS',
                '/I' + str(ROOT / 'src'), '/I' + str(ROOT / 'tools/xboxrecomp/include'),
                '/Fo:' + str(folder / 'contract.obj'), '/Fe:' + str(executable), str(source)], cwd=folder, check=True)
            for case in CASES:
                ram = fixture(image, case); expected_regs, expected_ram = Native(ram).run()
                inp, out, regs = folder / 'image.bin', folder / 'result.bin', folder / 'regs.bin'; inp.write_bytes(ram)
                subprocess.run([str(executable), str(inp), str(out), str(regs)], check=True)
                assert struct.unpack('<8I', regs.read_bytes()) == expected_regs, (optimization, case[0], 'registers')
                actual = out.read_bytes()
                assert actual == expected_ram, (optimization, case[0], 'guest RAM or MMIO/helper event sequence')
        print('PASS:15 original-XBE vs production-C paths at /Od and /O2; exact FIFO polls/read-write/call order, 0/1/3 voices, live count changes, recursive/empty children, all RAM/GPRs/ESP and guest FP/XMM state; strict ten-site LF/CRLF seam')


if __name__ == '__main__':
    main()
