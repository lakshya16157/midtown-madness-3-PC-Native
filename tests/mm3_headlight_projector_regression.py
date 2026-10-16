"""Exercise the native car road-projector producer and submission queue offline.

Run after scripts/enter-env.ps1. Uses generated functions and constants from the
local PAL XBE; no game process, installed asset, or save is opened for writing.
Only the unused ordinary-flare paths are assertion boundaries. Projector
placement, vector/matrix math, packed color, and queue insertion execute the
actual generated native code.
"""
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import zipfile


ROOT = Path(__file__).resolve().parents[1]
NATIVE = (
    "sub_000CF2DA", "sub_00012C43", "sub_00048974", "sub_00259A65",
    "sub_002597D8", "sub_00259851", "sub_0025978C", "sub_00259769",
    "sub_00259180", "sub_00259741", "sub_000489A6", "sub_0022FFD1",
    "sub_002592A5", "sub_00095286", "sub_0022FF8B",
)
UNUSED_FLARES = ("sub_001A779E", "sub_000120B6", "sub_001A842A")


def generated():
    result = {}
    for path in sorted((ROOT / "src/recomp/gen").glob("recomp_*.c")):
        source = path.read_text()
        for name in NATIVE:
            match = re.search(r"^void " + name + r"\(void\)\n\{.*?^\}",
                              source, re.M | re.S)
            if match:
                assert name not in result, name
                result[name] = match.group()
    assert result.keys() == set(NATIVE), set(NATIVE) - result.keys()
    calls = set(re.findall(r"RECOMP_ABI_CALL\(0x[0-9A-Fa-f]+u, (sub_[0-9A-Fa-f]+)\)",
                           "\n".join(result.values())))
    assert calls <= set(NATIVE + UNUSED_FLARES), calls - set(NATIVE + UNUSED_FLARES)
    return "\n\n".join(result[name] for name in NATIVE)


def constants(source):
    xbe = (ROOT / "game_files/default.xbe").read_bytes()
    word = lambda at: struct.unpack_from("<I", xbe, at)[0]
    sections = [struct.unpack_from("<4I", xbe, word(0x120) - word(0x104) + i * 56 + 4)
                for i in range(word(0x11C))]

    def native_word(address):
        section = next(s for s in sections if s[0] <= address <= s[0] + s[3] - 4)
        return word(section[2] + address - section[0])

    addresses = set()
    for kind, address in re.findall(r"MEM([FD])\(0x([0-9A-Fa-f]+)\)", source):
        addresses.add(int(address, 16))
        if kind == "D":
            addresses.add(int(address, 16) + 4)
    return "\nstatic void native_constants(void) {\n" + "".join(
        f"    MEM32(0x{at:X}) = 0x{native_word(at):08X}u;\n"
        for at in sorted(addresses)) + "}\n"


SOURCE = r'''
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp,g_seh_ebp,g_fs_base,g_eflags;
RECOMP_TLS int g_fp_top,g_df,g_fp_cmp;
RECOMP_TLS uint16_t g_fp_control_word,g_fp_cc;
RECOMP_TLS double g_fp_stack[8];
RECOMP_TLS RecompMmx g_mm0,g_mm1,g_mm2,g_mm3,g_mm4,g_mm5,g_mm6,g_mm7;
RECOMP_TLS RecompXmm g_xmm0,g_xmm1,g_xmm2,g_xmm3,g_xmm4,g_xmm5,g_xmm6,g_xmm7;
static unsigned char memory[0x400000];
enum { NODE=0x10000, OWNER=0x11000, MATRIX=0x12000, WORLD=0x13000,
       QUEUE=0x20000, RECORD=0x30000, STACK=0x90000 };
/* These paths would produce an ordinary flare, which kind 8 must bypass. */
void sub_001A779E(void) { assert(!"unexpected flare initialization"); }
void sub_000120B6(void) { assert(!"unexpected flare copy"); }
void sub_001A842A(void) { assert(!"unexpected flare submission"); }
#pragma warning(push)
#pragma warning(disable:4101 4102 4127 4189)
#include "native.inc"
#pragma warning(pop)
#include "constants.inc"

static unsigned submit(uint32_t color,float brightness,float size,unsigned on,unsigned mask)
{
    unsigned i;
    memset(memory,0,sizeof memory);g_xbox_mem_offset=(ptrdiff_t)memory;
    g_eax=g_edx=g_ebx=g_esi=g_edi=g_ebp=g_seh_ebp=g_fs_base=g_eflags=0;
    g_ecx=NODE;g_esp=STACK;g_fp_top=g_df=g_fp_cmp=0;
    g_fp_cc=0;g_fp_control_word=0x27f;
    memset(g_fp_stack,0,sizeof g_fp_stack);native_constants();
    MEM32(0x3c55a0)=WORLD;MEM32(WORLD+0x24c)=0x1234;
    MEM32(0x3c5d8c)=QUEUE;MEM32(QUEUE+0x5410)=RECORD;
    MEM32(NODE+4)=OWNER;MEM32(NODE+0x34)=8;
    MEM32(NODE+0x64)=color;
    MEMF(NODE+0x54)=MEMF(NODE+0x78)=brightness;
    MEMF(NODE+0x5c)=MEMF(NODE+0x7c)=size;
    MEM8(OWNER+0xf3)=(uint8_t)on;
    MEMF(MATRIX)=MEMF(MATRIX+0x10)=MEMF(MATRIX+0x20)=1;
    MEMF(MATRIX+0x24)=-0.0001057f;
    MEMF(MATRIX+0x28)=0.3702974f;
    MEMF(MATRIX+0x2c)=2.1201472f;
    MEM32(STACK)=0x12345678;
    MEM32(STACK+4)=MATRIX;MEM32(STACK+8)=0;MEM32(STACK+12)=mask;
    sub_000CF2DA();
    assert(g_esp==STACK+16);
    assert(g_fp_top==0);
    assert(g_ebx==0 && g_esi==0 && g_edi==0);
    if(MEM32(QUEUE+0x5810)) {
        assert(MEM32(QUEUE+0x5810)==1);
        assert(MEM32(QUEUE+0x5410)==RECORD && MEM32(RECORD+0x50)==1);
        for(i=0;i<0x4c;i+=4) assert(isfinite(MEMF(RECORD+i)));
        assert(MEMF(RECORD+0x40)>0 && MEMF(RECORD+0x44)>0 && MEMF(RECORD+0x48)>0);
    }
    return MEM32(QUEUE+0x5810);
}

static void shader_rgb_contribution(uint32_t old_color,uint32_t fixed_color)
{
    unsigned intensity,diffuse,channel;
    /* Actual LightDraw.psh: r0 = v0 * t3.a, then output r0.rgb.
     * Actual LightApplyTex.psh: scene += unlit.rgb * accumulation.rgb.
     * Exhaust every 8-bit intensity/diffuse pair for each RGB channel.
     * A zero projector contributes nothing for every such sample. */
    for(channel=0;channel<24;channel+=8) {
        for(intensity=0;intensity<256;intensity++) {
            double old_light=((old_color>>channel)&255u)/255.0*intensity/255.0;
            double fixed_light=((fixed_color>>channel)&255u)/255.0*intensity/255.0;
            assert(fixed_light==0);
            for(diffuse=0;diffuse<256;diffuse++) {
                double before=0.1+diffuse/255.0*old_light;
                double after=0.1+diffuse/255.0*fixed_light;
                assert(after==0.1 && before>=after);
                if(diffuse && intensity) assert(before>after);
            }
        }
    }
}

int main(void)
{
    unsigned char old_record[0x54],new_record[0x54];
    uint32_t old_color,fixed_color;
    assert(submit(0xffffffffu,0,0,1,0x100)==1);
    old_color=MEM32(RECORD+0x4c);
    assert(old_color==0x7f7f7f7fu);
    memcpy(old_record,memory+RECORD,sizeof old_record);
    /* Brightness/size zero alone still queues the white road projector. */
    assert(submit(0xffffffffu,0.8f,0.03f,1,0x100)==1);
    assert(!memcmp(old_record,memory+RECORD,sizeof old_record));
    assert(submit(0,0,0,1,0x100)==1);
    fixed_color=MEM32(RECORD+0x4c);
    assert(fixed_color==0);
    memcpy(new_record,memory+RECORD,sizeof new_record);
    /* Geometry and every remaining queue field stay identical. */
    memset(old_record+0x4c,0,4);
    assert(!memcmp(old_record,new_record,sizeof old_record));
    assert(submit(0xff000000u,0,0,1,0x100)==1);
    assert(MEM32(RECORD+0x4c)==0x7f000000u);
    assert(submit(0x80402010u,0.8f,0.03f,1,0x100)==1);
    assert(MEM32(RECORD+0x4c)==0x40201008u);
    /* Negative controls check the native activation gates, not a stub. */
    assert(submit(0xffffffffu,0,0,0,0x100)==0);
    assert(submit(0xffffffffu,0,0,1,0)==0);
    shader_rgb_contribution(old_color,fixed_color);
    puts("PASS: native road projector ignores zero brightness/size; zero ARGB queues zero color with identical geometry and zero RGB contribution");
    return 0;
}
'''


def main():
    if not shutil.which("cl"):
        raise SystemExit("Run scripts/enter-env.ps1 first so cl.exe is available.")
    native = generated()
    with zipfile.ZipFile(ROOT / "game_files/Data/Data_hd.zip") as archive:
        draw_shader = archive.read("Light/LightDraw.psh").decode("ascii")
        apply_shader = archive.read("Light/LightApplyTex.psh").decode("ascii")
        assert re.search(r"^mul\s+r0,\s*v0,\s*t3\.a\s*$", draw_shader, re.M)
        assert re.search(r"^xfc\s+zero,\s*zero,\s*zero,\s*r0\.rgb,\s*zero,\s*zero,\s*t3\.a\s*$",
                         draw_shader, re.M)
        assert re.search(r"^mad\s+r0\.rgb,\s*t0\.rgb,\s*t1\.rgb,\s*r0\.rgb", apply_shader, re.M)
    with tempfile.TemporaryDirectory(prefix="mm3-headlight-projector-") as temporary:
        folder = Path(temporary)
        shutil.copyfile(ROOT / "src/mm3_recomp_types.h", folder / "recomp_types.h")
        declarations = "\n".join(f"void {name}(void);" for name in NATIVE)
        (folder / "native.inc").write_text(declarations + "\n" + native)
        (folder / "constants.inc").write_text(constants(native))
        source, executable = folder / "contract.c", folder / "contract.exe"
        source.write_text(SOURCE)
        subprocess.run(["cl", "/nologo", "/W4", "/WX", "/Od", "/std:c11",
                        "/D_CRT_SECURE_NO_WARNINGS", "/I" + str(folder),
                        "/I" + str(ROOT / "tools/xboxrecomp/include"),
                        "/Fo:" + str(folder / "contract.obj"), "/Fe:" + str(executable), str(source)],
                       check=True, cwd=folder)
        subprocess.run([str(executable)], check=True, cwd=folder)


if __name__ == "__main__":
    main()
