"""Run the lamp-glow lift around the real native glow-list append (1A831B).

Run after scripts/enter-env.ps1. The generated append executes unchanged; only
its directional helper 1A7C9A is a recorded boundary. Checks the lifted point
stays on the eye ray, distance scaling and caps, eye validation against the
view planes, failed/full appends, the MM3_GLOW_LIFT=0 switch and the ABI.
"""
from pathlib import Path
import importlib.util
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "native_projector", ROOT / "tests/mm3_headlight_projector_regression.py")
native_projector = importlib.util.module_from_spec(spec)
spec.loader.exec_module(native_projector)


def generated(name):
    for path in sorted((ROOT / "src/recomp/gen").glob("recomp_*.c")):
        match = re.search(r"^void " + name + r"\(void\)\n\{.*?^\}", path.read_text(), re.M | re.S)
        if match:
            return match.group()
    raise AssertionError(name)


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
enum { VIEW=0x10000, MANAGER=0x14000, LIST=0x15000, TYPE=0x18000, RECORD=0x19000,
       OUT=0x19100, STACK=0x90000, CAPACITY=4 };
static unsigned directional;
static float directional_seen[3];
/* Native directional fade: record its unlifted input, then pop 3 arguments. */
void sub_001A7C9A(void)
{
    uint32_t position=MEM32(g_esp+4);
    ++directional;
    memcpy(directional_seen,(const void *)XBOX_PTR(position),sizeof directional_seen);
    g_esp+=16;
}
#pragma warning(push)
#pragma warning(disable:4101 4102 4127 4189)
void sub_001A831B_original(void);
#include "native.inc"
#pragma warning(pop)
#include "constants.inc"
#include "mm3_lamp_glow.c"

static const float EYE[3]={100,20,-50};

/* A live view as native F3F93 leaves it: world-to-camera rotation rows and
 * translation row at +0x1F24 (row vectors), the camera position derived from
 * them at +0x1EA8, and a 90-degree frustum whose four side planes pass
 * through that position (outward normals) plus a near plane 1m ahead. A view
 * turned 30 degrees about Y checks a non-trivial rotation. */
static void view_turned(const float eye[3], float yaw)
{
    const float c=cosf(yaw),s=sinf(yaw);
    const float rows[3][3]={{c,0,-s},{0,1,0},{s,0,c}};
    static const float n[5][3]={{-.70710678f,0,-.70710678f},{.70710678f,0,-.70710678f},
                                {0,-.70710678f,-.70710678f},{0,.70710678f,-.70710678f},{0,0,-1}};
    float translation[3]={0,0,0};
    unsigned i,j;
    MEM32(0x3c5588)=VIEW;MEM32(VIEW+0x1ea4)=5;
    /* Row-vector view matrix: camera c maps to the origin, c.R + T = 0. */
    for(i=0;i!=3;++i) for(j=0;j!=3;++j) {
        MEMF(VIEW+0x1f24+i*16+j*4)=rows[i][j];
        translation[j]-=eye[i]*rows[i][j];
    }
    for(i=0;i!=3;++i) {
        float position=0;
        MEMF(VIEW+0x1f54+i*4)=translation[i];
        for(j=0;j!=3;++j) position-=rows[i][j]*translation[j];
        MEMF(VIEW+0x1ea8+i*4)=position;
    }
    for(i=0;i!=5;++i) {
        float d=-(n[i][0]*eye[0]+n[i][1]*eye[1]+n[i][2]*eye[2]);
        if(i==4) d-=1;
        MEMF(VIEW+0x1e54+i*16)=n[i][0];MEMF(VIEW+0x1e58+i*16)=n[i][1];
        MEMF(VIEW+0x1e5c+i*16)=n[i][2];MEMF(VIEW+0x1e60+i*16)=d;
    }
}
static void view(const float eye[3]) {view_turned(eye,0);}

static void reset(void)
{
    memset(memory,0,sizeof memory);g_xbox_mem_offset=(ptrdiff_t)memory;
    g_eax=g_ecx=g_edx=g_ebx=g_esi=g_edi=0;g_ebp=0x5150;g_seh_ebp=0x6160;
    g_esp=STACK;g_fp_top=g_df=g_fp_cmp=0;g_fp_cc=0;g_fp_control_word=0x27f;
    memset(g_fp_stack,0,sizeof g_fp_stack);native_constants();directional=0;
    view(EYE);
    MEM32(MANAGER+0x214)=CAPACITY;MEM32(MANAGER+0x23c)=LIST;
    MEM32(TYPE+0x30)=1;
    MEM32(RECORD)=0x1234;MEMF(RECORD+0x1c)=.8f;MEM32(RECORD+0x2c)=0xffff0000u;
    MEMF(RECORD+0x30)=0;MEMF(RECORD+0x34)=1;
    MEMF(RECORD+0x10)=0;MEMF(RECORD+0x14)=0;MEMF(RECORD+0x18)=1;
}

/* Call 1A831B as 1A842A does: (manager, type, record, output), ret 16. */
static unsigned append(const float position[3], uint32_t out)
{
    unsigned i;
    for(i=0;i!=3;++i) MEMF(RECORD+4+i*4)=position[i];
    g_esp=STACK;
    MEM32(STACK)=0x001A8445u;MEM32(STACK+4)=MANAGER;MEM32(STACK+8)=TYPE;
    MEM32(STACK+12)=RECORD;MEM32(STACK+16)=out;
    g_ebx=0x0b0b;g_esi=0x0e5e;g_edi=0x0ed1;
    sub_001A831B();
    assert(g_esp==STACK+20 && g_fp_top==0);
    /* Callee-saved registers survive; generated code publishes its frame. */
    assert(g_ebx==0x0b0b && g_esi==0x0e5e && g_edi==0x0ed1);
    return g_eax&255u;
}

static float component(unsigned entry, unsigned axis) {return MEMF(LIST+entry*64+4+axis*4);}

static void expect_lift(unsigned entry, const float from[3], float lift)
{
    float toward[3],distance=0,moved=0,along=0;
    unsigned i;
    for(i=0;i!=3;++i) {toward[i]=EYE[i]-from[i];distance+=toward[i]*toward[i];}
    distance=sqrtf(distance);
    for(i=0;i!=3;++i) {
        float delta=component(entry,i)-from[i];
        moved+=delta*delta;along+=delta*toward[i]/distance;
    }
    /* The point moves exactly toward the eye: unchanged screen position. */
    assert(fabsf(sqrtf(moved)-lift)<2e-4f*(1+distance) && fabsf(along-lift)<2e-4f*(1+distance));
}

int main(void)
{
    const float rear[3]={100,19,-44},street[3]={100,26,-20},far[3]={100,20,950};
    const float close[3]={100,20,-49.5f};
    float before[3];
    unsigned i;
    /* Chase-camera rear lamp 6.08m away: 12cm + 0.3% of the distance. */
    reset();
    assert(append(rear,OUT)==1 && MEM32(MANAGER+0x218)==1);
    expect_lift(0,rear,.12f+.003f*sqrtf(1+36));
    /* Native output and copied record fields are those of the native copy. */
    assert(MEMF(OUT)==0 && MEMF(OUT+4)==0 && MEMF(OUT+8)==1);
    assert(MEM32(LIST)==0x1234 && MEMF(LIST+0x1c)==.8f && MEM32(LIST+0x2c)==0xffff0000u);
    assert(MEM32(LIST+0x3c)==TYPE && MEM8(LIST+0x38)==0);
    assert(MEMF(RECORD+4)==rear[0] && MEMF(RECORD+8)==rear[1] && MEMF(RECORD+12)==rear[2]);
    /* A street lamp 31m away and a lamp 1km away (capped at 3m). */
    assert(append(street,0)==1);
    expect_lift(1,street,.12f+.003f*sqrtf(36+900));
    assert(append(far,0)==1);
    expect_lift(2,far,3.0f);
    /* Within 1m of the eye nothing moves. */
    assert(append(close,0)==1);
    for(i=0;i!=3;++i) assert(component(3,i)==close[i]);
    /* A full list appends nothing and writes nothing past its capacity. */
    for(i=0;i!=3;++i) MEMF(LIST+4*64+4+i*4)=7;
    assert(append(rear,0)==0 && MEM32(MANAGER+0x218)==CAPACITY);
    for(i=0;i!=3;++i) assert(MEMF(LIST+4*64+4+i*4)==7);
    /* Native directional fade still sees the authored, unlifted position. */
    reset();MEMF(RECORD+0x30)=2;
    assert(append(rear,0)==1 && directional==1);
    assert(directional_seen[0]==rear[0] && directional_seen[1]==rear[1] && directional_seen[2]==rear[2]);
    expect_lift(0,rear,.12f+.003f*sqrtf(1+36));
    /* A disabled type or null record fails natively and is left alone. */
    reset();MEM32(TYPE+0x30)=0;
    assert(append(rear,0)==0 && !MEM32(MANAGER+0x218));
    /* A turned camera is still a live camera. */
    reset();view_turned(EYE,.5235988f);
    assert(append(rear,0)==1);
    expect_lift(0,rear,.12f+.003f*sqrtf(1+36));
    /* The translation row is not the camera: a row-vector view matrix whose
     * translation lies far from the camera still lifts toward the camera. */
    reset();view_turned(EYE,.5235988f);
    assert(fabsf(MEMF(VIEW+0x1f54)-EYE[0])>10);
    assert(append(rear,0)==1);
    expect_lift(0,rear,.12f+.003f*sqrtf(1+36));
    /* A position off the side planes, axes that are not a rotation, or a view
     * without its five planes are not trusted. */
    reset();
    MEMF(VIEW+0x1ea8)+=5;
    assert(append(rear,0)==1);
    for(i=0;i!=3;++i) assert(component(0,i)==rear[i]);
    reset();MEMF(VIEW+0x1f24)=2;
    assert(append(rear,0)==1);
    for(i=0;i!=3;++i) assert(component(0,i)==rear[i]);
    reset();MEM32(VIEW+0x1ea4)=6;
    assert(append(rear,0)==1);
    for(i=0;i!=3;++i) assert(component(0,i)==rear[i]);
    reset();MEM32(0x3c5588)=0;
    assert(append(rear,0)==1);
    for(i=0;i!=3;++i) assert(component(0,i)==rear[i]);
    /* Non-finite authored positions stay as the game wrote them. */
    reset();before[0]=NAN;before[1]=19;before[2]=-44;
    assert(append(before,0)==1 && isnan(component(0,0)));
    puts("PASS: lamp glows lift along the eye ray (12cm + 0.3%, 3m cap, none within 1m), eye from the live camera matrix; native append, output, directional fade, capacity and failures unchanged; untrusted eye/view skipped; ABI");
    return 0;
}
'''

DISABLED = r'''
int main(void)
{
    const float rear[3]={100,19,-44};
    unsigned i;
    reset();
    assert(append(rear,0)==1);
    for(i=0;i!=3;++i) assert(component(0,i)==rear[i]);
    puts("PASS: MM3_GLOW_LIFT=0 keeps the native glow positions");
    return 0;
}
'''


def build(folder: Path, name: str, source: str) -> Path:
    (folder / (name + ".c")).write_text(source)
    exe = folder / (name + ".exe")
    subprocess.run(["cl", "/nologo", "/W4", "/WX", "/Od", "/std:c11", "/fp:precise",
                    "/I" + str(folder), "/I" + str(ROOT / "src"),
                    "/I" + str(ROOT / "tools/xboxrecomp/include"),
                    "/Fo:" + str(folder / (name + ".obj")), "/Fe:" + str(exe),
                    str(folder / (name + ".c"))], check=True, cwd=folder,
                   stdout=subprocess.DEVNULL)
    return exe


def main():
    if not shutil.which("cl"):
        raise SystemExit("Run scripts/enter-env.ps1 first so cl.exe is available.")
    native = generated("sub_001A831B").replace("void sub_001A831B(void)",
                                                "void sub_001A831B_original(void)", 1)
    calls = set(re.findall(r"RECOMP_ABI_CALL\(0x[0-9A-Fa-f]+u, (sub_[0-9A-Fa-f]+)\)", native))
    assert calls == {"sub_001A7C9A"}, calls
    with tempfile.TemporaryDirectory(prefix="mm3-lamp-glow-") as temporary:
        folder = Path(temporary)
        shutil.copyfile(ROOT / "src/mm3_recomp_types.h", folder / "recomp_types.h")
        (folder / "native.inc").write_text(native)
        (folder / "constants.inc").write_text(native_projector.constants(native))
        exe = build(folder, "glow", SOURCE)
        subprocess.run([str(exe)], check=True, cwd=folder)
        disabled = build(folder, "disabled", SOURCE.split("int main(void)", 1)[0] + DISABLED)
        environment = dict(__import__("os").environ, MM3_GLOW_LIFT="0")
        subprocess.run([str(disabled)], check=True, cwd=folder, env=environment)


if __name__ == "__main__":
    main()
