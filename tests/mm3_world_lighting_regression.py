"""Exercise native ambient/quad code and scoped whole-scene lighting wrappers.

Run after scripts/enter-env.ps1; no running game or save is opened or modified.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def generated(name):
    for path in (ROOT / "src/recomp/gen").glob("*.c"):
        match = re.search(r"^void " + name + r"\(void\)\n\{.*?^\}",
                          path.read_text(), re.M | re.S)
        if match:
            return match.group()
    raise ValueError(name)


SOURCE = r'''
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#include "mm3_world_lighting.c"
ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp,g_seh_ebp,g_fs_base,g_eflags;
RECOMP_TLS int g_fp_top,g_df,g_fp_cmp;
RECOMP_TLS uint16_t g_fp_control_word,g_fp_cc;
RECOMP_TLS double g_fp_stack[8];
RECOMP_TLS RecompMmx g_mm0,g_mm1,g_mm2,g_mm3,g_mm4,g_mm5,g_mm6,g_mm7;
RECOMP_TLS RecompXmm g_xmm0,g_xmm1,g_xmm2,g_xmm3,g_xmm4,g_xmm5,g_xmm6,g_xmm7;
static unsigned char memory[0x500000],expected_memory[sizeof memory];
static uint32_t original_color;
static float requested_gain;
static int requested_softness;
static int requested_overlap;
void nv2a_d3d_set_local_light_gain(float gain) { requested_gain=gain; }
void nv2a_d3d_set_local_light_softness(int enabled) { requested_softness=enabled; }
void nv2a_d3d_set_local_light_overlap(int enabled) { requested_overlap=enabled; }
static unsigned original_calls;
static uint32_t observed_light_mode;
static uint32_t time_of_day;
static unsigned night_percent;
static void sub_000FEE80(void)
{
    assert(g_eax==0x00112233 && g_edx==0x00445566);
    assert(fabs(MEMF(g_esp+4)-0.4f)<1e-6);
    assert(fabs(MEMF(g_esp+8)-0.1f)<1e-6);
    assert(fabs(MEMF(g_esp+12)-0.15f)<1e-6);
    original_calls++;
    g_eax=original_color;g_ecx=0xAABBCCDD;g_edx=0x12345678;
    SetLastError(0x778899);errno=EDOM;
    g_esp+=16;
}
void sub_000FF324_original(void)
{
    original_calls++;g_eax=original_color;g_ecx=0xAABBCCDD;g_edx=0x12345678;
    SetLastError(0x778899);errno=EDOM;g_esp+=16;
}
void sub_000FF15C_original(void)
{
    original_calls++;g_eax=original_color;g_ecx=0xAABBCCDD;g_edx=0x12345678;
    SetLastError(0x778899);errno=EDOM;g_esp+=12;
}
void sub_00230DEE_original(void)
{
    if(MEM32(g_esp+4)) observed_light_mode=MEM32(g_ecx+4);
    g_eax=0x1234;g_ecx=0x5678;g_edx=0x9ABC;g_esp+=16;
    SetLastError(0x778899);errno=EDOM;
}
#pragma warning(push)
#pragma warning(disable:4101 4102 4127 4189)
#include "native_getter.inc"
#include "native_quad.inc"
#pragma warning(pop)
typedef struct {
    uint32_t regs[11];int fp_top,fp_cmp,df;
    uint16_t cc,control;double fp[8];RecompMmx mm[8];RecompXmm xmm[8];
    DWORD error;int crt_errno;
} Snapshot;
static void snapshot(Snapshot *s)
{
    memset(s,0,sizeof *s);
    s->regs[0]=g_eax;s->regs[1]=g_ecx;s->regs[2]=g_edx;s->regs[3]=g_esp;
    s->regs[4]=g_ebx;s->regs[5]=g_esi;s->regs[6]=g_edi;s->regs[7]=g_ebp;
    s->regs[8]=g_seh_ebp;s->regs[9]=g_fs_base;s->regs[10]=g_eflags;
    s->fp_top=g_fp_top;s->fp_cmp=g_fp_cmp;s->df=g_df;s->cc=g_fp_cc;s->control=g_fp_control_word;
    memcpy(s->fp,g_fp_stack,sizeof s->fp);
    s->mm[0]=g_mm0;s->mm[1]=g_mm1;s->mm[2]=g_mm2;s->mm[3]=g_mm3;
    s->mm[4]=g_mm4;s->mm[5]=g_mm5;s->mm[6]=g_mm6;s->mm[7]=g_mm7;
    s->xmm[0]=g_xmm0;s->xmm[1]=g_xmm1;s->xmm[2]=g_xmm2;s->xmm[3]=g_xmm3;
    s->xmm[4]=g_xmm4;s->xmm[5]=g_xmm5;s->xmm[6]=g_xmm6;s->xmm[7]=g_xmm7;
    s->error=GetLastError();s->crt_errno=errno;
}
static void setup(uint32_t caller,uint32_t color)
{
    unsigned i;
    memset(memory,0xAA,sizeof memory);g_xbox_mem_offset=(ptrdiff_t)memory;
    g_eax=0x20000;g_ecx=0x10203;g_edx=0x30405;g_esp=0x18000;
    g_ebx=0x1111;g_esi=0x2222;g_edi=0x3333;g_ebp=0x4444;g_seh_ebp=0x5555;
    g_fs_base=0x6666;g_eflags=0x202;g_df=1;g_fp_top=3;g_fp_cmp=2;
    g_fp_cc=0x4100;g_fp_control_word=0x27F;
    for(i=0;i<8;i++) g_fp_stack[i]=i+0.125;
    MEM32(g_esp)=caller;MEMF(g_esp+4)=0.4f;MEMF(g_esp+8)=1.0f;
    MEMF(0x386190)=0.005f;
    MEM32(g_eax+0x554)=0x00112233;MEM32(g_eax+0x57C)=0x00445566;
    MEMF(g_eax+0x568)=20;MEMF(g_eax+0x590)=30;
    MEM32(0x407C30)=time_of_day;
    original_color=color;SetLastError(0x112233);errno=ERANGE;
}
static uint32_t expected_scale(uint32_t color,unsigned percent)
{
    unsigned shift;uint32_t result=color&0xFF000000u;
    for(shift=0;shift<24;shift+=8)
        result|=((((color>>shift)&255u)*percent+50u)/100u)<<shift;
    return result;
}
static uint32_t expected_world_base(uint32_t color)
{
    return expected_scale(color,time_of_day==3u?night_percent:100u);
}
static uint32_t expected_lift(uint32_t color,unsigned lift)
{
    uint32_t r=(color>>16)&255,g=(color>>8)&255,b=color&255;
    r=r+lift>255?255:r+lift;g=g+lift>255?255:g+lift;b=b+lift>255?255:b+lift;
    return (color&0xFF000000u)|(r<<16)|(g<<8)|b;
}
static uint32_t expected_floor(uint32_t color,unsigned even)
{
    unsigned shift;uint32_t result=color&0xFF000000u;
    for(shift=0;shift<24;shift+=8) {
        unsigned channel=(color>>shift)&255;
        if(even&&channel<48) channel=48;
        result|=channel<<shift;
    }
    return result;
}
static void query(uint32_t caller,uint32_t color,unsigned lift,unsigned even)
{
    Snapshot expected,actual;unsigned before=original_calls;
    setup(caller,color);sub_000FF19A_original();snapshot(&expected);
    memcpy(expected_memory,memory,sizeof memory);
    setup(caller,color);sub_000FF19A();snapshot(&actual);
    expected.regs[0]=caller==0x00230F75u?expected_floor(expected_lift(expected_world_base(color),lift),even):color;
    assert(!memcmp(&expected,&actual,sizeof expected));
    if(!lift&&!even) assert(g_eax==(caller==0x00230F75u?expected_world_base(color):color));
    assert(!memcmp(expected_memory,memory,sizeof memory));
    assert(original_calls==before+2 && g_esp==0x1800C);
}
static void directional_query(uint32_t caller,uint32_t color,unsigned even,unsigned back)
{
    Snapshot expected,actual;unsigned before=original_calls;
    void(*native)(void)=back?sub_000FF15C_original:sub_000FF324_original;
    void(*wrapped)(void)=back?sub_000FF15C:sub_000FF324;
    setup(caller,color);native();snapshot(&expected);
    memcpy(expected_memory,memory,sizeof memory);
    setup(caller,color);wrapped();snapshot(&actual);
    expected.regs[0]=caller==(back?0x00230FA6u:0x00230F8Du)?expected_floor(expected_world_base(color),even):color;
    assert(!memcmp(&expected,&actual,sizeof expected));
    if(!even) assert(g_eax==(caller==(back?0x00230FA6u:0x00230F8Du)?expected_world_base(color):color));
    assert(!memcmp(expected_memory,memory,sizeof memory));
    assert(original_calls==before+2 && g_esp==0x18000u+(back?12u:16u));
}
static void composite(unsigned mode,unsigned even,unsigned view)
{
    const uint32_t renderer=0x10000,unrelated=0x30000;
    (void)even;
    setup(0x11223344,0);g_ecx=renderer;g_edi=unrelated;
    MEM32(renderer+4)=mode;MEM32(unrelated+4)=0xA1B2C3D4;
    MEM32(g_esp+4)=view;observed_light_mode=99;
    sub_00230DEE();
    assert(observed_light_mode==(view?mode:99u));
    assert(MEM32(renderer+4)==mode && MEM32(unrelated+4)==0xA1B2C3D4);
    assert(g_eax==0x1234 && g_ecx==0x5678 && g_edx==0x9ABC);
    assert(g_edi==unrelated && g_esp==0x18010);
    assert(GetLastError()==0x778899 && errno==EDOM);
}
static void quad(uint32_t color,unsigned lift,unsigned even)
{
    const uint32_t renderer=0x10000;
    unsigned i;
    static const unsigned ambient[4]={0x58B0,0x58D8,0x5900,0x5928};
    static const unsigned diffuse[4]={0x58AC,0x58D4,0x58FC,0x5924};
    uint32_t base=expected_world_base(color);
    uint32_t lit=expected_floor(expected_lift(base,lift),even),direct=expected_floor(base,even);
    if(!lift&&!even) assert(lit==base && direct==base);
    g_edi=renderer;g_ebx=lit;g_esi=direct;g_fp_top=0;
    memset(g_fp_stack,0,sizeof g_fp_stack);MEMF(0x385088)=0.5f;
    native_quad();
    for(i=0;i<4;i++) {
        assert(MEM32(renderer+ambient[i])==lit);
        assert(MEM32(renderer+diffuse[i])==direct);
    }
    /* All three LightApply inputs share the floor: unlike an ambient-only
     * lift, it survives both zero ambient weight and every normal intensity. */
    for(i=0;i<=1000;i++) {
        double intensity=(double)i/1000.0;
        double front=fmax(0,fmin(1,(intensity-.25)*4));
        double back=fmax(0,fmin(1,(.25-intensity)*4));
        double weight=1-front-back;
        double old=(base&255)/255.0,new_color=(lit&255)/255.0;
        double directional=(direct&255)/255.0;
        double before=.35*4*old;
        double after=.35*4*(directional*(1-weight)+new_color*weight);
        assert(after>=before-1e-12);
        if(!lift&&!even) assert(fabs(after-before)<1e-12);
        if(even) assert(after>=.35*4*48/255.0-1e-12);
        else assert(after-before<=.35*4*lift/255.0+1e-12);
    }
}
int main(int argc,char **argv)
{
    unsigned lift,even,i,time;uint32_t seed=1234567;
    static const uint32_t callers[]={0x00230F75u,0x00230F8Du,0x00230FA6u,0x0023101Cu,0x00123456u,0};
    static const uint32_t times[]={0,1,2,3,4,0xFFFFFFFFu};
    /* Explicit night and daylight samples catch any default floor or lift,
     * including a black scene. Exercise every LightApply role and unrelated
     * caller with exact day/night color/alpha, ABI and memory comparisons. */
    static const uint32_t native_colors[]={0xA7000000u,0xB104060Au,0xC2102030u,
        0xD3808080u,0xE4F0E0C0u,0xF5FFFFFFu};
    assert(argc==4);lift=(unsigned)atoi(argv[1]);even=(unsigned)atoi(argv[2]);
    night_percent=(unsigned)atoi(argv[3]);
    /* Local-light gain: gentle default, valid 1..8 (1 = retail). */
    _putenv("MM3_LOCAL_LIGHT_GAIN=");mm3_world_lighting_apply_gain();assert(requested_gain==1.3f);
    _putenv("MM3_LOCAL_LIGHT_GAIN=1.3");mm3_world_lighting_apply_gain();assert(requested_gain==1.3f);
    _putenv("MM3_LOCAL_LIGHT_GAIN=1");mm3_world_lighting_apply_gain();assert(requested_gain==1.0f);
    _putenv("MM3_LOCAL_LIGHT_GAIN=2.5");mm3_world_lighting_apply_gain();assert(requested_gain==2.5f);
    _putenv("MM3_LOCAL_LIGHT_GAIN=8");mm3_world_lighting_apply_gain();assert(requested_gain==8.0f);
    _putenv("MM3_LOCAL_LIGHT_GAIN=0.5");mm3_world_lighting_apply_gain();assert(requested_gain==1.3f);
    _putenv("MM3_LOCAL_LIGHT_GAIN=9");mm3_world_lighting_apply_gain();assert(requested_gain==1.3f);
    _putenv("MM3_LOCAL_LIGHT_GAIN=3x");mm3_world_lighting_apply_gain();assert(requested_gain==1.3f);
    _putenv("MM3_LOCAL_LIGHT_GAIN=nan");mm3_world_lighting_apply_gain();assert(requested_gain==1.3f);
    assert(requested_softness==1);
    assert(requested_overlap==1);
    _putenv("MM3_LOCAL_LIGHT_GAIN=");
    assert(mm3_world_light_scale(0xAB010203u,25)==0xAB000101u);
    assert(mm3_world_light_scale(0xA17E80FFu,25)==0xA1202040u);
    assert(mm3_world_light_scale(0xA1234567u,0)==0xA1000000u);
    assert(mm3_world_light_scale(0xA1234567u,100)==0xA1234567u);
    assert(mm3_world_ambient_lift(0xABFEFF01u,3)==0xABFFFF04u);
    assert(mm3_world_ambient_lift(0xFFFFFFFFu,16)==0xFFFFFFFFu);
    assert(mm3_world_ambient_lift(0xA1234567u,0)==0xA1234567u);
    assert(mm3_world_light_floor(0xAB003FFFu,64)==0xAB303FFFu);
    assert(mm3_world_light_floor(0xA1FFFFFFu,64)==0xA1FFFFFFu);
    assert(mm3_world_light_floor(0xA1234567u,0)==0xA1234567u);
    assert(mm3_world_light_floor(0xAB000000u,255)==0xAB303030u);
    for(time=0;time<sizeof times/sizeof *times;time++) {
        unsigned sample,caller;
        time_of_day=times[time];
        for(sample=0;sample<sizeof native_colors/sizeof *native_colors;sample++) {
            for(caller=0;caller<sizeof callers/sizeof *callers;caller++) {
                query(callers[caller],native_colors[sample],lift,even);
                directional_query(callers[caller],native_colors[sample],even,0);
                directional_query(callers[caller],native_colors[sample],even,1);
            }
            quad(native_colors[sample],lift,even);
        }
        for(i=0;i<64;i++) {
            seed=seed*1664525u+1013904223u;
            query(callers[i%6],seed,lift,even);
            directional_query(callers[i%6],seed,even,0);
            directional_query(callers[i%6],seed,even,1);
        }
        for(i=0;i<3;i++) { composite(i,even,1);composite(i,even,0); }
        for(i=0;i<256;i++) quad(0xA1000000u+i*0x010101u,lift,even);
    }
    puts("PASS: night-only RGB scale, day/invalid-time identity, ambient ABI, optional scoped boosts, LightApply vertices, native composite mode and unrelated renderer, alpha/unrelated callers intact");
    return 0;
}
'''


def main():
    if not shutil.which("cl"):
        raise SystemExit("Run scripts/enter-env.ps1 first.")
    getter = generated("sub_000FF19A").replace("sub_000FF19A(void)", "sub_000FF19A_original(void)")
    draw = generated("sub_00230DEE")
    assert "PUSH32(esp, 0x00230F75u); RECOMP_ABI_CALL(0x000FF19Au, sub_000FF19A)" in draw
    assert "loc_00230F75: ;\n    PUSH32(esp, ecx);" in draw
    assert "    ebx = eax;" in draw[draw.index("loc_00230F75:"):draw.index("loc_00230F8D:")]
    assert "PUSH32(esp, 0x00230F8Du); RECOMP_ABI_CALL(0x000FF324u, sub_000FF324)" in draw
    assert "PUSH32(esp, 0x00230FA6u); RECOMP_ABI_CALL(0x000FF15Cu, sub_000FF15C)" in draw
    # Mode 1 bypasses exactly the pre/post local-light passes. The native
    # exposure-readback helper does not consume this mode while it is scoped.
    assert len(re.findall(r"^    _fa = .*MEM32\(edi \+ 4\).*$", draw, re.M)) == 2
    assert "if (CMP_NE(_fa, _fb)) goto loc_00230E8B;" in draw
    assert "if (CMP_NE(_fa, _fb)) goto loc_002312AD;" in draw
    assert "MEM32(edi + 0x5884) = MEM32(edi + 0x5884) & 0;" in draw
    assert "esi = MEM32(edi + 0x5868);" in draw
    assert "esi = MEM32(edi + 0x586C);" in draw
    assert "MEM32(edi + 4)" not in generated("sub_002305D1")
    start, end = draw.index("loc_002310B3:"), draw.index("    POP32(esp, ebx);", draw.index("loc_002310B3:"))
    quad = ("static void native_quad(void)\n{\n    uint32_t ebp=0x30000;\n"
            "    uint32_t _fa=0,_fb=0;int32_t _fas=0,_fbs=0;\n" +
            getter[getter.index("    #define fp_push"):getter.index("loc_000FF19A:")] +
            draw[start:end] + "\n" + getter[getter.index("    #undef fp_push"):])
    with zipfile.ZipFile(ROOT / "game_files/Data/Data_hd.zip") as archive:
        shader = archive.read("Light/LightApply.psh").decode("ascii")
        assert "v1.rgb = Ambient color" in shader and "r0.rgb, v1.rgb" in shader
        assert "mul_x4\tr0.rgb, t0.rgb, r0.rgb" in shader
        local_shader = archive.read("Light/LightApplyTex.psh").decode("ascii")
        assert "mad\t\tr0.rgb, t0.rgb, t1.rgb, r0.rgb" in local_shader
        assert "v0.rgb = Diffuse color" in shader and "c2.rgb = Backlight color" in shader
    cmake = (ROOT / "CMakeLists.txt").read_text()
    definitions = set()
    for names in re.findall(r"list\(APPEND MM3_DEFINITION_ONLY_FUNCTIONS\s+([^)]+)\)", cmake):
        definitions.update(names.split())
    assert {"sub_000FF19A", "sub_000FF324", "sub_000FF15C", "sub_00230DEE"} <= definitions
    assert "src/mm3_world_lighting.c" in cmake
    with tempfile.TemporaryDirectory(prefix="mm3-world-lighting-") as temporary:
        folder = Path(temporary)
        shutil.copyfile(ROOT / "src/mm3_recomp_types.h", folder / "recomp_types.h")
        (folder / "native_getter.inc").write_text(getter)
        (folder / "native_quad.inc").write_text(quad)
        source, executable = folder / "contract.c", folder / "contract.exe"
        source.write_text(SOURCE)
        subprocess.run(["cl", "/nologo", "/W4", "/WX", "/Od", "/std:c11",
                        "/D_CRT_SECURE_NO_WARNINGS", "/I" + str(folder),
                        "/I" + str(ROOT / "src"), "/I" + str(ROOT / "tools/xboxrecomp/include"),
                        "/Fo:" + str(folder / "contract.obj"), "/Fe:" + str(executable), str(source)],
                       check=True, cwd=folder)
        cases = [(None, None, 0, 0), ("", "", 0, 0)]
        cases += [(setting, "0", lift, 0) for setting, lift in
                  (("0", 0), ("1", 1), ("3", 3), ("16", 16),
                   ("-1", 0), ("17", 0), ("NaN", 0), ("3.5", 0),
                   ("999999999999999999999999999999", 0))]
        cases += [(None, setting, 0, even) for setting, even in
                  (("0", 0), ("1", 1), ("2", 0), ("off", 0))]
        cases += [("3", "1", 3, 1), ("NaN", "off", 0, 0)]
        cases = [(setting, even_setting, None, lift, even, 25)
                 for setting, even_setting, lift, even in cases]
        cases += [(None, None, setting, 0, 0, percent) for setting, percent in
                  (("", 25), ("0", 0), ("25", 25), ("100", 100),
                   ("-1", 25), ("101", 25), ("NaN", 25), ("25.5", 25),
                   ("999999999999999999999999999999", 25))]
        cases += [("3", "1", "0", 3, 1, 0), ("3", "1", "100", 3, 1, 100)]
        for setting, even_setting, night_setting, lift, even, percent in cases:
            env = os.environ.copy()
            if setting is None:
                env.pop("MM3_AMBIENT_LIFT", None)
            else:
                env["MM3_AMBIENT_LIFT"] = setting
            if even_setting is None:
                env.pop("MM3_EVEN_LIGHTING", None)
            else:
                env["MM3_EVEN_LIGHTING"] = even_setting
            if night_setting is None:
                env.pop("MM3_NIGHT_WORLD_LIGHT", None)
            else:
                env["MM3_NIGHT_WORLD_LIGHT"] = night_setting
            result = subprocess.run([str(executable), str(lift), str(even), str(percent)], env=env,
                                    capture_output=True, text=True, check=True)
            assert result.stderr.count("world ambient lift=") == 1
            assert f"world ambient lift={lift}/255 even_lighting={even}" in result.stderr
            assert f"world_rgb_floor={48 if even else 0}/255" in result.stderr
            if setting in ("-1", "17", "NaN", "3.5", "999999999999999999999999999999"):
                assert "invalid MM3_AMBIENT_LIFT; expected integer 0..16, using 0" in result.stderr
            if even_setting in ("2", "off"):
                assert "invalid MM3_EVEN_LIGHTING; expected 0 or 1, using 0" in result.stderr
            if night_setting in ("-1", "101", "NaN", "25.5", "999999999999999999999999999999"):
                assert "invalid MM3_NIGHT_WORLD_LIGHT; expected integer 0..100, using 25" in result.stderr
        print("PASS: night-only 25% world defaults, day/invalid-time identity, bounded night scale and optional lift/floor, all three roles, ambient ABI, native local-light modes and quad/material math")


if __name__ == "__main__":
    main()
