"""Run the car-source hook around the real native effect producer and queue.

Only flare graphics submission is stubbed. Matrix helpers, source activation,
native cone production and the replacement queue records execute real C.
"""
from pathlib import Path
import importlib.util
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "native_projector", ROOT / "tests/mm3_headlight_projector_regression.py")
native_projector = importlib.util.module_from_spec(spec)
spec.loader.exec_module(native_projector)


def audit_assets():
    sys.path.insert(0, str(ROOT / "tools/car-models"))
    import mm3_model
    from ensure_night_lights import ensure_night_light
    models = shared = ambient = fallback = 0
    installed = sorted((ROOT / "game_files/Data/Shared/Cars").glob("*/body.cmp"))
    installed += sorted((ROOT / "game_files/TDATA").rglob("body.cmp"))
    for path in installed:
        model = mm3_model.read_model(path.read_bytes())
        lamps = []
        for _, node in model.root.walk():
            if node.kind != 4:
                continue
            at = 5 + len(node.name)
            kind = struct.unpack_from("<I", node.prefix, at)[0]
            brightness = struct.unpack_from("<f", node.prefix, at + 12)[0]
            color = struct.unpack_from("<I", node.prefix, at + 28)[0]
            lamps.append((kind, brightness, color))
        heads = any(kind == 0 and brightness > 0 and color & 0xffffff
                    for kind, brightness, color in lamps)
        beams = [color for kind, _, color in lamps if kind == 8]
        if beams:
            assert heads or all(color == 0 for color in beams), path
            assert not heads or all(color != 0 for color in beams), path
            shared += 1
            fallback += not heads
        elif heads:
            ambient += 1
        else:
            raise AssertionError(f"No front-light source; run ensure_night_lights.py: {path}")
        assert ensure_night_light(path.read_bytes()) == (path.read_bytes(), None), path
        models += 1
    assert shared and ambient and fallback, (models, shared, ambient, fallback)
    print(f"PASS: {models} installed models; {shared} shared-beam models, "
          f"{ambient} front-lamp-only traffic models, {fallback} zero-color fallbacks")


CHECKS = r'''
#include "mm3_car_lighting.c"
static unsigned flares;
static void setup(unsigned kind, uint32_t color, float brightness, unsigned mask, float yaw)
{
    unsigned i;
    memset(memory,0,sizeof memory);g_xbox_mem_offset=(ptrdiff_t)memory;
    g_eax=g_edx=g_ebx=g_esi=g_edi=g_ebp=g_seh_ebp=g_fs_base=g_eflags=0;
    g_ecx=NODE;g_esp=STACK;g_fp_top=g_df=g_fp_cmp=0;
    g_fp_cc=0;g_fp_control_word=0x27f;
    memset(g_fp_stack,0,sizeof g_fp_stack);native_constants();flares=0;
    MEM32(0x3c55a0)=WORLD;MEM32(WORLD+0x24c)=0x1234;
    MEM32(0x3c5d8c)=QUEUE;
    for(i=0;i<256;++i) MEM32(QUEUE+0x5410+i*4)=RECORD+i*0x54;
    MEM32(NODE+4)=OWNER;MEM32(NODE+0x34)=kind;
    MEM32(NODE+0x64)=color;
    MEMF(NODE+0x54)=MEMF(NODE+0x78)=brightness;
    MEMF(NODE+0x5c)=MEMF(NODE+0x7c)=.03f;
    MEM8(OWNER+0xf3)=1;
    MEMF(MATRIX)=cosf(yaw);MEMF(MATRIX+8)=-sinf(yaw);
    MEMF(MATRIX+0x10)=1;
    MEMF(MATRIX+0x18)=sinf(yaw);MEMF(MATRIX+0x20)=cosf(yaw);
    MEMF(MATRIX+0x24)=13;MEMF(MATRIX+0x28)=.7f;MEMF(MATRIX+0x2c)=-8;
    MEM32(STACK)=0x12345678;
    MEM32(STACK+4)=MATRIX;MEM32(STACK+8)=0;MEM32(STACK+12)=mask;
    MEM32(0x407c30)=3;
}
static void run(void)
{
    sub_000CF2DA();
    assert(g_esp==STACK+16 && g_fp_top==0);
    assert(!g_ebx && !g_esi && !g_edi);
}
static uint32_t entry(unsigned i) {return MEM32(QUEUE+0x5410+i*4);}
static void direction_checks(void)
{
    unsigned i,j;
    for(i=0;i<8;++i) {
        float yaw=i*3.14159265358979f/4;
        float forward[3]={sinf(yaw),0,cosf(yaw)};
        float source[12], expected[16];
        uint32_t cone;
        /* Real native producer/inverse must agree on forward and position. */
        setup(8,0xffffffffu,.8f,0x100,yaw);
        memcpy(source,(const void *)XBOX_PTR(MATRIX),sizeof source);
        assert(car_light_matrix(source,0,expected));
        sub_000CF2DA_original();
        assert(MEM32(QUEUE+0x5810)==1);
        for(j=0;j<3;++j) {
            assert(fabsf(MEMF(RECORD+0x20+j*4)-forward[j])<.0001f);
            assert(fabsf(MEMF(RECORD+0x30+j*4)-source[9+j])<.0001f);
            assert(fabsf(expected[8+j]-forward[j])<.0001f);
        }
        setup(0,0xffffffffu,.8f,1,yaw);run();
        assert(MEM32(QUEUE+0x5810)==2);
        assert(MEM32(entry(0)+0x50)==0 && MEM32(entry(1)+0x50)==1);
        assert(flares==1);
        cone=entry(1);
        assert(MEMF(cone+0x24)<0); /* beam pitches toward road */
        assert(MEMF(cone+0x20)*forward[0]+MEMF(cone+0x28)*forward[2]>.99f);
        assert(MEMF(cone+0x40)>MEMF(cone+0x44));
        assert(MEMF(cone+0x48)==38);
    }
}
int main(void)
{
    unsigned kind;
    direction_checks();
    /* Native on/off masks, brightness, scene availability remain gates. */
    setup(0,0xffffffffu,.8f,0,0);run();assert(!MEM32(QUEUE+0x5810)&&!flares);
    setup(0,0xffffffffu,0,1,0);run();assert(!MEM32(QUEUE+0x5810)&&!flares);
    setup(0,0xffffffffu,.8f,1,0);MEM8(WORLD+0x2a8)=1;run();assert(!MEM32(QUEUE+0x5810));
    setup(0,0xffffffffu,.8f,1,0);MEM32(WORLD+0x24c)=0;run();assert(!MEM32(QUEUE+0x5810));
    /* Ambient traffic needs neither the player-only flag nor HeadLightEnv. */
    setup(0,0xffffffffu,.8f,1,0);MEM8(OWNER+0xf3)=0;run();assert(MEM32(QUEUE+0x5810)==2);
    setup(8,0xffffffffu,.8f,0x100,0);run();assert(!MEM32(QUEUE+0x5810));
    assert(MEM32(STACK+12)==0x100 && MEM32(NODE+0x64)==0xffffffffu);
    setup(8,0,0,0x100,0);run();assert(MEM32(QUEUE+0x5810)==2);
    assert(MEM32(NODE+0x64)==0 && MEMF(NODE+0x54)==0);
    setup(8,0,0,0x100,0);MEM32(0x407c30)=1;run();assert(!MEM32(QUEUE+0x5810));
    for(kind=1;kind<=3;++kind) {
        setup(kind,kind==3?0xffffffffu:0xffff0000u,.8f,1u<<kind,0);run();
        assert(MEM32(QUEUE+0x5810)==1 && MEM32(entry(0)+0x50)==0);
        if(kind!=3) assert((MEM32(entry(0)+0x4c)&0xffff)==0);
        assert(MEMF(entry(0)+0x40)==(kind==1?2.5f:kind==2?3.6f:6));
        setup(kind,0xffffffffu,.8f,0,0);run();assert(!MEM32(QUEUE+0x5810));
    }
    for(kind=5;kind<=6;++kind) {
        setup(kind,0xffff9900u,.8f,1u<<kind,0);run();assert(MEM32(QUEUE+0x5810)==1);
        setup(kind,0xffff9900u,.8f,1u<<kind,0);
        MEM8(OWNER+0xed)=1;MEMF(WORLD+0x1c0)=4;MEMF(OWNER+0x12c)=1;
        run();assert(!MEM32(QUEUE+0x5810)&&!flares);
    }
    setup(4,0xffff0000u,.8f,1u<<4,0);run();assert(MEM32(QUEUE+0x5810)==1);
    setup(9,0xffffffffu,.8f,1u<<9,0);run();assert(MEM32(QUEUE+0x5810)==1);
    /* Point lights preserve road-beam reserve; full queues remain bounded. */
    setup(0,0xffffffffu,.8f,1,0);MEM32(QUEUE+0x5810)=192;run();assert(MEM32(QUEUE+0x5810)==193);
    setup(0,0xffffffffu,.8f,1,0);MEM32(QUEUE+0x5810)=256;run();assert(MEM32(QUEUE+0x5810)==256);
    puts("PASS: native car source, eight headings, traffic/F1 coverage, lamp activation/color/radii, queue reserve and ABI");
    return 0;
}
'''


def main():
    if not shutil.which("cl"):
        raise SystemExit("Run scripts/enter-env.ps1 first so cl.exe is available.")
    audit_assets()
    native = native_projector.generated().replace(
        "void sub_000CF2DA(void)", "void sub_000CF2DA_original(void)")
    source = native_projector.SOURCE.split("static unsigned submit", 1)[0]
    source = source.replace("memory[0x400000]", "memory[0x500000]")
    source = source.replace('void sub_001A842A(void) { assert(!"unexpected flare submission"); }',
                            'static unsigned flares;\nvoid sub_001A842A(void) { ++flares; g_esp+=16; }')
    source += CHECKS.replace("static unsigned flares;", "")
    with tempfile.TemporaryDirectory(prefix="mm3-car-lighting-") as temporary:
        folder = Path(temporary)
        shutil.copyfile(ROOT / "src/mm3_recomp_types.h", folder / "recomp_types.h")
        declarations = "\n".join(f"void {name}(void);" for name in native_projector.NATIVE)
        declarations += "\nvoid sub_000CF2DA_original(void);\n"
        (folder / "native.inc").write_text(declarations + native)
        (folder / "constants.inc").write_text(native_projector.constants(native))
        (folder / "contract.c").write_text(source)
        exe = folder / "contract.exe"
        subprocess.run(["cl", "/nologo", "/W4", "/WX", "/Od", "/std:c11",
                        "/I" + str(folder), "/I" + str(ROOT / "src"),
                        "/I" + str(ROOT / "tools/xboxrecomp/include"),
                        "/Fo:" + str(folder / "contract.obj"), "/Fe:" + str(exe),
                        str(folder / "contract.c")], check=True, cwd=folder)
        subprocess.run([str(exe)], check=True, cwd=folder)


if __name__ == "__main__":
    main()
