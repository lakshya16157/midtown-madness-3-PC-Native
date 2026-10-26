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
QUEUE_SEAMS = ("sub_0022FF8B", "sub_0022FFD1", "sub_0023006E")
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
static unsigned char memory[0x410000]; /* through time of day at 0x407C30 */
enum { SOURCE=0x10000, MANAGER=0x11000, MATRIX=0x13000, POSITION=0x14000,
       DIRECTION=0x15000, FLAG=0x16000, QUEUE=0x20000, RECORD=0x30000,
       MINIMUM=0x40000, MAXIMUM=0x41000, RETAIL_MINIMUM=0x42000,
       RETAIL_MAXIMUM=0x43000, VIEW=0x60000, STACK=0x90000 };
#include "mm3_light_queue.c"
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
    s_lamp_height=s_lamp_drop=0;
    s_lamp_reach=LAMP_ADDED_MAX_REACH;s_lamp_frame_queue=s_lamp_frame_stamp=s_lamp_frame_count=0;
    s_lamp_frame_base=s_lamp_frame_capped=s_lamp_frame_seen=0;memset(s_lamp_distance,0,sizeof s_lamp_distance);
    s_queue=s_slots=s_allocation=0;s_capacity=MM3_LIGHT_QUEUE_RETAIL;
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

/* A live 90-degree view looking down +Z from eye: four side planes through
 * the eye with outward normals, and a near plane 1m ahead. */
static void view(const float eye[3])
{
    static const float n[5][3]={{-.70710678f,0,-.70710678f},{.70710678f,0,-.70710678f},
                                {0,-.70710678f,-.70710678f},{0,.70710678f,-.70710678f},{0,0,-1}};
    unsigned i;
    MEM32(0x3c5588)=VIEW;MEM32(VIEW+0x1ea4)=5;
    /* World-to-camera rows (identity rotation, translation -eye) and the
     * camera position native F3F93 derives from them; the side planes below
     * pass through it. */
    for(i=0;i!=3;++i) {
        unsigned j;
        for(j=0;j!=3;++j) MEMF(VIEW+0x1f24+i*16+j*4)=i==j?1.0f:0.0f;
        MEMF(VIEW+0x1f54+i*4)=-eye[i];MEMF(VIEW+0x1ea8+i*4)=eye[i];
    }
    for(i=0;i!=5;++i) {
        float d=-(n[i][0]*eye[0]+n[i][1]*eye[1]+n[i][2]*eye[2]);
        if(i==4) d-=1;
        MEMF(VIEW+0x1e54+i*16)=n[i][0];MEMF(VIEW+0x1e58+i*16)=n[i][1];
        MEMF(VIEW+0x1e5c+i*16)=n[i][2];MEMF(VIEW+0x1e60+i*16)=d;
    }
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
    lamp_set_mount(POSITION,DIRECTION); /* a street lamp: 5m post, lighting down */
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
    /* Different nearly-neutral authored shades cross the warmth blend smoothly
     * (from 0xFFFF2B up, the yellowest shade that still counts as lamp light;
     * a fixture's class comes from its authored colour, so its fades stay
     * smooth). */
    MEM32(SOURCE+0x2b)=0xffffff2au;assert(!lamp_is_street(SOURCE));
    MEM32(SOURCE+0x2b)=0xffffff2bu;assert(lamp_is_street(SOURCE));
    previous=lamp_source_color(SOURCE,0xffffff2bu);
    for(level=0x2c;level<256;level++) {
        MEM32(SOURCE+0x2b)=0xffffff00u|level;
        uint32_t output=lamp_source_color(SOURCE,0xffffff00u|level);
        for(shift=0;shift<=16;shift+=8)
            assert(abs((int)channel(output,shift)-(int)channel(previous,shift))<=2);
        previous=output;
    }
    /* Saturated authored hues receive only uniform dimming, including alpha:
     * signs and neon at retail strength through the 1.3 composite gain, the
     * warm lamp shade at the street-lamp 0.85. */
    for(i=0;i<sizeof colored/sizeof colored[0];i++) {
        MEM32(SOURCE+0x2b)=colored[i];
        assert(lamp_source_color(SOURCE,colored[i])==
               lamp_color_gain(colored[i],lamp_is_street(SOURCE) ? .85f : LAMP_ARCHITECTURE_GAIN));
        assert(lamp_is_street(SOURCE)==(colored[i]==0xff806040u));
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

/* The PC-sized queue: an enlarged manager gets a 512-record pointer array
 * after the retail object, the seamed native submission fills it past 256
 * records in point-before-cone order, and the lamp limit follows. */
static void queue_extension(void)
{
    enum { BIG=0x200000 };
    unsigned i,j,cones=0;
    uint32_t slots;
    reset();
    s_requested=512;
    assert(mm3_light_queue_alloc_size()==0x5B20+512*4+256*0x54);
    for(i=0;i<256;i++) MEM32(BIG+0x5410+i*4)=BIG+0x10+i*0x54;
    MEM32(0x3c5d8c)=BIG;mm3_light_queue_allocated(BIG);mm3_light_queue_attach();
    assert(mm3_light_queue_capacity(BIG)==512 && mm3_light_queue_capacity(QUEUE)==256);
    slots=mm3_light_queue_slots(BIG);
    assert(slots==BIG+0x5B20 && mm3_light_queue_slots(QUEUE)==QUEUE+0x5410);
    for(i=0;i<512;i++)
        assert(MEM32(slots+i*4)==(i<256 ? BIG+0x10+i*0x54 : BIG+0x5B20+512*4+(i-256)*0x54));
    assert(lamp_added_limit(BIG)==384 && lamp_added_limit(QUEUE)==144);
    /* 600 submissions through native point (21E18) and spot (489A6) entry
     * points: the seamed queue takes exactly 512 distinct records. */
    for(i=0;i<600;i++) {
        g_esp=STACK;g_ebx=SOURCE;MEM32(STACK)=0x12345678;MEM32(STACK+4)=MATRIX;
        MEM32(STACK+8)=0x00404040u+i;MEMF(STACK+12)=3;
        if(i%5==0) { MEMF(STACK+16)=30;sub_000489A6();assert(g_esp==STACK+20); }
        else { sub_00021E18();assert(g_esp==STACK+16); }
        assert(g_fp_top==0);
    }
    assert(MEM32(BIG+0x5810)==512);
    for(i=0;i<512;i++) {
        uint32_t record=MEM32(slots+i*4);
        assert(record);
        if(MEM32(record+0x50)) cones++;
        else assert(!cones);                    /* points before cones */
        for(j=0;j<i;j+=37) assert(MEM32(slots+j*4)!=record);
    }
    assert(cones>90 && cones<120);
    /* Another manager, or a failed larger allocation, keeps the retail layout. */
    mm3_light_queue_allocated(BIG+0x100000);MEM32(0x3c5d8c)=BIG+0x180000;
    s_queue=0;s_capacity=256;mm3_light_queue_attach();
    assert(mm3_light_queue_capacity(BIG+0x180000)==256 &&
           mm3_light_queue_slots(BIG+0x180000)==BIG+0x180000+0x5410);
    mm3_light_queue_allocated(0);MEM32(0x3c5d8c)=0;mm3_light_queue_attach();
    assert(mm3_light_queue_capacity(0)==256);
    s_requested=256;assert(mm3_light_queue_alloc_size()==0x5B20);
    s_requested=0;s_queue=0;s_slots=0;s_capacity=256;s_allocation=0;
    printf("queue extension: 512 records after the retail manager, native submission past 256 in order, retail fallback\n");fflush(stdout);
}

/* Lamp pools follow a per-frame distance reach so a crowded queue keeps the
 * nearest lamps steady instead of first-come pools that flicker. */
static unsigned reach_frame(unsigned base,unsigned first,unsigned last,unsigned stamp,
                            unsigned *capped,float *farthest)
{
    unsigned d,lit=0;
    MEM32(QUEUE+0x5acc)=stamp;MEM32(QUEUE+0x5810)=base;*capped=0;*farthest=0;
    for(d=first;d<=last;d++) {
        unsigned before=s_lamp_added_capped;
        MEMF(POSITION)=0;MEMF(POSITION+4)=5;MEMF(POSITION+8)=(float)d;
        assert(!mm3_lamp_density_skip(1,MATRIX,POSITION,DIRECTION,SOURCE));
        if(s_lamp_added_capped!=before) (*capped)++;
        if(!s_lamp_glow_only) {
            lit++;MEM32(QUEUE+0x5810)++;
            if((float)d>*farthest) *farthest=(float)d;
            if(d<=15) assert(s_lamp_added_fade==1.0f);
        }
    }
    return lit;
}

static void adaptive_reach(void)
{
    static const float eye[3]={0,5,0};
    unsigned frame,capped,lit;
    float farthest,crowded;
    reset();view(eye);
    /* 100 records queued before 80 added lamps at 10..89m: 44 fit under 144. */
    lit=reach_frame(100,10,89,0,&capped,&farthest);
    assert(lit==44 && capped>0);
    lit=reach_frame(100,10,89,1,&capped,&farthest);
    assert(!capped && farthest<s_lamp_reach+0.001f && s_lamp_reach>=40 && s_lamp_reach<=48);
    for(frame=2;frame<40;frame++) {
        lit=reach_frame(100,10,89,frame&3,&capped,&farthest);
        assert(!capped && lit>=30 && lit<=44 && s_lamp_reach<=48.001f);
    }
    crowded=s_lamp_reach;
    /* A quiet street: the reach grows back half a metre a frame to 100m. */
    for(frame=40;frame<200;frame++) reach_frame(100,10,14,frame&3,&capped,&farthest);
    assert(fabsf(s_lamp_reach-100.0f)<.001f);
    printf("adaptive reach: crowded frames settle at %.1fm without hitting the limit, quiet ones return to 100m\n",
           (double)crowded);fflush(stdout);
}

/* Lamp pools reach the ground below their mounting height (locator Y). */
static void mount_reach(void)
{
    /* A warm 2m lamp on a 4m post: 2.9m tuned -> 7m (1.75 x 4m). */
    reset();MEMF(POSITION+4)=4;MEMF(SOURCE+0x4f)=2;MEM32(SOURCE+0x2b)=0x00ded57d;
    assert(produce()==1 && MEM32(RECORD+0x50)==0);
    assert(fabsf(MEMF(RECORD+0x40)-14.0f)<.01f);
    /* Its bounds use the same reach around the locator. */
    run_bounds(sub_000E10A5,MINIMUM,MAXIMUM);
    assert(fabsf(MEMF(MINIMUM)-5.0f)<.01f && fabsf(MEMF(MAXIMUM)-19.0f)<.01f);
    /* Sodium amber and white lamps count; neon and signal hues are not street
     * lamps and keep the retail 2m. */
    reset();MEMF(POSITION+4)=4;MEMF(SOURCE+0x4f)=2;MEM32(SOURCE+0x2b)=0x00ffaf48;
    assert(produce()==1 && fabsf(MEMF(RECORD+0x40)-14.0f)<.01f);
    reset();MEMF(POSITION+4)=4;MEMF(SOURCE+0x4f)=2;MEM32(SOURCE+0x2b)=0x0064ebff;
    assert(produce()==1 && fabsf(MEMF(RECORD+0x40)-4.0f)<.01f);
    reset();MEMF(POSITION+4)=4;MEMF(SOURCE+0x4f)=2;MEM32(SOURCE+0x2b)=0x00ff6464;
    assert(produce()==1 && fabsf(MEMF(RECORD+0x40)-4.0f)<.01f);
    reset();MEMF(POSITION+4)=4;MEMF(SOURCE+0x4f)=2;MEM32(SOURCE+0x2b)=0x00ebe100;
    assert(produce()==1 && fabsf(MEMF(RECORD+0x40)-4.0f)<.01f);
    /* Ground-level (monument floodlights, bollards) and implausible mounts
     * are not street lamps: the retail reach. */
    reset();MEMF(POSITION+4)=.5f;MEMF(SOURCE+0x4f)=2;MEM32(SOURCE+0x2b)=0x00ded57d;
    assert(produce()==1 && fabsf(MEMF(RECORD+0x40)-4.0f)<.01f);
    reset();MEMF(POSITION+4)=30;MEMF(SOURCE+0x4f)=2;MEM32(SOURCE+0x2b)=0x00ded57d;
    assert(produce()==1 && fabsf(MEMF(RECORD+0x40)-4.0f)<.01f);
    /* A tall lamp that already reaches the road is unchanged (13.05m). */
    reset();MEMF(POSITION+4)=6;MEMF(SOURCE+0x4f)=9;MEM32(SOURCE+0x2b)=0x00ffd57d;
    assert(produce()==1 && fabsf(MEMF(RECORD+0x40)-26.1f)<.01f);
    /* A small downward spot reaches 1.5x its height along the axis; the
     * same spot aimed sideways (a facade floodlight) keeps the retail 3m. */
    reset();MEMF(POSITION+4)=4;MEMF(SOURCE+0x4f)=3;MEM8(SOURCE+0x53)=226;MEM32(SOURCE+0x2b)=0x00ffffff;
    assert(produce()==1 && MEM32(RECORD+0x50)==1 && fabsf(MEMF(RECORD+0x48)-6.0f)<.01f);
    reset();MEMF(POSITION+4)=4;MEMF(SOURCE+0x4f)=3;MEM8(SOURCE+0x53)=226;MEM32(SOURCE+0x2b)=0x00ffffff;
    MEMF(DIRECTION)=1;MEMF(DIRECTION+4)=0;
    assert(produce()==1 && fabsf(MEMF(RECORD+0x48)-3.0f)<.01f);
    /* The authored 7m street spot on a 5m post is unchanged (10.15m). */
    reset();MEMF(SOURCE+0x4f)=7;MEM8(SOURCE+0x53)=169;
    assert(produce()==1 && fabsf(MEMF(RECORD+0x48)-10.15f)<.001f);
    /* Evening still shrinks the grown pool, but not short of the road: the
     * 7m mounted reach keeps 0.8 of itself (1.4x the 4m post). */
    reset();MEMF(POSITION+4)=4;MEMF(SOURCE+0x4f)=2;MEM32(SOURCE+0x2b)=0x00ded57d;
    lamp_set_mount(POSITION,DIRECTION);MEM32(0x407c30)=2;
    assert(fabsf(mm3_lamp_source_range(SOURCE)-7.0f*LAMP_EVENING_MOUNT)<.01f);
    assert(mm3_lamp_source_range(SOURCE)<7.0f);
    MEM32(0x407c30)=0;assert(fabsf(mm3_lamp_source_range(SOURCE)-7.0f)<.01f);
    /* A 9m lamp (10m authored, 15.75m mounted): the evening pool used to end
     * 0.45m below the bulb's height (9.45m); now 12.6m, the road at 71%. */
    reset();MEMF(POSITION+4)=9;MEMF(SOURCE+0x4f)=10;MEM32(SOURCE+0x2b)=0x00ffaf48;
    lamp_set_mount(POSITION,DIRECTION);
    assert(fabsf(mm3_lamp_source_range(SOURCE)-15.75f)<.01f);
    MEM32(0x407c30)=2;assert(fabsf(mm3_lamp_source_range(SOURCE)-12.6f)<.01f);
    MEM32(0x407c30)=3;assert(fabsf(mm3_lamp_source_range(SOURCE)-15.75f)<.01f);
    /* A long-reach lamp keeps the plain evening shrink: 17.4m -> 10.44m. */
    reset();MEMF(POSITION+4)=2;MEMF(SOURCE+0x4f)=12;MEM32(SOURCE+0x2b)=0x00ded57d;
    lamp_set_mount(POSITION,DIRECTION);MEM32(0x407c30)=2;
    assert(fabsf(mm3_lamp_source_range(SOURCE)-17.4f*MM3_EVENING_LIGHT_REACH)<.01f);
    MEM32(0x407c30)=0;
    printf("mount reach: small lamps reach the road, signs/tall lamps/sideways spots unchanged, evening pools still reach the road\n");fflush(stdout);
}

/* Monuments: a white floodlight at the base of a monument aimed up its wall
 * (one-in-two density) is not a street lamp. The skipped one stays dark as
 * on the Xbox (no pool, no glow); the lit one keeps the retail 20m throw,
 * cone and colour at retail strength. Street lamps beside it still all light. */
static void architecture(void)
{
    uint32_t color;
    reset();MEMF(POSITION+4)=.3f;MEMF(DIRECTION+4)=1;
    MEMF(SOURCE+0x4f)=20;MEM8(SOURCE+0x53)=60;MEM32(SOURCE+0x2b)=0x00ffffff;
    MEM8(SOURCE+8)=2;MEM32(STACK+32)=5;
    assert(produce()==0 && !s_lamp_glow_only);
    reset();MEMF(POSITION+4)=.3f;MEMF(DIRECTION+4)=1;
    MEMF(SOURCE+0x4f)=20;MEM8(SOURCE+0x53)=60;MEM32(SOURCE+0x2b)=0x00ffffff;
    MEM8(SOURCE+8)=2;MEM32(STACK+32)=4;
    assert(produce()==1 && MEM32(RECORD+0x50)==1 && MEMF(RECORD+0x48)==20.0f);
    color=MEM32(RECORD+0x4c);
    assert(channel(color,16)==channel(color,8) && channel(color,8)==channel(color,0));
    assert(fabsf(mm3_lamp_source_cone(SOURCE)-60.0f*90.0f/255.0f)<.001f);
    /* The same fixture on a 6m post aimed up is still a floodlight. */
    reset();MEMF(POSITION+4)=6;MEMF(DIRECTION+4)=1;
    MEMF(SOURCE+0x4f)=20;MEM8(SOURCE+0x53)=60;MEM32(SOURCE+0x2b)=0x00ffffff;
    MEM8(SOURCE+8)=2;MEM32(STACK+32)=5;
    assert(produce()==0);
    /* A 40m floodlight on a 10m mast aimed steeply down is still a floodlight. */
    reset();MEMF(POSITION+4)=10;
    MEMF(SOURCE+0x4f)=40;MEM8(SOURCE+0x53)=125;MEM32(SOURCE+0x2b)=0x00805a3c;
    MEM8(SOURCE+8)=2;MEM32(STACK+32)=5;
    assert(produce()==0);
    reset();MEMF(POSITION+4)=10;
    MEMF(SOURCE+0x4f)=40;MEM8(SOURCE+0x53)=125;MEM32(SOURCE+0x2b)=0x00805a3c;
    MEM8(SOURCE+8)=2;MEM32(STACK+32)=4;
    assert(produce()==1 && MEMF(RECORD+0x48)==40.0f);
    /* Red warning lights (one in two) stay one in two. */
    reset();MEMF(SOURCE+0x4f)=8;MEM32(SOURCE+0x2b)=0x00b4001e;MEM8(SOURCE+8)=2;MEM32(STACK+32)=5;
    assert(produce()==0);
    /* A bulb with only a glow, on a monument's wall at 1.5m: no new pool. */
    reset();MEMF(POSITION+4)=1.5f;assert(produce()==0);
    /* A street lamp beside them: every one lights, pool included. */
    reset();MEM8(SOURCE+8)=2;MEM32(STACK+32)=5;assert(produce()==1 && !s_lamp_glow_only);
    printf("architecture: monument floodlights and wall bulbs keep the retail density, reach, cone and colour\n");fflush(stdout);
}

/* Tunnel, wall and low road lamps: thinned one in two like street lamps but
 * not street lamps by their mount. The skipped one now lights at the retail
 * reach, cone and colour (also in the evening); pink signs, sideways spots,
 * dim floods and long throws stay one in two. */
static void restored(void)
{
    uint32_t color;
    unsigned before;
    /* A white 8m tunnel ceiling light: its object's origin is above the
     * road, so its height reads 0 (here 20m). */
    reset();MEMF(POSITION+4)=20;MEMF(SOURCE+0x4f)=8;MEM32(SOURCE+0x2b)=0x00ffffff;
    MEM8(SOURCE+8)=2;MEM32(STACK+32)=5;before=s_lamp_restored_lit;
    assert(produce()==1 && !s_lamp_glow_only && MEM32(RECORD+0x50)==0);
    assert(s_lamp_restored_lit==before+1);
    assert(fabsf(MEMF(RECORD+0x40)-16.0f)<.01f);
    color=MEM32(RECORD+0x4c);
    assert(channel(color,16)==channel(color,8) && channel(color,8)==channel(color,0));
    assert(!lamp_is_street(SOURCE));
    MEM32(0x407c30)=2;assert(mm3_lamp_source_range(SOURCE)==8.0f);MEM32(0x407c30)=0;
    /* The same at ground level (a road light under 2m) and a cool-white
     * fluorescent; a downward 7m spot under a bridge. */
    reset();MEMF(POSITION+4)=.6f;MEMF(SOURCE+0x4f)=8;MEM32(SOURCE+0x2b)=0x00d2e6f5;
    MEM8(SOURCE+8)=2;MEM32(STACK+32)=5;assert(produce()==1 && !s_lamp_glow_only);
    reset();MEMF(POSITION+4)=0;MEMF(SOURCE+0x4f)=7;MEM8(SOURCE+0x53)=169;MEM32(SOURCE+0x2b)=0x00ded57d;
    MEM8(SOURCE+8)=2;MEM32(STACK+32)=5;assert(produce()==1 && MEM32(RECORD+0x50)==1);
    assert(MEMF(RECORD+0x48)==7.0f);
    /* Far away it keeps only its glow, like an added street lamp. */
    {
        static const float far_eye[3]={12,5,-113};
        reset();view(far_eye);MEMF(POSITION+4)=20;MEMF(SOURCE+0x4f)=8;MEM32(SOURCE+0x2b)=0x00ffffff;
        MEM8(SOURCE+8)=2;MEM32(STACK+32)=5;before=s_lamp_restored_glow;
        assert(produce()==0 && s_lamp_glow_only && s_lamp_restored_glow==before+1);
    }
    /* Still one in two: Paris's pink signs, the same spot aimed sideways, a
     * dim monument flood and a 20m throw. */
    reset();MEMF(POSITION+4)=20;MEMF(SOURCE+0x4f)=4;MEM32(SOURCE+0x2b)=0x00ff96dc;
    MEM8(SOURCE+8)=2;MEM32(STACK+32)=5;assert(produce()==0 && !s_lamp_glow_only);
    reset();MEMF(POSITION+4)=0;MEMF(SOURCE+0x4f)=7;MEM8(SOURCE+0x53)=169;MEM32(SOURCE+0x2b)=0x00ded57d;
    MEMF(DIRECTION)=1;MEMF(DIRECTION+4)=0;
    MEM8(SOURCE+8)=2;MEM32(STACK+32)=5;assert(produce()==0);
    reset();MEMF(POSITION+4)=20;MEM32(SOURCE+0x2b)=0x00805a3c;
    MEM8(SOURCE+8)=2;MEM32(STACK+32)=5;assert(produce()==0);
    reset();MEMF(POSITION+4)=20;MEMF(SOURCE+0x4f)=20;MEM32(SOURCE+0x2b)=0x00ffffff;
    MEM8(SOURCE+8)=2;MEM32(STACK+32)=5;assert(produce()==0);
    /* A tunnel uplighter (a lamp-coloured 7m spot aimed up the wall) keeps its
     * spot and also lights a pool of its hue down to the road: 1.75x its 5m
     * mounting, at a 0x48 peak. Aimed down, sideways or coloured: no pool. */
    reset();MEMF(SOURCE+0x4f)=7;MEM8(SOURCE+0x53)=169;MEM32(SOURCE+0x2b)=0x00ffca85;
    MEMF(DIRECTION+4)=1;MEM32(QUEUE+0x5414)=RECORD+0x100;MEM32(0x407c30)=3;
    assert(produce()==2 && MEM32(RECORD+0x50)==1 && MEM32(RECORD+0x150)==0);
    assert(fabsf(MEMF(RECORD+0x140)-17.5f)<.01f && (MEM32(RECORD+0x14c)&0xFFFFFFu)==0x00483926u);
    reset();MEMF(SOURCE+0x4f)=7;MEM8(SOURCE+0x53)=169;MEM32(SOURCE+0x2b)=0x00ffca85;
    MEMF(DIRECTION)=1;MEMF(DIRECTION+4)=0;MEM32(QUEUE+0x5414)=RECORD+0x100;assert(produce()==1);
    reset();MEMF(SOURCE+0x4f)=7;MEM8(SOURCE+0x53)=169;MEM32(SOURCE+0x2b)=0x000000ff;
    MEMF(DIRECTION+4)=1;MEM32(QUEUE+0x5414)=RECORD+0x100;assert(produce()==1);
    MEM32(0x407c30)=0;
    printf("restored: thinned tunnel/wall/road lamps light at retail strength; signs, sideways spots, floods stay thinned; uplighters pool the road\n");fflush(stdout);
}

int main(void)
{
    unsigned i,lights=0,fallbacks=0,dark=0,warm=0,colored=0,floodlights=0;
    float tiny,large;
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    /* The default locator is 5m up: the 5.65m bulb pool grows to 1.75x that
     * height so it reaches the ground. A glow-only bulb at ground level is not
     * a street lamp and, as on the Xbox, casts no pool. */
    reset();assert(produce()==1);assert(MEM32(RECORD+0x50)==0);
    assert(fabsf(MEMF(RECORD+0x40)-17.5f)<.01f);
    reset();MEMF(POSITION+4)=.5f;assert(produce()==0);
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
    {
        /* One-in-N fixtures (byte 8): a hash multiple lights natively and
         * the others now light too, pool included, along the same path. */
        unsigned lit;
        reset();MEM8(SOURCE+8)=2;MEM32(STACK+32)=4;assert(produce()==1);lit=g_eax&255u;
        assert(!s_lamp_glow_only);
        reset();MEM8(SOURCE+8)=2;MEM32(STACK+32)=5;assert(produce()==1);
        assert((g_eax&255u)==lit && !s_lamp_glow_only);
        reset();MEM8(SOURCE+8)=3;MEM32(STACK+32)=7;assert(produce()==1);
        /* Once 144 records are queued, added fixtures keep only their glow
         * (112 records stay for vehicles); authored ones still queue. */
        reset();MEM8(SOURCE+8)=2;MEM32(STACK+32)=5;MEM32(QUEUE+0x5810)=144;
        assert(produce()==144 && s_lamp_glow_only);
        reset();MEM8(SOURCE+8)=2;MEM32(STACK+32)=5;MEM32(QUEUE+0x5810)=143;
        MEM32(QUEUE+0x5410+143*4)=RECORD;assert(produce()==144 && !s_lamp_glow_only);
        /* The next fixture is unaffected by the previous glow-only one. */
        reset();MEM8(SOURCE+8)=2;MEM32(STACK+32)=5;MEM32(QUEUE+0x5810)=144;
        assert(produce()==144 && s_lamp_glow_only);
        reset();MEM8(SOURCE+8)=2;MEM32(STACK+32)=4;MEM32(QUEUE+0x5810)=144;
        MEM32(QUEUE+0x5410+144*4)=RECORD;assert(s_lamp_glow_only);
        assert(produce()==145 && !s_lamp_glow_only);
        {
            /* With a trusted camera, added pools follow distance: full up to
             * 70m, eased (same tint, lower energy) to 100m, glow only beyond.
             * Authored fixtures keep their pool at every distance. */
            static const float near_eye[3]={12,5,-53},mid_eye[3]={12,5,-88},far_eye[3]={12,5,-113};
            uint32_t full,eased;
            reset();view(near_eye);MEM8(SOURCE+8)=2;MEM32(STACK+32)=5;
            assert(produce()==1 && !s_lamp_glow_only && s_lamp_added_fade==1.0f);
            full=MEM32(RECORD+0x4c);
            reset();view(mid_eye);MEM8(SOURCE+8)=2;MEM32(STACK+32)=5;
            assert(produce()==1 && !s_lamp_glow_only && s_lamp_added_fade>0 && s_lamp_added_fade<1);
            eased=MEM32(RECORD+0x4c);
            assert(channel(eased,16)<channel(full,16) && channel(eased,0)<channel(full,0));
            reset();view(far_eye);MEM8(SOURCE+8)=2;MEM32(STACK+32)=5;
            assert(produce()==0 && s_lamp_glow_only);
            reset();view(far_eye);MEM8(SOURCE+8)=2;MEM32(STACK+32)=4;
            assert(produce()==1 && !s_lamp_glow_only && MEM32(RECORD+0x4c)==full);
            /* The fixture's object matrix places it: moved 60m nearer, lit. */
            reset();view(far_eye);MEM8(SOURCE+8)=2;MEM32(STACK+32)=5;MEMF(MATRIX+0x2c)=-60;
            assert(produce()==1 && s_lamp_added_fade==1.0f);
            /* An untrusted eye (off its planes) keeps the full pool. */
            reset();view(far_eye);MEMF(VIEW+0x1ea8)+=7;MEM8(SOURCE+8)=2;MEM32(STACK+32)=5;
            assert(produce()==1 && s_lamp_added_fade==1.0f);
        }
        /* Retail's own denser streaming mode (N/4) still lights it fully. */
        reset();MEM8(SOURCE+8)=2;MEM32(STACK+32)=5;MEM8(MANAGER+0x15f5)=1;
        assert(produce()==1 && !s_lamp_glow_only);
    }
    reset();MEM32(QUEUE+0x5810)=256;assert(produce()==256);
    /* Glow-only bulbs: on a 2m post a tiny dim one (0.75m from its flare)
     * still reaches the ground (3.5m), a large bright one keeps 8m; the same
     * bulbs without a street mount (walls, monuments) cast no pool. */
    reset();MEMF(POSITION+4)=2;lamp_set_mount(POSITION,DIRECTION);
    MEMF(SOURCE+0x49)=.1f;MEM8(SOURCE+0x4d)=25;tiny=mm3_lamp_source_range(SOURCE);
    MEMF(SOURCE+0x49)=.5f;MEM8(SOURCE+0x4d)=255;large=mm3_lamp_source_range(SOURCE);
    assert(fabsf(tiny-3.5f)<.001f && large>7.9f && large<8.1f);
    s_lamp_height=0;assert(mm3_lamp_source_range(SOURCE)==0.0f);
    lamp_set_mount(POSITION,DIRECTION);
    /* A 50m throw is a floodlight: retail reach; a 12m lamp gains 5.4m. */
    MEMF(SOURCE+0x4f)=50;assert(mm3_lamp_source_range(SOURCE)==50);
    MEMF(SOURCE+0x4f)=12;assert(fabsf(mm3_lamp_source_range(SOURCE)-17.4f)<.001f);
    /* Evening (time 2) keeps every fixture on with a shorter, softer pool;
     * morning, noon and night keep the full reach and color. */
    MEM32(0x407c30)=2;
    assert(fabsf(mm3_lamp_source_range(SOURCE)-17.4f*MM3_EVENING_LIGHT_REACH)<.001f);
    MEMF(SOURCE+0x4f)=50;assert(mm3_lamp_source_range(SOURCE)==50*MM3_EVENING_LIGHT_REACH);
    MEMF(SOURCE+0x4f)=0;assert(mm3_lamp_source_range(SOURCE)==large*MM3_EVENING_LIGHT_REACH);
    MEMF(SOURCE+0x4f)=7;MEM32(SOURCE+0x2b)=0x00ffffffu;
    assert(channel(lamp_source_color(SOURCE,0x00ffffffu),16)<channel(0x00d9b282u,16));
    MEM32(0x407c30)=3;assert(lamp_source_color(SOURCE,0x00ffffffu)==0x00d9b282u);
    MEM32(0x407c30)=0;
    MEMF(SOURCE+0x4f)=NAN;assert(mm3_lamp_source_range(SOURCE)==0);
    mount_reach();
    architecture();
    restored();
    queue_extension();
    adaptive_reach();
    direct_spot(0x0016945a);direct_spot(0x000CF5ED);direct_spot(0x12345678);
    source_colors();
    queued_source_fades();
    /* All authored sources in both cities keep independent reach and tint.
     * Zero-radius bulbs become local sources; non-light effects stay dark. */
    for(i=0;i<sizeof retail/sizeof retail[0];i++) {
        float before,after,size;
        uint32_t original_color,tuned_color;
        unsigned shift,peak=0,minimum=255,street;
        int bulb;
        reset();memcpy(memory+SOURCE,retail[i],0x56);
        before=MEMF(SOURCE+0x4f);size=MEMF(SOURCE+0x49);
        original_color=MEM32(SOURCE+0x2b);
        bulb=before==0 && size>0 && MEM8(SOURCE+0x4d) && (original_color&0xffffff);
        /* At ground level or on a wall (no street mount): exactly retail. */
        after=mm3_lamp_source_range(SOURCE);
        assert(!lamp_is_street(SOURCE) && after==(before>0 ? before : 0.0f));
        if(original_color&0xffffff)
            assert(lamp_source_color(SOURCE,original_color)==lamp_color_gain(original_color,LAMP_ARCHITECTURE_GAIN));
        /* On a 5m post lighting the road. */
        lamp_set_mount(POSITION,DIRECTION);
        street=lamp_is_street(SOURCE);
        after=mm3_lamp_source_range(SOURCE);
        tuned_color=lamp_source_color(SOURCE,original_color);
        assert(!memcmp(memory+SOURCE,retail[i],0x56));
        assert((original_color&0xff000000u)==(tuned_color&0xff000000u));
        for(shift=0;shift<=16;shift+=8) {
            unsigned value=channel(original_color,shift);
            if(value>peak)peak=value;
            if(value<minimum)minimum=value;
            assert(channel(tuned_color,shift)<=value);
        }
        if(street && after>0 && peak) {
            source_fade();
            if(minimum*4>peak*3)warm++;
            else {
                float gain=before==0?.65f*(float)MEM8(SOURCE+0x4d)/255.0f:.85f;
                assert(tuned_color==lamp_color_gain(original_color,gain));
                colored++;
            }
        }
        if(before>0) {
            if(street) assert(after>before && after<=fmaxf(before+6.001f,8.751f));
            else { assert(after==before); ++floodlights; }
            lights++;
        } else if(bulb) {
            if(street) { assert(after>0 && after<=10.001f); fallbacks++; }
            else assert(after==0);
            lights++;
        } else {assert(after==0);dark++;}
    }
    assert(lights==157 && dark==455 && fallbacks>25 && warm>0 && colored>0 && floodlights>0);
    printf("PASS: %u city light descriptors (%u added local bulbs on street posts, %u authored fixtures kept retail as floodlights/signs), %u dark effects; fixed fixture warmth, monotone 256-step authored/queued fades, authored saturated hues, native source gates, source-sized reach, matched bounds, point/spot queues, traffic-signal tint and guest ABI\n",lights,fallbacks,floodlights,dark);
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
        # The production PC-sized light queue seam on the native queue owners.
        queue_script = folder / "queue_seam.cmake"
        queue_script.write_text(f'include("{(ROOT / "scripts/mm3_light_queue_seam.cmake").as_posix()}")\n'
                                'file(READ "${INPUT}" content)\n'
                                'mm3_add_light_queue_seam(content "${OWNER}")\n'
                                'file(WRITE "${OUTPUT}" "${content}")\n')
        for owner in QUEUE_SEAMS:
            original, modified = folder / "original.c", folder / "modified.c"
            original.write_text(native[owner])
            subprocess.run([str(cmake), f"-DINPUT={original.as_posix()}", f"-DOUTPUT={modified.as_posix()}",
                            f"-DOWNER={owner}", "-P", str(queue_script)], check=True)
            native[owner] = modified.read_text().replace("#include <stdint.h>\n", "")
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
