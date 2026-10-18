"""Keep live vehicle illumination independent of native body-frustum rejection.

Runs both generated render entries through the real CMake seam, native sphere
culling and native matrix multiplication. Only unrelated LOD and unused body/
flare paths are boundaries; no game assets, saves or production files change.
--benchmark also measures the full source-only hierarchy walk and emission in
/O2 using real installed hierarchy layouts, transforms and lamp parameters.
QPC samples include native queue reset and 51 scene slots, without rendering.
"""
from pathlib import Path
import argparse
import importlib.util
import re
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "car_sources", ROOT / "tests/mm3_car_lighting_regression.py")
car_sources = importlib.util.module_from_spec(spec)
spec.loader.exec_module(car_sources)
projector = car_sources.native_projector
SEAMS = ("sub_000E01C2", "sub_000E0277")
NATIVE = projector.NATIVE + SEAMS + (
    "sub_00036FD9", "sub_000139C8", "sub_000DE5BC", "sub_000DE57E",
    "sub_000DE593", "sub_000E2D26", "sub_000717F2", "sub_0023A32E",
)


def benchmark_image(folder):
    """Serialize actual retail hierarchy shapes/transforms into guest objects."""
    import mm3_model
    installed = sorted((ROOT / "game_files/Data/Shared/Cars").glob("*/body.cmp"))
    installed += sorted((ROOT / "game_files/TDATA").rglob("body.cmp"))
    models = [mm3_model.read_model(path.read_bytes()) for path in installed]
    region, graphs, counts = bytearray(), [], []
    base = 0x600000

    def allocate(size):
        at = base + len(region)
        region.extend(bytes((size + 3) & ~3))
        return at

    def put(at, fmt, *values):
        struct.pack_into("<" + fmt, region, at - base, *values)

    def emit(node, graph=None):
        at = allocate(0x180)
        if graph is None:
            graph = at
        put(at, "I", {0: 0x1e100, 1: 0x378758, 4: 0x372af0}.get(node.kind, 0x1e200))
        if node.kind == 0:
            put(at + 4, "I", 0x18000)
            put(at + 0x124, "I", 0x103)
        elif node.kind == 1:
            local = struct.unpack_from("<12f", node.prefix, node.transform_offset + 4 + 48)
            put(at + 0x6c, "12f", *local)
        elif node.kind == 4:
            off = 5 + len(node.name)
            kind = struct.unpack_from("<I", node.prefix, off)[0]
            brightness = struct.unpack_from("<f", node.prefix, off + 12)[0]
            color = struct.unpack_from("<I", node.prefix, off + 28)[0]
            put(at + 4, "I", graph)
            put(at + 0x34, "I", kind)
            put(at + 0x54, "f", brightness)
            put(at + 0x64, "I", color)
            put(at + 0x78, "2f", brightness, .03)
            put(at + 0x5c, "f", .03)
        children = [emit(child, graph) for child in node.children]
        if children:
            vector = allocate(4 * len(children))
            put(vector, "I" * len(children), *children)
            put(at + 0xc, "2I", vector, vector + 4 * len(children))
        return at

    for car in range(320):
        model = models[car % len(models)]
        nodes = [node for _, node in model.root.walk()]
        counts.append((len(nodes) - 1, sum(n.kind == 1 for n in nodes), sum(n.kind == 4 for n in nodes)))
        graphs.append(emit(model.root))
    assert base + len(region) < 0x2000000
    path = folder / "retail-hierarchies.bin"
    path.write_bytes(struct.pack("<320I", *graphs) + region)
    for cars in (80, 320):
        totals = [sum(row[column] for row in counts[:cars]) for column in range(3)]
        print(f"BENCH fixture: {cars} model instances, {totals[0]} serialized non-root hierarchy nodes, "
              f"{totals[1]} transforms, {totals[2]} retail lamps; all branches enabled, "
              "current authored relative matrices/colors/brightness, night mask0x103, depth<=5", flush=True)
    return path


def audit_ancestors():
    sys.path.insert(0, str(ROOT / "tools/car-models"))
    import mm3_model
    models = lamps = deepest = 0
    installed = sorted((ROOT / "game_files/Data/Shared/Cars").glob("*/body.cmp"))
    installed += sorted((ROOT / "game_files/TDATA").rglob("body.cmp"))
    for path in installed:
        model = mm3_model.read_model(path.read_bytes())

        def visit(node, ancestors):
            nonlocal lamps, deepest
            if node.kind == 4:
                assert ancestors and ancestors[0] == 0, (path, node.name, ancestors)
                assert all(kind == 1 for kind in ancestors[1:]), (path, node.name, ancestors)
                lamps += 1
                deepest = max(deepest, len(ancestors) - 1)
            for child in node.children:
                visit(child, ancestors + (node.kind,))

        visit(model.root, ())
        models += 1
    assert models >= 264 and lamps >= 3405 and deepest <= 5, (models, lamps, deepest)
    print(f"PASS: {models} installed models, {lamps} lamps; only root/transform ancestors, "
          f"at most {deepest} nested transforms")


CHECKS = r'''
#include "mm3_car_light_visibility.c"
enum { ACTOR=0x18000, CAMERA=0x1b000, GRAPH_TABLE=0x1e000,
       T0=0x15000, T1=0x15200, V0=0x14000, V1=0x14100, V2=0x14200,
       ENV=NODE+0x200, TAIL=NODE+0x400, BRAKE=NODE+0x600, SIGNAL=NODE+0x800,
       PARENT=0x1f000, EXPECT0=0x1f100, EXPECT1=0x1f200, EXPECT_ROOT=0x1f300 };
static unsigned lod_updates;

static void fixture_icall(uint32_t address)
{
    switch(address) {
    case 0x000de5bcu:sub_000DE5BC();break;
    case 0x000de57eu:sub_000DE57E();break;
    case 0x000de593u:sub_000DE593();break;
    default:assert(!"body/update/flare virtual call escaped its native cull");
    }
}

/* The LOD manager changes model residency, which this small fixed graph does
 * not model. It has no role in matrix composition or body-frustum acceptance. */
void sub_000DFEF9(void) {++lod_updates;g_esp+=8;}

static void pose(uint32_t at,float angle,float bank,float x,float y,float z)
{
    const float forward[3]={sinf(angle),0,cosf(angle)};
    const float right[3]={cosf(angle)*cosf(bank),sinf(bank),-sinf(angle)*cosf(bank)};
    const float up[3]={-cosf(angle)*sinf(bank),cosf(bank),sinf(angle)*sinf(bank)};
    unsigned i;
    for(i=0;i<3;++i) {
        MEMF(at+4*i)=right[i];MEMF(at+12+4*i)=up[i];MEMF(at+24+4*i)=forward[i];
    }
    MEMF(at+36)=x;MEMF(at+40)=y;MEMF(at+44)=z;
}

static void light_node(uint32_t node,unsigned kind,uint32_t color)
{
    if(node!=NODE)memcpy((void *)XBOX_PTR(node),(const void *)XBOX_PTR(NODE),0x84);
    MEM32(node)=0x372af0;MEM32(node+4)=OWNER;
    MEM32(node+0x34)=kind;MEM32(node+0x64)=color;
}

static void visibility_setup(uint32_t mask)
{
    setup(0,0xffffffffu,.8f,1,0);
    yaw(0);lod_updates=0;
    MEM32(OWNER)=GRAPH_TABLE;MEM32(OWNER+4)=ACTOR;
    MEM32(OWNER+0x34)=MEM32(OWNER+0x38)=4;
    MEM8(OWNER+0xf3)=0;MEM32(OWNER+0x124)=mask;
    MEM32(OWNER+0xc)=V0;MEM32(OWNER+0x10)=V0+4;MEM32(V0)=T0;
    MEM32(GRAPH_TABLE+0x88)=0x000de5bc;MEM32(GRAPH_TABLE+0x94)=0x000de57e;
    MEM32(GRAPH_TABLE+0x90)=0x000de593;
    MEM32(0x372af0+0x40)=0x000cf2da;
    MEM32(0x378758+0x40)=0x000de780;
    MEM32(T0)=MEM32(T1)=0x378758;
    MEM32(T0+0xc)=V1;MEM32(T0+0x10)=V1+16;
    MEM32(V1)=T1;MEM32(V1+4)=TAIL;MEM32(V1+8)=BRAKE;MEM32(V1+12)=SIGNAL;
    MEM32(T1+0xc)=V2;MEM32(T1+0x10)=V2+8;MEM32(V2)=NODE;MEM32(V2+4)=ENV;
    pose(T0+0x6c,0,0,0,0,0);pose(T1+0x6c,0,0,.7f,0,2);
    pose(OWNER+0x6c,0,0,13,.7f,-8);
    memcpy((void *)XBOX_PTR(OWNER+0x3c),(const void *)XBOX_PTR(OWNER+0x6c),48);
    pose(PARENT,0,0,0,0,0);
    light_node(NODE,0,0xffffffffu);light_node(ENV,8,0xffffffffu);
    light_node(TAIL,1,0xffff0000u);light_node(BRAKE,2,0xffff0000u);
    light_node(SIGNAL,5,0xffff9900u);
    MEM32(0x3c5588)=CAMERA;MEM32(CAMERA+0x1ea4)=1;
    /* The car remains behind the camera (z<0) while its 50 m headlights can
     * still reach the road in front. The actual native sphere test rejects it. */
    MEMF(CAMERA+0x1e54+8)=-1;MEMF(OWNER+0xa8)=2;
}

static void native_matrix(uint32_t out,uint32_t local,uint32_t parent)
{
    g_eax=out;g_edx=parent;g_esp=STACK;MEM32(STACK+4)=local;
    sub_00036FD9();assert(g_esp==STACK+8 && g_fp_top==0);
    g_eax=g_edx=g_ebx=g_esi=g_edi=g_ebp=g_seh_ebp=0;g_esp=STACK;
}

static void rejected_body(unsigned alternate,uint32_t selector)
{
    uint32_t frame=STACK-4;
    MEM32(STACK)=0x12345678;MEM32(STACK+4)=PARENT;MEM32(STACK+8)=selector;
    g_ebx=OWNER;g_esi=g_edi=g_ebp=g_seh_ebp=0;g_esp=STACK;
    if(!alternate) sub_000E01C2();
    else {
        /* Recreate the genuine overlapping entry's incoming frame/arguments
         * at E0277: E01C2 has already saved EDI/ESI and pushed selector. */
        MEM32(frame)=0;MEM32(STACK-128)=MEM32(STACK-132)=0;
        MEM32(STACK-136)=selector;
        g_ebp=g_seh_ebp=frame;g_esp=STACK-136;
        g_eax=GRAPH_TABLE;g_esi=OWNER+0x6c;g_edi=selector;
        sub_000E0277();
    }
    assert(g_esp==STACK+12 && g_fp_top==0 && !g_esi && !g_edi);
    assert(MEM32(OWNER+0x11c+selector*4)&2);
    assert(flares==0);
    g_ebx=g_ebp=g_seh_ebp=0;
}

static void check_front(uint32_t expected)
{
    float source[12],heading[3],matrix[16];
    unsigned i;
    uint32_t cone=entry(1),pool=entry(0);
    memcpy(source,(const void *)XBOX_PTR(expected),sizeof source);
    memcpy(heading,source+6,sizeof heading);
    assert(car_light_matrix(source,heading,CAR_BEAM_PITCH,matrix));
    for(i=0;i<16;++i)assert(fabsf(MEMF(cone+4*i)-matrix[i])<.00002f);
    assert(MEM32(cone+0x50)==1 && MEM32(pool+0x50)==0);
}

static void offscreen_motion_checks(void)
{
    unsigned i,alternate;
    for(alternate=0;alternate<2;++alternate) {
        visibility_setup(1);
        pose(PARENT,.06f,.03f,-.5f,1,1);
        pose(T0+0x6c,-.11f,.07f,.2f,.1f,0);
        pose(T1+0x6c,.18f,-.04f,.7f,0,2);
        for(i=0;i<24;++i) {
            native_reset(QUEUE);flares=0;
            MEM32(OWNER+0x34)=MEM32(OWNER+0x38)=(i&1)?0:4;
            pose(OWNER+0x6c,i*.035f,.12f*sinf(i*.2f),13+i*.2f,.7f,-8-i*.05f);
            /* Exercise both native absolute-copy and relative-composition
             * branches, with stale cached world data and nontrivial locals. */
            pose(OWNER+0x3c,2,1,-300,-200,-100);
            if(i&1) native_matrix(EXPECT_ROOT,OWNER+0x6c,PARENT);
            else memcpy((void *)XBOX_PTR(EXPECT_ROOT),(const void *)XBOX_PTR(OWNER+0x6c),48);
            native_matrix(EXPECT0,T0+0x6c,EXPECT_ROOT);
            native_matrix(EXPECT1,T1+0x6c,EXPECT0);
            rejected_body(alternate,1);
            assert(MEM32(QUEUE+0x5810)==2);check_front(EXPECT1);
        }
        /* Later on-screen callbacks update those same fields, while their
         * visible lenses remain on the original native path. */
        memcpy((void *)XBOX_PTR(MATRIX),(const void *)XBOX_PTR(EXPECT1),48);
        MEM32(STACK+12)=1;lamp(NODE,OWNER,MEMF(EXPECT1+36));
        assert(MEM32(QUEUE+0x5810)==2 && flares==1);check_front(EXPECT1);
        environment(OWNER);assert(MEM32(QUEUE+0x5810)==2);check_front(EXPECT1);
        again(0,1);assert(MEM32(QUEUE+0x5810)==2);
    }
}

static void mask_and_damage_checks(void)
{
    unsigned i;
    const uint32_t masks[]={1,2,4,1|2|4,1u<<5,0};
    const unsigned counts[]={2,1,1,4,1,0};
    visibility_setup(1);
    for(i=0;i<sizeof masks/sizeof masks[0];++i) {
        native_reset(QUEUE);MEM32(OWNER+0x124)=masks[i];
        mm3_car_lighting_model(OWNER,0);
        assert(MEM32(QUEUE+0x5810)==counts[i] && flares==0);
        if(masks[i]==2 || masks[i]==4)assert((MEM32(entry(0)+0x4c)&0xffff)==0);
    }
    native_reset(QUEUE);MEM32(OWNER+0x124)=1u<<5;
    MEM8(OWNER+0xed)=1;MEMF(WORLD+0x1c0)=4;MEMF(OWNER+0x12c)=1;
    mm3_car_lighting_model(OWNER,0);assert(MEM32(QUEUE+0x5810)==0);
    MEMF(OWNER+0x12c)=3;mm3_car_lighting_model(OWNER,0);
    assert(MEM32(QUEUE+0x5810)==1);
    visibility_setup(1);MEM8(ACTOR+0xec)=1;mm3_car_lighting_model(OWNER,0);
    assert(MEM32(QUEUE+0x5810)==0);
    MEM8(ACTOR+0xec)=0;MEM32(T1+0x34)=1;mm3_car_lighting_model(OWNER,0);
    assert(MEM32(QUEUE+0x5810)==0);
    mm3_car_lighting_model(OWNER,1);assert(MEM32(QUEUE+0x5810)==2);
    native_reset(QUEUE);MEM32(T1+0x34)=4;mm3_car_lighting_model(OWNER,0);
    assert(MEM32(QUEUE+0x5810)==0);
    MEM32(T1+0x34)=0;MEM32(NODE+4)=OWNER+0x1000;
    mm3_car_lighting_model(OWNER,0);assert(MEM32(QUEUE+0x5810)==0);
    visibility_setup(1);MEMF(T1+0x6c)=NAN;mm3_car_lighting_model(OWNER,0);
    assert(MEM32(QUEUE+0x5810)==0);
    visibility_setup(1);MEM8(WORLD+0x2a8)=1;mm3_car_lighting_model(OWNER,0);
    assert(MEM32(QUEUE+0x5810)==0);
    MEM8(WORLD+0x2a8)=0;MEM32(WORLD+0x24c)=0;mm3_car_lighting_model(OWNER,0);
    assert(MEM32(QUEUE+0x5810)==0);
    /* Generic groups forward the current parent transform, while rotating
     * effects keep their phase updates on the original native draw path. */
    visibility_setup(1);MEM32(T1)=GRAPH_TABLE+0x100;
    MEM32(GRAPH_TABLE+0x140)=0x000df751;
    mm3_car_lighting_model(OWNER,0);assert(MEM32(QUEUE+0x5810)==2);
    visibility_setup(1|(1u<<4)|(1u<<9));MEM32(NODE+0x34)=4;MEM32(ENV+0x34)=9;
    mm3_car_lighting_model(OWNER,0);assert(MEM32(QUEUE+0x5810)==0);
}

static void time_of_day_visibility_checks(void)
{
    unsigned period,alternate;
    for(period=0;period<4;++period) {
        unsigned dark=period>=2;
        for(alternate=0;alternate<2;++alternate) {
            visibility_setup(0x101);MEM32(0x407c30)=period;
            rejected_body(alternate,1);
            assert(MEM32(QUEUE+0x5810)==(dark?2u:0u));
            assert(MEM32(OWNER+0x124)==0x101 && !flares);
        }
        visibility_setup(0x107);MEM32(0x407c30)=period;
        mm3_car_lighting_model(OWNER,0);
        assert(MEM32(QUEUE+0x5810)==(dark?4u:2u) && !flares);
        /* Tail/brake remain active while only front sources are masked. */
        assert(MEMF(entry(dark?1:0)+0x40)==(dark?4.4f:2.2f));
        assert(MEMF(entry(dark?2:1)+0x40)==6.8f);
    }
    puts("PASS: both real native offscreen render entries gate front sources by period; head-only daytime models skip collection; mixed tail/brake remain active with smaller daylight tail gradient");
}

static void bounded_walk_checks(void)
{
    unsigned i;
    visibility_setup(1);MEM32(V2)=T0;MEM32(T1+0x10)=V2+4;
    mm3_car_lighting_model(OWNER,0);assert(MEM32(QUEUE+0x5810)==0);
    visibility_setup(1);MEM32(OWNER+0x10)=V0-4;
    mm3_car_lighting_model(OWNER,0);assert(MEM32(QUEUE+0x5810)==0);
    visibility_setup(1);MEM32(QUEUE+0x5810)=256;
    mm3_car_lighting_model(OWNER,0);assert(MEM32(QUEUE+0x5810)==256);
    visibility_setup(1);
    for(i=0;i<320;++i) {
        uint32_t graph=0xa0000+i*0x400,node=graph+0x180,vector=graph+0x240;
        memcpy((void *)XBOX_PTR(graph),(const void *)XBOX_PTR(OWNER),0x128);
        memcpy((void *)XBOX_PTR(node),(const void *)XBOX_PTR(NODE),0x84);
        MEM32(node+4)=graph;MEM32(graph+0xc)=vector;MEM32(graph+0x10)=vector+4;MEM32(vector)=node;
        MEMF(graph+0x3c+36)=13+i*.2f;
        mm3_car_lighting_model(graph,0);
    }
    assert(MEM32(QUEUE+0x5810)==256 && s_car_groups<=CAR_GROUP_SLOTS && flares==0);
    for(i=0;i<256;++i)assert(entry(i)>=RECORD && entry(i)<RECORD+256*0x54);
}

static void volume_culling_checks(void)
{
    const float unit=.7071067812f;
    float matrix[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    unsigned i;
    visibility_setup(1);MEM32(CAMERA+0x1ea4)=5;
    memset((void *)XBOX_PTR(CAMERA+0x1e54),0,5*16);
    MEMF(CAMERA+0x1e54)=-unit;MEMF(CAMERA+0x1e54+8)=-unit;
    MEMF(CAMERA+0x1e64)=unit;MEMF(CAMERA+0x1e64+8)=-unit;
    MEMF(CAMERA+0x1e74+4)=-unit;MEMF(CAMERA+0x1e74+8)=-unit;
    MEMF(CAMERA+0x1e84+4)=unit;MEMF(CAMERA+0x1e84+8)=-unit;
    MEMF(CAMERA+0x1e94+8)=1;MEMF(CAMERA+0x1e94+12)=-80;
    pose(OWNER+0x6c,0,0,0,.7f,-8);
    rejected_body(0,1);
    assert(MEM32(QUEUE+0x5810)==2);
    native_reset(QUEUE);pose(OWNER+0x6c,0,0,0,.7f,-100);
    rejected_body(0,1);assert(MEM32(QUEUE+0x5810)==0);
    /* A conservative 5 cm border keeps rounding at a plane from dropping a
     * touching volume. Cone bounds start at the bulb and extend forward. */
    memset((void *)XBOX_PTR(CAMERA+0x1e54),0,5*16);
    MEMF(CAMERA+0x1e54)=1;
    matrix[12]=1.04f;assert(car_light_volume_visible(matrix,2,2,2,0));
    matrix[12]=1.06f;assert(!car_light_volume_visible(matrix,2,2,2,0));
    MEMF(CAMERA+0x1e54)=0;MEMF(CAMERA+0x1e54+8)=-1;
    matrix[12]=0;matrix[14]=-CAR_BEAM_LENGTH-.04f;
    assert(car_light_volume_visible(matrix,CAR_BEAM_WIDTH,CAR_BEAM_HEIGHT,CAR_BEAM_LENGTH,1));
    matrix[14]=-CAR_BEAM_LENGTH-.06f;
    assert(!car_light_volume_visible(matrix,CAR_BEAM_WIDTH,CAR_BEAM_HEIGHT,CAR_BEAM_LENGTH,1));
    /* Unready/invalid view data must fail open, including a malformed later
     * plane after an earlier plane would otherwise reject the source. */
    MEMF(CAMERA+0x1e94)=NAN;
    assert(car_light_volume_visible(matrix,CAR_BEAM_WIDTH,CAR_BEAM_HEIGHT,CAR_BEAM_LENGTH,1));
    MEMF(CAMERA+0x1e94)=0;MEM32(CAMERA+0x1ea4)=4;
    assert(car_light_volume_visible(matrix,CAR_BEAM_WIDTH,CAR_BEAM_HEIGHT,CAR_BEAM_LENGTH,1));
    for(i=0;i<2;++i) {
        MEM32(0x3c5588)=i?0x07ffffffu:0;
        assert(car_light_volume_visible(matrix,CAR_BEAM_WIDTH,CAR_BEAM_HEIGHT,CAR_BEAM_LENGTH,1));
    }
}

int main(void)
{
    offscreen_motion_checks();mask_and_damage_checks();time_of_day_visibility_checks();
    bounded_walk_checks();volume_culling_checks();
    puts("PASS: real native rejected-body seams (E01C2/E0277), live offscreen movement/turning/bank, native matrix reference, zero offscreen flares/geometry, current light masks/blink clock, actor/damage gates, later draw deduplication, bounded traversal, 320-model queue pressure and conservative full-volume frustum culling");
    return 0;
}
'''


BENCHMARK = r'''
#include <windows.h>
static uint32_t benchmark_graphs[320];
static unsigned benchmark_cars;
static volatile uint32_t benchmark_sink;
static void benchmark_frame(void)
{
    unsigned i;
    g_ecx=QUEUE;g_esp=STACK;sub_0022FDFD();
    /* Include the real native/host reset and 51 street/scene record slots. */
    for(i=0;i<51;++i) {
        uint32_t record=entry(i);
        memset((void *)XBOX_PTR(record),0,0x54);
        MEMF(record)=MEMF(record+0x14)=MEMF(record+0x28)=1;
        MEM32(record+0x4c)=0xff102030;
    }
    MEM32(QUEUE+0x5810)=51;
    for(i=0;i<benchmark_cars;++i)mm3_car_lighting_model(benchmark_graphs[i],1);
    benchmark_sink+=MEM32(QUEUE+0x5810);
}
static int benchmark_compare(const void *a,const void *b)
{
    double x=*(const double *)a,y=*(const double *)b;
    return (x>y)-(x<y);
}
static void benchmark(const char *path)
{
    FILE *file=NULL;
    LARGE_INTEGER frequency,start,finish;
    const float unit=.7071067812f;
    const float depths[3]={35,-8,-150};
    const char *names[3]={"visible","body-behind-camera","fully-outside"};
    unsigned cars,scenario,i,sample,frames,queued;
    double samples[7],elapsed;
    visibility_setup(0x103);s_dbg_on=0;
    assert(!fopen_s(&file,path,"rb") && file);
    assert(fread(benchmark_graphs,sizeof benchmark_graphs,1,file)==1);
    assert(fread(memory+0x600000,1,sizeof memory-0x600000,file)>0);
    assert(feof(file));fclose(file);
    MEM32(0x1e140)=0x000df751;MEM32(0x1e240)=0xdeadc0de;
    MEM32(CAMERA+0x1ea4)=5;
    memset((void *)XBOX_PTR(CAMERA+0x1e54),0,5*16);
    MEMF(CAMERA+0x1e54)=-unit;MEMF(CAMERA+0x1e54+8)=-unit;
    MEMF(CAMERA+0x1e64)=unit;MEMF(CAMERA+0x1e64+8)=-unit;
    MEMF(CAMERA+0x1e74+4)=-unit;MEMF(CAMERA+0x1e74+8)=-unit;
    MEMF(CAMERA+0x1e84+4)=unit;MEMF(CAMERA+0x1e84+8)=-unit;
    MEMF(CAMERA+0x1e94+8)=1;MEMF(CAMERA+0x1e94+12)=-80;
    QueryPerformanceFrequency(&frequency);
    for(cars=80;cars<=320;cars*=4)for(scenario=0;scenario<3;++scenario) {
        benchmark_cars=cars;
        for(i=0;i<cars;++i)
            pose(benchmark_graphs[i]+0x3c,0,0,((float)(i%8)-3.5f)*.3f,.7f,depths[scenario]);
        for(i=0;i<50;++i)benchmark_frame();
        frames=32;
        do {
            frames*=2;QueryPerformanceCounter(&start);
            for(i=0;i<frames;++i)benchmark_frame();
            QueryPerformanceCounter(&finish);
            elapsed=(double)(finish.QuadPart-start.QuadPart)/frequency.QuadPart;
        }while(elapsed<.08 && frames<8192);
        for(sample=0;sample<7;++sample) {
            QueryPerformanceCounter(&start);
            for(i=0;i<frames;++i)benchmark_frame();
            QueryPerformanceCounter(&finish);
            samples[sample]=(double)(finish.QuadPart-start.QuadPart)*1000/frequency.QuadPart/frames;
        }
        queued=MEM32(QUEUE+0x5810);assert(queued<=256 && !flares);
        qsort(samples,7,sizeof samples[0],benchmark_compare);
        printf("BENCH /O2 hierarchy+emission cars=%u mode=%s ms/frame median=%.4f min=%.4f max=%.4f "
               "range/median=%.1f%% frames/sample=%u samples=7 queue=%u/256 groups=%u\n",
               cars,names[scenario],samples[3],samples[0],samples[6],
               (samples[6]-samples[0])*100/samples[3],frames,queued,s_car_groups);
        fflush(stdout);
    }
}
'''


def generated():
    found = {}
    for path in sorted((ROOT / "src/recomp/gen").glob("recomp_*.c")):
        source = path.read_text()
        for name in NATIVE:
            match = re.search(r"^void " + name + r"\(void\)\n\{.*?^\}", source, re.M | re.S)
            if match:
                assert name not in found, name
                found[name] = match.group()
    assert set(found) == set(NATIVE), set(NATIVE) - set(found)
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--benchmark", action="store_true",
                        help="also time actual retail hierarchy traversal plus emission in /O2")
    args = parser.parse_args()
    cmake = ROOT / "tools/venv/Lib/site-packages/cmake/data/bin/cmake.exe"
    if not cmake.is_file():
        cmake = shutil.which("cmake")
    if not shutil.which("cl") or not cmake:
        raise SystemExit("Run scripts/enter-env.ps1 first so cl.exe and CMake are available.")
    audit_ancestors()
    native = generated()
    with tempfile.TemporaryDirectory(prefix="mm3-car-light-visibility-") as temporary:
        folder = Path(temporary)
        seam = folder / "seam.cmake"
        seam.write_text(f'include("{(ROOT / "scripts/mm3_car_light_culling_seam.cmake").as_posix()}")\n'
                        'file(READ "${INPUT}" content)\n'
                        'mm3_add_car_light_culling_seam(content "${OWNER}")\n'
                        'file(WRITE "${OUTPUT}" "${content}")\n')
        for owner in SEAMS:
            original, modified = folder / "original.c", folder / "modified.c"
            original.write_text(native[owner])
            subprocess.run([str(cmake), f"-DINPUT={original.as_posix()}", f"-DOUTPUT={modified.as_posix()}",
                            f"-DOWNER={owner}", "-P", str(seam)], check=True)
            native[owner] = modified.read_text()
        for owner in ("sub_000CF2DA", "sub_0022FDFD"):
            native[owner] = native[owner].replace("void " + owner + "(void)",
                                                   "void " + owner + "_original(void)", 1)
        body = "\n".join(native.values())
        calls = set(re.findall(r"RECOMP_ABI_CALL\(0x[0-9A-Fa-f]+u, (sub_[0-9A-Fa-f]+)\)", body))
        defined = set(NATIVE) | set(projector.UNUSED_FLARES) | {"sub_000DFEF9"}
        unused = "\n".join(f'void {name}(void) {{ assert(!"unused visibility boundary {name}"); }}'
                           for name in calls - defined)
        names = set(NATIVE) | calls | {"sub_000CF2DA_original", "sub_0022FDFD_original"}
        declarations = "\n".join("void " + name + "(void);" for name in sorted(names))
        (folder / "native.inc").write_text(declarations + "\n" + unused + "\n" + body)
        (folder / "constants.inc").write_text(projector.constants(body))
        shutil.copyfile(ROOT / "src/mm3_recomp_types.h", folder / "recomp_types.h")
        source = projector.SOURCE.split("static unsigned submit", 1)[0]
        source = source.replace("memory[0x400000]", "memory[0x2000000]" if args.benchmark else "memory[0x500000]")
        source = source.replace("#pragma warning(disable:4101 4102 4127 4189)",
                                "#pragma warning(disable:4101 4102 4127 4189 4702)")
        source = source.replace('void sub_001A842A(void) { assert(!"unexpected flare submission"); }',
                                'static unsigned flares; static uint32_t flare_color,flare_node;\n'
                                'void sub_001A842A(void) { ++flares; flare_color=MEM32(flare_node+0x64); g_esp+=16; }')
        source = source.replace('#pragma warning(push)',
                                'static void fixture_icall(uint32_t);\n'
                                '#undef RECOMP_ICALL_SAFE_AT\n'
                                '#define RECOMP_ICALL_SAFE_AT(address,saved,site) fixture_icall(address)\n'
                                '#pragma warning(push)')
        fixture = car_sources.CHECKS.split("static void direction_checks", 1)[0]
        checks, arguments = CHECKS, []
        if args.benchmark:
            arguments = [str(benchmark_image(folder))]
            checks = checks.replace("int main(void)", BENCHMARK + "\nint main(int argc,char **argv)")
            checks = checks.replace("    return 0;\n}", "    assert(argc==2);benchmark(argv[1]);\n    return 0;\n}")
        (folder / "contract.c").write_text(source + fixture + checks)
        exe = folder / "contract.exe"
        subprocess.run(["cl", "/nologo", "/W4", "/WX", "/O2" if args.benchmark else "/Od", "/std:c11",
                        "/I" + str(folder), "/I" + str(ROOT / "src"),
                        "/I" + str(ROOT / "tools/xboxrecomp/include"),
                        "/Fo:" + str(folder / "contract.obj"), "/Fe:" + str(exe),
                        str(folder / "contract.c")], check=True, cwd=folder)
        subprocess.run([str(exe), *arguments], check=True, cwd=folder)


if __name__ == "__main__":
    main()
