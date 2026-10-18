"""Execute retail world-light gates, bounds and point/spot queues offline.

Uses both cities' installed packed effect descriptors without modifying assets.
Only unused sound/particle/flare-render paths are assertion boundaries.
"""
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import zipfile

from mm3_headlight_projector_regression import constants

ROOT = Path(__file__).resolve().parents[1]
SEAMS = ("sub_000E10A5", "sub_000E1F49", "sub_000E2385", "sub_000E247C")
NATIVE = SEAMS + (
    "sub_000489A6", "sub_00021E18", "sub_0022FFD1", "sub_0023006E",
    "sub_00230342", "sub_002300C6", "sub_002598CD", "sub_000139C8",
    "sub_001A779E", "sub_00259A65", "sub_002597D8", "sub_00259851",
    "sub_0025978C", "sub_00259769", "sub_00259180", "sub_00259741",
    "sub_002592A5", "sub_00095286", "sub_0022FF8B",
)


def generated():
    found = {}
    for path in sorted((ROOT / "src/recomp/gen").glob("recomp_*.c")):
        source = path.read_text()
        for name in NATIVE:
            match = re.search(r"^void " + name + r"\(void\)\n\{.*?^\}", source, re.M | re.S)
            if match:
                assert name not in found
                found[name] = match.group()
    assert set(found) == set(NATIVE), set(NATIVE) - set(found)
    return found


def descriptors():
    result = []
    with zipfile.ZipFile(ROOT / "game_files/Data/Data_hd.zip") as archive:
        for city in ("Paris", "Washington"):
            data = archive.read(city + "/LocalObject.bin")
            assert data[:4] == b"OTF0"
            version, count, effect_size, record_size, end = struct.unpack_from("<4HI", data, 4)
            assert (version, effect_size, record_size) == (7, 0x56, 0x34)
            offsets = [struct.unpack_from("<II", data, 16 + i * 8)[1] for i in range(count)] + [end]
            before = len(result)
            for start, stop in zip(offsets, offsets[1:]):
                assert (stop - start - record_size) % effect_size == 0
                for at in range(start + record_size, stop, effect_size):
                    result.append(data[at:at + effect_size])
            print(f"{city}: {len(result) - before} authored source/effect descriptors")
    return "static const unsigned char retail[][0x56] = {\n" + ",\n".join(
        "{" + ",".join(str(byte) for byte in record) + "}" for record in result) + "\n};\n"


SOURCE = r'''
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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
enum { SOURCE=0x10000, MANAGER=0x11000, MATRIX=0x13000, POSITION=0x14000,
       DIRECTION=0x15000, FLAG=0x16000, QUEUE=0x20000, RECORD=0x30000,
       MINIMUM=0x40000, MAXIMUM=0x41000, RETAIL_MINIMUM=0x42000,
       RETAIL_MAXIMUM=0x43000, STACK=0x90000 };
#include "mm3_lamp_lighting.c"
#pragma warning(push)
#pragma warning(disable:4101 4102 4127 4189 4702)
#include "native.inc"
#pragma warning(pop)
#include "constants.inc"
#include "retail.inc"

static void reset(void)
{
    memset(memory,0,sizeof memory);g_xbox_mem_offset=(ptrdiff_t)memory;
    g_eax=g_edx=g_ebx=g_esi=g_edi=g_ebp=g_seh_ebp=g_fs_base=g_eflags=0;
    g_ecx=SOURCE;g_esp=STACK;g_fp_top=g_df=g_fp_cmp=0;
    g_fp_cc=0;g_fp_control_word=0x27f;
    memset(g_fp_stack,0,sizeof g_fp_stack);native_constants();
    MEM32(0x3c5d8c)=QUEUE;MEM32(QUEUE+0x5410)=RECORD;
    MEMF(MATRIX)=MEMF(MATRIX+0x10)=MEMF(MATRIX+0x20)=1;
    MEMF(POSITION)=12;MEMF(POSITION+4)=5;MEMF(POSITION+8)=-3;
    MEMF(DIRECTION+4)=-1;
    MEM8(SOURCE+8)=1;MEM8(SOURCE+0x11)=255;
    MEM32(SOURCE+0x2b)=0x00806040;
    MEMF(SOURCE+0x49)=.5f;MEM8(SOURCE+0x4d)=127;
    MEMF(MANAGER+0x15dc)=1;
    MEM32(STACK)=0x12345678;MEM32(STACK+4)=MANAGER;
    MEM32(STACK+8)=FLAG;MEM32(STACK+12)=MATRIX;
    MEM32(STACK+16)=POSITION;MEM32(STACK+20)=DIRECTION;
    MEM32(STACK+32)=2;
}

static unsigned produce(void)
{
    unsigned char descriptor[0x56],matrix[0x30],point[12],direction[12];
    memcpy(descriptor,memory+SOURCE,sizeof descriptor);memcpy(matrix,memory+MATRIX,sizeof matrix);
    memcpy(point,memory+POSITION,12);memcpy(direction,memory+DIRECTION,12);
    sub_000E1F49();
    assert(g_esp==STACK+40 && g_fp_top==0);
    assert(g_ebx==0 && g_esi==0 && g_edi==0);
    assert(!memcmp(descriptor,memory+SOURCE,sizeof descriptor));
    assert(!memcmp(matrix,memory+MATRIX,sizeof matrix));
    assert(!memcmp(point,memory+POSITION,12) && !memcmp(direction,memory+DIRECTION,12));
    return MEM32(QUEUE+0x5810);
}

/* Run a bounds builder (the seamed one or the untouched retail copy). */
static void run_bounds(void (*builder)(void), uint32_t minimum, uint32_t maximum)
{
    g_ecx=POSITION;g_edx=DIRECTION;g_esi=SOURCE;g_edi=maximum;g_esp=STACK;
    MEM32(STACK)=0x12345678;MEM32(STACK+4)=minimum;
    MEM32(STACK+8)=0;MEM32(STACK+12)=0;MEM32(STACK+16)=0;
    builder();
    assert(g_esp==STACK+20 && g_fp_top==0);
}

/* Point-style sources: a world-space box around the source position. */
static void bounds(void)
{
    run_bounds(sub_000E10A5,MINIMUM,MAXIMUM);
    printf("bounds min %.3f %.3f %.3f max %.3f %.3f %.3f\n",MEMF(MINIMUM),MEMF(MINIMUM+4),MEMF(MINIMUM+8),MEMF(MAXIMUM),MEMF(MAXIMUM+4),MEMF(MAXIMUM+8));fflush(stdout);
    assert(MEMF(MINIMUM)<MEMF(POSITION) && MEMF(MAXIMUM)>MEMF(POSITION));
}

/* Spot sources: the cone box is built by the game's own cone routine. Its frame
 * is whatever that routine uses, so compare against the untouched retail
 * builder on identical input: the tuned reach and widened angle must give a box
 * that contains the retail box on every axis and still reaches the road. */
static void spot_bounds_grow(void)
{
    unsigned axis;
    float grown=0;
    reset();MEMF(SOURCE+0x4f)=7;MEM8(SOURCE+0x53)=169;
    run_bounds(sub_000E10A5,MINIMUM,MAXIMUM);
    run_bounds(sub_000E10A5_retail,RETAIL_MINIMUM,RETAIL_MAXIMUM);
    for(axis=0;axis<3;axis++) {
        float tuned_low=MEMF(MINIMUM+4*axis),tuned_high=MEMF(MAXIMUM+4*axis);
        float retail_low=MEMF(RETAIL_MINIMUM+4*axis),retail_high=MEMF(RETAIL_MAXIMUM+4*axis);
        assert(isfinite(tuned_low) && isfinite(tuned_high) && tuned_low<tuned_high);
        assert(tuned_low<=retail_low+.001f && tuned_high>=retail_high-.001f);
        grown+=(tuned_high-tuned_low)-(retail_high-retail_low);
    }
    /* Retail reaches 7 m at 59.65 degrees; the tuned cone is 10.15 m at 64.65. */
    assert(grown>10.0f);
}

static void direct_spot(uint32_t caller)
{
    unsigned char arguments[20];
    reset();g_ebx=SOURCE;MEM32(STACK)=caller;MEM32(STACK+4)=MATRIX;
    MEM32(STACK+8)=0x003F2000;MEMF(STACK+12)=6;MEMF(STACK+16)=30;
    memcpy(arguments,memory+STACK,20);sub_000489A6();
    assert(g_esp==STACK+20 && g_fp_top==0 && g_ebx==SOURCE);
    assert(!memcmp(arguments,memory+STACK,20));
    assert(MEM32(QUEUE+0x5810)==1);
    if(caller==0x0016945a) {
        assert(MEMF(RECORD+0x48)==6.0f && fabsf(MEMF(RECORD+0x40)-6.0f*tanf(30.0f*3.14159265f/180.0f))<.01f);
        assert(MEM32(RECORD+0x4c)==0x00321A00);
    } else {
        assert(MEMF(RECORD+0x48)==6 && MEM32(RECORD+0x4c)==0x003F2000);
    }
}

static unsigned channel(uint32_t color,unsigned shift)
{
    return (color>>shift)&255u;
}

static void source_colors(void)
{
    unsigned level,shift,i;
    uint32_t previous=0;
    const uint32_t colored[]={0xffff0000,0xff00ff00,0xff0000ff,
        0xff806040,0xff40ffff,0xffff40ff};
    reset();MEMF(SOURCE+0x4f)=7;MEM32(SOURCE+0x2b)=0x00ffffffu;
    /* Both dim and bright neutral fixtures get warmth without channel clipping;
     * each step of a native fade remains at most one output channel step. */
    for(level=0;level<256;level++) {
        uint32_t input=0xa0000000u|level|(level<<8)|(level<<16);
        uint32_t output=lamp_source_color(SOURCE,input);
        assert((output&0xff000000u)==0xa0000000u);
        assert(channel(output,16)<=level);
        assert(channel(output,16)>=channel(output,8));
        assert(channel(output,8)>=channel(output,0));
        for(shift=0;shift<=16;shift+=8) {
            assert(channel(output,shift)>=channel(previous,shift));
            assert(channel(output,shift)<=channel(previous,shift)+1);
        }
        previous=output;
    }
    assert(previous==0xa0d9b282u);
    /* Different nearly-neutral authored shades cross the warmth blend smoothly. */
    MEM32(SOURCE+0x2b)=0xffffff00u;
    previous=lamp_source_color(SOURCE,0xffffff00u);
    for(level=1;level<256;level++) {
        MEM32(SOURCE+0x2b)=0xffffff00u|level;
        uint32_t output=lamp_source_color(SOURCE,0xffffff00u|level);
        for(shift=0;shift<=16;shift+=8)
            assert(abs((int)channel(output,shift)-(int)channel(previous,shift))<=2);
        previous=output;
    }
    /* Saturated authored hues receive only uniform dimming, including alpha. */
    for(i=0;i<sizeof colored/sizeof colored[0];i++) {
        MEM32(SOURCE+0x2b)=colored[i];
        assert(lamp_source_color(SOURCE,colored[i])==lamp_color_gain(colored[i],.85f));
    }
    assert(lamp_source_color(SOURCE,0x7f000000u)==0x7f000000u);
    /* Queue both native source types to verify the warm conversion is wired in. */
    reset();MEM32(SOURCE+0x2b)=0x00ffffffu;assert(produce()==1);
    assert(channel(MEM32(RECORD+0x4c),16)>channel(MEM32(RECORD+0x4c),8));
    assert(channel(MEM32(RECORD+0x4c),8)>channel(MEM32(RECORD+0x4c),0));
    assert(channel(MEM32(RECORD+0x4c),16)<100);
    reset();MEMF(SOURCE+0x4f)=7;MEM8(SOURCE+0x53)=169;
    MEM32(SOURCE+0x2b)=0x00ffffffu;assert(produce()==1);
    assert(MEM32(RECORD+0x4c)==0x00d9b282u);
}

static void source_fade(void)
{
    uint32_t authored=MEM32(SOURCE+0x2b),previous=authored&0xff000000u;
    unsigned level,shift;
    /* Retail quantizes faded RGB to bytes before our tint conversion. Check
     * every authored fixture through a complete fade, especially 0xD2E6F5 and
     * 0x5A5A46: recomputing warmth from faded RGB made channels reverse here. */
    for(level=0;level<256;level++) {
        uint32_t input=authored&0xff000000u,output;
        for(shift=0;shift<=16;shift+=8)
            input|=(channel(authored,shift)*level/255u)<<shift;
        output=lamp_source_color(SOURCE,input);
        assert((output&0xff000000u)==(authored&0xff000000u));
        for(shift=0;shift<=16;shift+=8) {
            assert(channel(output,shift)>=channel(previous,shift));
            assert(channel(output,shift)<=channel(previous,shift)+1);
        }
        assert(MEM32(SOURCE+0x2b)==authored);
        previous=output;
    }
}

static void queued_source_fades(void)
{
    const uint32_t colors[]={0x00d2e6f5u,0x005a5a46u,0x00f8fed5u};
    unsigned i,spot,level,shift;
    for(i=0;i<sizeof colors/sizeof colors[0];i++)for(spot=0;spot<2;spot++) {
        uint32_t previous=0;
        for(level=0;level<256;level++) {
            uint32_t output;
            reset();MEM32(SOURCE+0x2b)=colors[i];MEM8(SOURCE+0x11)=(uint8_t)level;
            if(spot) {MEMF(SOURCE+0x4f)=7;MEM8(SOURCE+0x53)=169;}
            assert(produce()==1);
            output=MEM32(RECORD+0x4c);
            for(shift=0;shift<=16;shift+=8) {
                assert(channel(output,shift)>=channel(previous,shift));
                /* Retail's final fully-lit step skips its divide-by-256 fade. */
                assert(channel(output,shift)<=channel(previous,shift)+2);
            }
            previous=output;
        }
    }
}

int main(void)
{
    unsigned i,lights=0,fallbacks=0,dark=0,warm=0,colored=0;
    float tiny,large;
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    reset();assert(produce()==1);assert(MEM32(RECORD+0x50)==0);
    assert(MEMF(RECORD+0x40)>11 && MEMF(RECORD+0x40)<11.6f);
    assert(MEM32(RECORD+0x4c)==0x00291F15);
    reset();MEMF(SOURCE+0x4f)=7;MEM8(SOURCE+0x53)=169;
    assert(produce()==1);assert(MEM32(RECORD+0x50)==1);
    assert(fabsf(MEMF(RECORD+0x48)-10.15f)<.001f);
    assert(MEMF(RECORD+0x40)>20 && MEMF(RECORD+0x40)<23);
    assert(MEM32(RECORD+0x4c)==0x006D5236);
    reset();bounds();assert(MEMF(MINIMUM)<8 && MEMF(MAXIMUM)>16);
    spot_bounds_grow();
    /* Native broken/off, time mask, pause and flicker gates execute intact. */
    reset();MEM32(STACK+28)=1;assert(!produce());
    reset();MEM8(SOURCE)=1;MEM8(MANAGER+0x15f4)=1;assert(!produce());
    /* The manager gate is fcomp/test ah,5/jnp against 0.0: only a negative value
     * skips the light. Zero leaves both condition bits clear, so retail still
     * produces it, and the seam must not change either decision. */
    reset();MEMF(MANAGER+0x15dc)=-1;assert(!produce());
    reset();MEMF(MANAGER+0x15dc)=0;assert(produce()==1);
    reset();MEMF(SOURCE+0x35)=1;MEMF(SOURCE+0x3d)=0;assert(!produce());
    reset();MEMF(SOURCE+0x49)=0;assert(!produce());
    reset();MEM8(SOURCE+0x4d)=0;assert(!produce());
    reset();MEM32(SOURCE+0x2b)=0;assert(!produce());
    reset();MEM32(0x3c5d8c)=0;assert(!produce());
    reset();MEM32(QUEUE+0x5810)=256;assert(produce()==256);
    reset();MEMF(SOURCE+0x49)=.1f;MEM8(SOURCE+0x4d)=25;tiny=mm3_lamp_source_range(SOURCE);
    MEMF(SOURCE+0x49)=.5f;MEM8(SOURCE+0x4d)=255;large=mm3_lamp_source_range(SOURCE);
    assert(tiny>0 && tiny<1 && large>7.9f && large<8.1f);
    MEMF(SOURCE+0x4f)=50;assert(mm3_lamp_source_range(SOURCE)==56);
    MEMF(SOURCE+0x4f)=NAN;assert(mm3_lamp_source_range(SOURCE)==0);
    direct_spot(0x0016945a);direct_spot(0x000CF5ED);direct_spot(0x12345678);
    source_colors();
    queued_source_fades();
    /* All authored sources in both cities keep independent reach and tint.
     * Zero-radius bulbs become local sources; non-light effects stay dark. */
    for(i=0;i<sizeof retail/sizeof retail[0];i++) {
        float before,after,size;
        uint32_t original_color,tuned_color;
        unsigned shift,peak=0,minimum=255;
        reset();memcpy(memory+SOURCE,retail[i],0x56);
        before=MEMF(SOURCE+0x4f);size=MEMF(SOURCE+0x49);
        after=mm3_lamp_source_range(SOURCE);
        original_color=MEM32(SOURCE+0x2b);
        tuned_color=lamp_source_color(SOURCE,original_color);
        assert(!memcmp(memory+SOURCE,retail[i],0x56));
        assert((original_color&0xff000000u)==(tuned_color&0xff000000u));
        for(shift=0;shift<=16;shift+=8) {
            unsigned value=channel(original_color,shift);
            if(value>peak)peak=value;
            if(value<minimum)minimum=value;
            assert(channel(tuned_color,shift)<=value);
        }
        if(after>0 && peak) {
            source_fade();
            if(minimum*4>peak*3)warm++;
            else {
                float gain=before==0?.65f*(float)MEM8(SOURCE+0x4d)/255.0f:.85f;
                assert(tuned_color==lamp_color_gain(original_color,gain));
                colored++;
            }
        }
        if(before>0) { assert(after>before && after<=before+6.001f);lights++; }
        else if(size>0 && MEM8(SOURCE+0x4d) && (MEM32(SOURCE+0x2b)&0xffffff)) {
            assert(after>0 && after<=10.001f);fallbacks++;lights++;
        } else {assert(after==0);dark++;}
    }
    assert(lights==157 && dark==455 && fallbacks>25 && warm>0 && colored>0);
    printf("PASS: %u city light descriptors (%u added local bulbs), %u dark effects; fixed fixture warmth, monotone 256-step authored/queued fades, authored saturated hues, native source gates, source-sized reach, matched bounds, point/spot queues, traffic-signal tint and guest ABI\n",lights,fallbacks,dark);
    return 0;
}
'''


def main():
    cmake = ROOT / "tools/venv/Lib/site-packages/cmake/data/bin/cmake.exe"
    if not cmake.is_file():
        cmake = shutil.which("cmake")
    if not shutil.which("cl") or not cmake:
        raise SystemExit("Run scripts/enter-env.ps1 first so cl.exe and CMake are available.")
    native = generated()
    # Untouched retail bounds builder, kept beside the seamed one for comparison.
    retail_bounds = native["sub_000E10A5"].replace(
        "void sub_000E10A5(void)", "void sub_000E10A5_retail(void)", 1)
    with tempfile.TemporaryDirectory(prefix="mm3-lamp-lighting-") as temporary:
        folder = Path(temporary)
        script = folder / "seam.cmake"
        script.write_text(f'include("{(ROOT / "scripts/mm3_lamp_lighting_seam.cmake").as_posix()}")\n'
                          'file(READ "${INPUT}" content)\n'
                          'mm3_add_lamp_lighting_seam(content "${OWNER}")\n'
                          'file(WRITE "${OUTPUT}" "${content}")\n')
        for owner in SEAMS:
            original, modified = folder / "original.c", folder / "modified.c"
            original.write_text(native[owner])
            subprocess.run([str(cmake), f"-DINPUT={original.as_posix()}", f"-DOUTPUT={modified.as_posix()}",
                            f"-DOWNER={owner}", "-P", str(script)], check=True)
            native[owner] = modified.read_text()
        for owner in ("sub_000489A6", "sub_00021E18"):
            native[owner] = native[owner].replace("void " + owner + "(void)", "void " + owner + "_original(void)", 1)
        body = "\n".join(list(native.values()) + [retail_bounds])
        calls = set(re.findall(r"RECOMP_ABI_CALL\(0x[0-9A-Fa-f]+u, (sub_[0-9A-Fa-f]+)\)", body))
        declarations = "\n".join("void " + name + "(void);" for name in set(NATIVE) | calls)
        unused = "\n".join(f'void {name}(void) {{ assert(!"unused {name}"); }}' for name in calls - set(NATIVE))
        (folder / "native.inc").write_text(declarations + "\n" + unused + "\n" + body)
        extra_constants = " ".join(f"MEMF(0x{at:X})" for at in range(0x378A38, 0x378A5C, 4))
        (folder / "constants.inc").write_text(constants(body + extra_constants))
        (folder / "retail.inc").write_text(descriptors())
        shutil.copyfile(ROOT / "src/mm3_recomp_types.h", folder / "recomp_types.h")
        source, executable = folder / "contract.c", folder / "contract.exe"
        source.write_text(SOURCE)
        subprocess.run(["cl", "/nologo", "/W4", "/WX", "/Od", "/std:c11", "/D_CRT_SECURE_NO_WARNINGS",
                        "/I" + str(folder), "/I" + str(ROOT / "src"), "/I" + str(ROOT / "tools/xboxrecomp/include"),
                        "/Fo:" + str(folder / "contract.obj"), "/Fe:" + str(executable), str(source)],
                       check=True, cwd=folder)
        subprocess.run([str(executable)], check=True, cwd=folder)


if __name__ == "__main__":
    main()
