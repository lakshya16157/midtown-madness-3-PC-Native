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
native_projector.NATIVE += ("sub_0022FDFD",)


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
static void native_reset(uint32_t queue)
{
    uint32_t saved_eax=g_eax,saved_ecx=g_ecx,saved_esp=g_esp;
    uint32_t stamp=MEM32(queue+0x5acc),arguments[3];
    uint32_t saved_edx=g_edx,saved_ebx=g_ebx,saved_esi=g_esi,saved_edi=g_edi;
    uint32_t saved_ebp=g_ebp,saved_flags=g_eflags,saved_seh=g_seh_ebp,saved_fs=g_fs_base;
    double saved_fp[8];
    int saved_top=g_fp_top,saved_cmp=g_fp_cmp;
    uint16_t saved_cc=g_fp_cc,saved_control=g_fp_control_word;
    memcpy(saved_fp,g_fp_stack,sizeof saved_fp);
    memcpy(arguments,(const void *)XBOX_PTR(STACK),sizeof arguments);
    g_ecx=queue;g_esp=STACK;sub_0022FDFD();
    assert(MEM32(queue+0x5810)==0 && MEM32(queue+0x5acc)==((stamp+1)&3));
    assert(g_eax==queue+0x5acc && g_ecx==((stamp+1)&3) && g_esp==STACK+12);
    assert(g_edx==saved_edx && g_ebx==saved_ebx && g_esi==saved_esi && g_edi==saved_edi);
    assert(g_ebp==saved_ebp && g_eflags==saved_flags && g_seh_ebp==saved_seh && g_fs_base==saved_fs);
    assert(g_fp_top==saved_top && g_fp_cmp==saved_cmp && g_fp_cc==saved_cc && g_fp_control_word==saved_control);
    assert(!memcmp(saved_fp,g_fp_stack,sizeof saved_fp));
    assert(!memcmp(arguments,(const void *)XBOX_PTR(STACK),sizeof arguments));
    g_eax=saved_eax;g_ecx=saved_ecx;g_esp=saved_esp;
}
static void setup(unsigned kind, uint32_t color, float brightness, unsigned mask, float yaw)
{
    unsigned i;
    memset(memory,0,sizeof memory);g_xbox_mem_offset=(ptrdiff_t)memory;
    g_eax=g_edx=g_ebx=g_esi=g_edi=g_ebp=g_seh_ebp=g_fs_base=g_eflags=0;
    g_ecx=NODE;g_esp=STACK;g_fp_top=g_df=g_fp_cmp=0;
    g_fp_cc=0;g_fp_control_word=0x27f;
    memset(g_fp_stack,0,sizeof g_fp_stack);native_constants();flares=0;flare_color=0;
    MEM32(0x3c55a0)=WORLD;MEM32(WORLD+0x24c)=0x1234;
    MEM32(0x3c5d8c)=QUEUE;native_reset(QUEUE);
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
    uint32_t node=g_ecx,color=MEM32(node+0x64);
    float brightness=MEMF(node+0x54);
    flare_node=node;
    sub_000CF2DA();
    assert(g_esp==STACK+16 && g_fp_top==0);
    assert(!g_ebx && !g_esi && !g_edi);
    assert(MEM32(node+0x64)==color);
    if(MEM32(node+0x34)==0) assert(MEMF(node+0x54)==brightness);
}
static void again(unsigned kind,uint32_t mask)
{
    g_ecx=NODE;g_esp=STACK;MEM32(NODE+0x34)=kind;MEM32(STACK+12)=mask;run();
}
static void lamp(uint32_t node,uint32_t owner,float x)
{
    if(node!=NODE) memcpy((void *)XBOX_PTR(node),(const void *)XBOX_PTR(NODE),0x90);
    MEM32(node+4)=owner;MEMF(MATRIX+0x24)=x;g_ecx=node;g_esp=STACK;run();
}
static uint32_t entry(unsigned i) {return MEM32(QUEUE+0x5410+i*4);}
static void yaw(float angle)
{
    MEMF(MATRIX)=cosf(angle);MEMF(MATRIX+8)=-sinf(angle);
    MEMF(MATRIX+0x18)=sinf(angle);MEMF(MATRIX+0x20)=cosf(angle);
}
static void environment(uint32_t owner)
{
    const uint32_t node=NODE+0x400;
    memcpy((void *)XBOX_PTR(node),(const void *)XBOX_PTR(NODE),0x90);
    MEM32(node+4)=owner;MEM32(node+0x34)=8;MEM32(node+0x64)=0xffffffffu;
    g_ecx=node;g_esp=STACK;MEM32(STACK+12)=0x100;run();
    MEM32(STACK+12)=1;
}
static void direction_checks(void)
{
    unsigned i,j;
    for(i=0;i<8;++i) {
        float yaw=i*3.14159265358979f/4;
        float forward[3]={sinf(yaw),0,cosf(yaw)};
        float source[12], expected[16];
        uint32_t cone,pool;
        setup(8,0xffffffffu,.8f,0x100,yaw);
        memcpy(source,(const void *)XBOX_PTR(MATRIX),sizeof source);
        assert(car_light_matrix(source,NULL,0,expected));
        sub_000CF2DA_original();
        assert(MEM32(QUEUE+0x5810)==1);
        for(j=0;j<3;++j) {
            assert(fabsf(MEMF(RECORD+0x20+j*4)-forward[j])<.0001f);
            assert(fabsf(MEMF(RECORD+0x30+j*4)-source[9+j])<.0001f);
            assert(fabsf(expected[8+j]-forward[j])<.0001f);
        }
        setup(0,0xffffffffu,.8f,1,yaw);run();
        /* A native smoothly attenuated ellipsoid plus one beam, rather than
         * three overlapping light records per car. */
        assert(MEM32(QUEUE+0x5810)==2);
        pool=entry(0);cone=entry(1);
        assert(MEM32(pool+0x50)==0 && MEM32(cone+0x50)==1 && flares==1);
        assert(MEMF(cone+0x24)<0);
        assert(MEMF(cone+0x20)*forward[0]+MEMF(cone+0x28)*forward[2]>.99f);
        assert(MEMF(cone+0x40)==22 && MEMF(cone+0x44)==6 && MEMF(cone+0x48)==50);
        assert(MEMF(pool+0x40)==10 && MEMF(pool+0x44)==4 && MEMF(pool+0x48)==18);
        for(j=0;j<3;++j) {
            assert(fabsf(MEMF(pool+0x30+j*4)-(source[9+j]+5*forward[j]-(j==1?.45f:0)))<.001f);
            assert(fabsf(MEMF(pool+0x20+j*4)-forward[j])<.001f);
        }
        /* Both local records and the native visible lens are warm and dimmer
         * than the old beam/pool/glow, with authoring data restored. */
        for(j=0;j<2;++j) {
            uint32_t rgb=MEM32(entry(j)+0x4c);
            assert(((rgb>>16)&255)>((rgb>>8)&255) && ((rgb>>8)&255)>(rgb&255));
            assert(((rgb>>16)&255)<=(j?67u:26u));
        }
        assert(((flare_color>>16)&255)<=210);
        assert(((flare_color>>16)&255)>((flare_color>>8)&255) && ((flare_color>>8)&255)>(flare_color&255));
        assert(MEM32(NODE+0x64)==0xffffffffu);
    }
}
static void axis_checks(void)
{
    float toe=25*3.14159265358979f/180;
    uint32_t cone,collision=OWNER+(128u<<11);
    float source[12]={0};
    setup(8,0xffffffffu,1,0x100,0);run();assert(!MEM32(QUEUE+0x5810));
    setup(0,0xffffffffu,.8f,1,toe);run();cone=entry(1);
    /* A hidden/reused vehicle must not inherit a previous frame's world
     * heading; models without a current env node use their current lamps. */
    assert(fabsf(MEMF(cone+0x20)-sinf(toe))<.01f);
    setup(8,0xffffffffu,1,0x100,0);run();yaw(toe);again(0,1);cone=entry(1);
    assert(fabsf(MEMF(cone+0x20))<.001f && MEMF(cone+0x28)>.99f);
    /* A nearby different owner cannot borrow this car's steering direction. */
    setup(0,0xffffffffu,.8f,1,toe);MEM32(NODE+4)=OWNER+0x1000;run();
    cone=entry(1);assert(fabsf(MEMF(cone+0x20)-sinf(toe))<.01f);
    setup(0,0xffffffffu,.8f,1,3.14159265358979f);run();
    cone=entry(1);assert(MEMF(cone+0x28)<-.99f);
    /* Open addressing preserves owners with colliding cache hashes. */
    setup(8,0xffffffffu,1,0x100,0);run();
    while(car_axis_slot(collision)!=car_axis_slot(OWNER)) collision+=16;
    source[6]=1;car_axis_remember(collision,source);
    yaw(toe);again(0,1);cone=entry(1);
    assert(fabsf(MEMF(cone+0x20))<.001f && MEMF(cone+0x28)>.99f);
    memset(s_car_axis,0,sizeof s_car_axis);
    /* Traffic models without HeadLightEnv average toed-out lamp axes. */
    setup(0,0xffffffffu,.8f,1,toe);lamp(NODE,OWNER,12.3f);
    MEMF(MATRIX)=cosf(toe);MEMF(MATRIX+8)=sinf(toe);
    MEMF(MATRIX+0x18)=-sinf(toe);MEMF(MATRIX+0x20)=cosf(toe);
    lamp(NODE+0x200,OWNER,13.7f);
    assert(MEM32(QUEUE+0x5810)==2);
    cone=entry(1);assert(fabsf(MEMF(cone+0x20))<.001f && MEMF(cone+0x28)>.99f);
}
static void current_axis_checks(void)
{
    const float toe=.436332313f;
    unsigned pass,i;
    unsigned char saved[2][0x54];
    /* A single visible lens catches the old one-frame lag even when the
     * partner is culled. Env-before-lens and env-after-lens must agree. */
    for(pass=0;pass<2;++pass) {
        const float angle=.9f;
        setup(0,0xffffffffu,.8f,1,angle+toe);
        if(pass) {yaw(angle);environment(OWNER);yaw(angle+toe);}
        lamp(NODE,OWNER,12.3f);
        if(!pass) {yaw(angle);environment(OWNER);}
        assert(MEM32(QUEUE+0x5810)==2);
        assert(fabsf(MEMF(entry(1)+0x20)-sinf(angle))<.001f);
        assert(fabsf(MEMF(entry(1)+0x28)-cosf(angle))<.001f);
        for(i=0;i<2;++i) {
            if(!pass) memcpy(saved[i],(const void *)XBOX_PTR(entry(i)),0x54);
            else assert(!memcmp(saved[i],(const void *)XBOX_PTR(entry(i)),0x54));
        }
        /* Updating an existing field works even when no queue slots remain.
         * Another vehicle and repeated env traversal cannot move it. */
        yaw(angle);MEM32(QUEUE+0x5810)=256;environment(OWNER+0x1000);
        assert(!memcmp(saved[1],(const void *)XBOX_PTR(entry(1)),0x54));
        environment(OWNER);assert(MEM32(QUEUE+0x5810)==256);
        assert(!memcmp(saved[1],(const void *)XBOX_PTR(entry(1)),0x54));
    }
    /* Fast steering used to keep a stale axis and snap at its 60-degree
     * fallback guard. Each frame now resolves the actual current car axis. */
    for(i=0;i<120;++i) {
        float angle=i*.05f;
        setup(0,0xffffffffu,.8f,1,angle+toe);run();
        yaw(angle);environment(OWNER);
        assert(fabsf(MEMF(entry(1)+0x20)-sinf(angle))<.001f);
        assert(fabsf(MEMF(entry(1)+0x28)-cosf(angle))<.001f);
    }
    /* Four skipped native frames alias the modulo-four stamp. Even when all
     * old record images survive and the next queue count equals the old one,
     * the native reset hook must expire cached axes and grouped fields. */
    setup(0,0xffffffffu,.8f,1,0);run();environment(OWNER);
    {
        uint32_t stamp=MEM32(QUEUE+0x5acc);
        for(i=0;i<4;++i) native_reset(QUEUE);
        assert(MEM32(QUEUE+0x5acc)==stamp);
    }
    MEM32(QUEUE+0x5810)=2;
    yaw(toe);again(0,1);
    assert(MEM32(QUEUE+0x5810)==4);
    assert(fabsf(MEMF(entry(3)+0x20)-sinf(toe))<.001f);
    /* An environment-only visit has no images to inspect for a reset. */
    setup(8,0xffffffffu,.8f,0x100,0);run();
    assert(s_car_groups==0 && MEM32(QUEUE+0x5810)==0);
    for(i=0;i<4;++i) native_reset(QUEUE);
    yaw(toe);again(0,1);
    assert(MEM32(QUEUE+0x5810)==2);
    assert(fabsf(MEMF(entry(1)+0x20)-sinf(toe))<.001f);
}
static void reset_lifecycle_checks(void)
{
    const uint32_t other_queue=0x60000;
    unsigned i;
    uint32_t groups,queue,stamp,count;
    unsigned char records[2][0x54],axes[sizeof s_car_axis];
    struct car_light_group group;
    setup(0,0xffffffffu,.8f,1,0);run();environment(OWNER);
    groups=s_car_groups;queue=s_car_queue;stamp=s_car_frame;count=s_car_count;
    group=s_car_group[0];memcpy(axes,s_car_axis,sizeof axes);
    for(i=0;i<2;++i)memcpy(records[i],(const void *)XBOX_PTR(entry(i)),0x54);
    MEM32(other_queue+0x5810)=77;MEM32(other_queue+0x5acc)=3;
    /* A different native queue must keep its own reset and ABI effects while
     * leaving the active scene's source groups, axes and records untouched. */
    g_edx=0x12345678;g_ebx=0x23456789;g_esi=0x3456789a;g_edi=0x456789ab;
    g_ebp=0x56789abc;g_seh_ebp=0x6789abcd;g_fs_base=0x789abcde;g_eflags=0x202;
    native_reset(other_queue);
    assert(s_car_groups==groups && s_car_queue==queue && s_car_frame==stamp && s_car_count==count);
    assert(!memcmp(&group,s_car_group,sizeof group) && !memcmp(axes,s_car_axis,sizeof axes));
    for(i=0;i<2;++i)assert(!memcmp(records[i],(const void *)XBOX_PTR(entry(i)),0x54));
    assert(MEM32(QUEUE+0x5810)==2);
    g_edx=g_ebx=g_esi=g_edi=g_ebp=g_seh_ebp=g_fs_base=g_eflags=0;
    yaw(.436332313f);again(0,1);
    assert(MEM32(QUEUE+0x5810)==2 && fabsf(MEMF(entry(1)+0x20))<.001f);
    /* Resetting a full active queue frees its groups and heading cache before
     * any later lamp callback, without touching its reusable record storage. */
    MEM32(QUEUE+0x5810)=256;native_reset(QUEUE);
    assert(s_car_groups==0 && s_car_queue==QUEUE && s_car_count==0);
    for(i=0;i<CAR_AXIS_SLOTS;++i)assert(s_car_axis[i].owner==0);
    for(i=0;i<2;++i)assert(!memcmp(records[i],(const void *)XBOX_PTR(entry(i)),0x54));
    again(0,1);assert(MEM32(QUEUE+0x5810)==2);
    assert(fabsf(MEMF(entry(1)+0x20)-sinf(.436332313f))<.001f);
}
static void fading_checks(void)
{
    unsigned i,j,previous[2]={0,0};
    float old_strength=0;
    float previous_centre=12.3f;
    /* Exhaust the ramp in small steps. RGB may advance by at most one byte
     * per step, reaches zero continuously, and preserves authored traffic. */
    assert(car_light_head_strength(0)==0 && car_light_head_strength(-1)==0);
    assert(car_light_head_strength(.001f)<.0001f);
    assert(fabsf(car_light_head_strength(.34f)-.868f)<.00001f);
    setup(0,0xffffffffu,.00001f,1,0);
    for(i=0;i<300;++i) lamp(NODE,OWNER+i*0x1000,13);
    assert(MEM32(QUEUE+0x5810)==0 && s_car_groups==0);
    MEMF(NODE+0x54)=MEMF(NODE+0x78)=.8f;lamp(NODE,OWNER,13);
    assert(MEM32(QUEUE+0x5810)==2);
    for(i=1;i<=1000;++i) {
        float brightness=i*.001f,strength=car_light_head_strength(brightness);
        assert(strength>=old_strength && strength-old_strength<.005f);
        old_strength=strength;
        setup(0,0xffffffffu,brightness,1,0);run();
        for(j=0;j<2;++j) {
            unsigned red=(car_light_color(car_light_front_color(0xffffffffu),
                               strength*(j?CAR_BEAM_GAIN:CAR_POOL_GAIN))>>16)&255;
            assert(red>=previous[j] && red-previous[j]<=1);previous[j]=red;
        }
        if(MEM32(QUEUE+0x5810)) {
            for(j=0;j<MEM32(QUEUE+0x5810);++j)
                assert((MEM32(entry(j)+0x4c)&0xffffff)!=0);
        }
    }
    /* A fading partner must relinquish its position/aim continuously before
     * it becomes zero. A plain unweighted average jumps half a car width. */
    for(i=0;i<=300;++i) {
        float brightness=i*.001f,centre;
        setup(0,0xffffffffu,.8f,1,0);lamp(NODE,OWNER,12.3f);
        if(i) {
            MEMF(NODE+0x54)=MEMF(NODE+0x78)=brightness;
            lamp(NODE+0x200,OWNER,13.7f);
        }
        centre=MEMF(entry(1)+0x30);
        /* Packed RGB first becomes visible at one byte; its position enters
         * within 2 cm, rather than the old 70 cm half-car-width jump. */
        assert(centre>=previous_centre-.00001f && centre-previous_centre<.02f);
        previous_centre=centre;
        assert(MEM32(QUEUE+0x5810)==2);
        assert(MEM32(entry(1)+0x4c)==car_light_color(car_light_front_color(0xffffffffu),
                                                  CAR_BEAM_GAIN*car_light_head_strength(.8f)));
    }
}
static void grouping_checks(void)
{
    unsigned pass,i;
    unsigned char saved[2][0x54];
    uint32_t pool,cone;
    for(pass=0;pass<2;++pass) {
        setup(0,0xffffffffu,.8f,1,0);
        lamp(pass?NODE+0x200:NODE,OWNER,pass?13.7f:12.3f);
        lamp(pass?NODE:NODE+0x200,OWNER,pass?12.3f:13.7f);
        assert(MEM32(QUEUE+0x5810)==2);
        pool=entry(0);cone=entry(1);
        assert(fabsf(MEMF(pool+0x30)-13)<.001f && fabsf(MEMF(cone+0x30)-13)<.001f);
        for(i=0;i<2;++i) {
            if(!pass) memcpy(saved[i],(const void *)XBOX_PTR(entry(i)),0x54);
            else assert(!memcmp(saved[i],(const void *)XBOX_PTR(entry(i)),0x54));
        }
        /* Repeated rendering of a source does not bias the centroid. */
        lamp(NODE,OWNER,12.3f);
        assert(MEM32(QUEUE+0x5810)==2 && fabsf(MEMF(cone+0x30)-13)<.001f);
    }
    /* Two cars only centimetres apart keep independent fields, even at the
     * exact position of a large street light with matching cone/type. */
    setup(0,0xffffffffu,.8f,1,0);
    MEM32(QUEUE+0x5810)=1;MEM32(RECORD+0x50)=0;
    MEMF(RECORD+0x30)=13;MEMF(RECORD+0x34)=.25f;MEMF(RECORD+0x38)=-3;
    MEMF(RECORD+0x40)=30;MEM32(RECORD+0x4c)=0xff123456;
    run();lamp(NODE+0x200,OWNER+0x1000,13.1f);
    assert(MEM32(QUEUE+0x5810)==5 && MEM32(RECORD+0x4c)==0xff123456);
    /* Tail and brake pools remain distinct even at identical coordinates;
     * two differently colored emergency sources keep their authored hues. */
    setup(1,0xffff0000u,.8f,2,0);run();
    again(2,4);assert(MEM32(QUEUE+0x5810)==2);
    MEM32(NODE+0x64)=0xff0000ff;again(2,4);
    assert(MEM32(QUEUE+0x5810)==3 && (MEM32(entry(2)+0x4c)&0xff0000)==0);
    /* A native new frame must not inherit any previous suppression, even
     * when its initial native-light count is larger than the last frame's. */
    setup(0,0xffffffffu,.8f,1,0);run();
    MEM32(QUEUE+0x5acc)=(MEM32(QUEUE+0x5acc)+1)&3;
    for(i=0;i<4;++i) {
        memset((void *)XBOX_PTR(entry(i)),0,0x54);MEM32(entry(i)+0x4c)=0xff112233;
    }
    MEM32(QUEUE+0x5810)=4;again(0,1);assert(MEM32(QUEUE+0x5810)==6);
    /* Partial same-frame queue recycling still invalidates matching groups. */
    memset((void *)XBOX_PTR(entry(4)),0,0x54);
    memset((void *)XBOX_PTR(entry(5)),0,0x54);
    again(0,1);assert(MEM32(QUEUE+0x5810)==8);
    /* Queue rollback without a stamp change is safe too. */
    MEM32(QUEUE+0x5810)=0;again(0,1);assert(MEM32(QUEUE+0x5810)==2);
}
static void banked_pool_checks(void)
{
    const float diagonal=.7071067812f;
    const float right[3]={.5f,diagonal,-.5f};
    const float forward[3]={diagonal,0,diagonal};
    const float up[3]={-.5f,diagonal,.5f};
    float previous[12]={0}, delta[3];
    unsigned i;
    uint32_t pool;
    setup(0,0xffffffffu,.8f,1,0);
    car_light_frame();
    memset(s_car_axis,0,sizeof s_car_axis);
    for(i=0;i<3;++i) {
        MEMF(MATRIX+i*4)=right[i];
        MEMF(MATRIX+12+i*4)=up[i];
        MEMF(MATRIX+24+i*4)=forward[i];
        previous[6+i]=forward[i]+.35f*up[i];
    }
    car_axis_remember(OWNER,previous);
    run();assert(MEM32(QUEUE+0x5810)==2 && flares==1);pool=entry(0);
    for(i=0;i<3;++i) {
        delta[i]=MEMF(pool+0x30+i*4)-MEMF(MATRIX+0x24+i*4)+.45f*up[i];
        assert(fabsf(delta[i]-5*forward[i])<.001f);
        assert(fabsf(MEMF(pool+i*4)-right[i])<.001f);
        assert(fabsf(MEMF(pool+0x10+i*4)-up[i])<.001f);
        assert(fabsf(MEMF(pool+0x20+i*4)-forward[i])<.001f);
    }
    assert(fabsf(car_light_dot(delta,up))<.001f);
    assert(fabsf(car_light_dot(delta,delta)-25)<.001f);
    memset(s_car_axis,0,sizeof s_car_axis);
}
static void crowded_queue_checks(void)
{
    unsigned i;
    setup(0,0xffffffffu,.8f,1,0);
    /* 51 native fixtures plus 80 traffic cars need 211 of the 256 records.
     * Nearby cars all survive; points remain sorted before cones. */
    for(i=0;i<51;++i) {
        uint32_t record=entry(i);
        MEMF(record)=MEMF(record+0x14)=MEMF(record+0x28)=1;
        MEM32(record+0x4c)=0xff102030;MEM32(record+0x50)=0;
    }
    MEM32(QUEUE+0x5810)=51;
    for(i=0;i<80;++i) lamp(NODE,OWNER+i*0x1000,13+i*.05f);
    assert(MEM32(QUEUE+0x5810)==211);
    for(i=0;i<211;++i) assert(MEM32(entry(i)+0x50)==(i>=131?1u:0u));
    setup(0,0xffffffffu,.8f,1,0);MEM32(QUEUE+0x5810)=224;run();assert(MEM32(QUEUE+0x5810)==225);
    setup(0,0xffffffffu,.8f,1,0);MEM32(QUEUE+0x5810)=256;run();assert(MEM32(QUEUE+0x5810)==256);
    for(i=0;i<300;++i) lamp(NODE,OWNER+i*0x1000,13+i*.05f);
    assert(MEM32(QUEUE+0x5810)==256 && s_car_groups<=256);
}
static void scalability_checks(void)
{
    struct car_light_group *group,*other;
    uint32_t color=car_light_front_color(0xffffffffu),collision=OWNER+0x100000;
    float matrix[16]={1,0,0,0,0,1,0,0,0,0,1,0,1,1,1,1};
    float source[12]={0},heading[3];
    unsigned bucket,slot,i;
    uint32_t cone;
    /* Colliding owner/color/kind keys remain distinct without a linear
     * search over every existing vehicle. */
    setup(0,0xffffffffu,.8f,1,0);car_light_frame();
    bucket=car_light_group_bucket(OWNER,0,color,&group);assert(!group);
    while(car_light_group_bucket(collision,0,color,&other)!=bucket) collision+=16;
    lamp(NODE,OWNER,12.3f);lamp(NODE+0x200,collision,13.7f);
    assert(s_car_groups==2 && MEM32(QUEUE+0x5810)==4);
    car_light_group_bucket(OWNER,0,color,&group);
    car_light_group_bucket(collision,0,color,&other);
    assert(group && other && group!=other);
    assert(fabsf(MEMF(group->record[1]+0x30)-12.3f)<.001f);
    assert(fabsf(MEMF(other->record[1]+0x30)-13.7f)<.001f);
    /* Native point insertion moves a cone. Cached queue positions must be
     * verified and repaired, and an altered image must still be rejected. */
    setup(0,0xffffffffu,.8f,1,0);run();group=s_car_group;cone=group->record[1];
    assert(group->slot[1]==1);
    assert(car_light_queue(matrix,2,2,2,0xff102030,0,0,&slot));
    car_light_frame();assert(slot==1 && group->slot[1]==1);
    assert(car_light_group_valid(group) && group->slot[1]==2 && entry(2)==cone);
    MEM32(cone+0x4c)^=1;assert(!car_light_group_valid(group));
    again(0,1);assert(s_car_groups==1 && MEM32(QUEUE+0x5810)==5);
    /* A temporarily absent record pointer may be retried by two lenses;
     * both belong to one group even before either obtains a native record. */
    setup(0,0xffffffffu,.8f,1,0);MEM32(QUEUE+0x5410)=0;
    lamp(NODE,OWNER,12.3f);lamp(NODE+0x200,OWNER,13.7f);
    assert(s_car_groups==1 && s_car_group[0].count==2 && !MEM32(QUEUE+0x5810));
    MEM32(QUEUE+0x5410)=RECORD;lamp(NODE,OWNER,12.3f);
    assert(s_car_groups==1 && MEM32(QUEUE+0x5810)==2);
    /* Queue pressure cannot fill host storage with empty duplicate groups. */
    setup(0,0xffffffffu,.8f,1,0);MEM32(QUEUE+0x5810)=256;
    for(i=0;i<320;++i) lamp(NODE,OWNER+i*0x1000,13);
    assert(!s_car_groups && MEM32(QUEUE+0x5810)==256);
    setup(1,0xffff0000u,.8f,2,0);MEM32(QUEUE+0x5810)=224;
    for(i=0;i<320;++i) lamp(NODE,OWNER+i*0x1000,13);
    assert(!s_car_groups && MEM32(QUEUE+0x5810)==224);
    /* More than 128 current vehicles must retain their own axis; this
     * host-only cache is independent of the native 256-light budget. */
    setup(0,0xffffffffu,.8f,1,0);car_light_frame();
    for(i=0;i<320;++i) {
        float angle=i*.003f;
        source[6]=sinf(angle);source[8]=cosf(angle);
        car_axis_remember(OWNER+i*0x1000,source);
    }
    for(i=0;i<320;++i) {
        float angle=i*.003f;
        source[6]=sinf(angle+.3f);source[8]=cosf(angle+.3f);
        assert(car_light_heading(OWNER+i*0x1000,source,heading));
        assert(fabsf(heading[0]-sinf(angle))<.001f && fabsf(heading[2]-cosf(angle))<.001f);
    }
    /* Complete-volume rejection must not spend host group capacity. Nearby
     * relevant traffic still needs slots after many offscreen cars and
     * repeated/paired callbacks have produced no native records. */
    {
        const uint32_t view=0x160000;
        const float unit=.7071067812f;
        setup(0,0xffffffffu,.8f,1,0);
        MEM32(0x3c5588)=view;MEM32(view+0x1ea4)=5;
        MEMF(view+0x1e54)=-unit;MEMF(view+0x1e54+8)=-unit;
        MEMF(view+0x1e64)=unit;MEMF(view+0x1e64+8)=-unit;
        MEMF(view+0x1e74+4)=-unit;MEMF(view+0x1e74+8)=-unit;
        MEMF(view+0x1e84+4)=unit;MEMF(view+0x1e84+8)=-unit;
        MEMF(view+0x1e94+8)=1;MEMF(view+0x1e94+12)=-80;
        for(i=0;i<320;++i) {
            uint32_t owner=OWNER+i*0x1000;
            lamp(NODE,owner,1000);lamp(NODE+0x200,owner,1001.4f);lamp(NODE,owner,1000);
        }
        assert(!MEM32(QUEUE+0x5810) && !s_car_groups);
        lamp(NODE,OWNER+0x200000,0);
        assert(MEM32(QUEUE+0x5810)==2 && s_car_groups==1);
    }
}
static void primary_driver(void)
{
    enum { MANAGER=0x180000, RACE=0x181000, PLAYER=0x182000 };
    MEM32(0x3c5cdc)=MANAGER;MEM32(MANAGER+0x54)=RACE;
    MEM32(RACE+0xd8)=PLAYER;MEM32(PLAYER+8)=OWNER;MEM32(OWNER+4)=PLAYER;
    assert(car_light_player(OWNER));
}
static void full_native_queue(unsigned mode)
{
    unsigned i;
    for(i=0;i<256;++i) {
        uint32_t record=entry(i);
        memset((void *)XBOX_PTR(record),0,0x54);
        MEMF(record)=MEMF(record+0x14)=MEMF(record+0x28)=MEMF(record+0x3c)=1;
        MEMF(record+0x40)=MEMF(record+0x44)=MEMF(record+0x48)=8;
        MEM32(record+0x4c)=0xff102030u+i;
        MEM32(record+0x50)=mode==0?0:mode==1?1:i>=224;
    }
    MEM32(QUEUE+0x5810)=256;
}
static void queue_permutation(void)
{
    unsigned i,seen[256]={0},previous=0;
    assert(MEM32(QUEUE+0x5810)==256);
    for(i=0;i<256;++i) {
        uint32_t record=entry(i),kind=MEM32(record+0x50);
        unsigned index=(record-RECORD)/0x54;
        assert(record>=RECORD && record<RECORD+256*0x54 && (record-RECORD)%0x54==0);
        assert(!seen[index]++ && kind>=previous && kind<=1);previous=kind;
    }
}
static struct car_light_group *player_front(uint32_t color)
{
    struct car_light_group *group;
    car_light_group_bucket(OWNER,0,car_light_front_color(color),&group);
    assert(group && group->priority && group->record[0] && group->record[1]);
    return group;
}
static void player_priority_checks(void)
{
    unsigned mode,i,j,frame;
    uint32_t pointers[2];
    unsigned char stable[2][0x54],baseline[2][0x54];
    struct car_light_group *group;
    /* A full native-only scene needs no remembered traffic source to lend
     * the primary driver its fill+beam. All point/cone mixtures preserve the
     * allocation permutation and native draw order. */
    for(mode=0;mode<3;++mode) {
        setup(0,0xffffffffu,.8f,1,0);full_native_queue(mode);
        run();assert(!s_car_groups);primary_driver();lamp(NODE,OWNER,13);
        group=player_front(0xffffffffu);
        for(j=0;j<2;++j)memcpy(stable[j],(const void *)XBOX_PTR(group->record[j]),0x54);
        lamp(NODE+0x200,OWNER,13);environment(OWNER);
        for(j=0;j<2;++j)assert(!memcmp(stable[j],(const void *)XBOX_PTR(group->record[j]),0x54));
        queue_permutation();
        /* A second authored front color retains its own fields and cannot
         * reclaim either of the already protected primary-driver records. */
        MEM32(NODE+0x64)=0xffc0d0ffu;lamp(NODE,OWNER,13);
        assert(player_front(0xffc0d0ffu)!=group);
        for(j=0;j<2;++j)assert(!memcmp(stable[j],(const void *)XBOX_PTR(group->record[j]),0x54));
        queue_permutation();
    }
    /* Occupancy/order jitter across frames must not reach the driver's
     * illumination. Real source groups include deliberately identical light
     * images, so displaced allocation aliases must be explicitly retired. */
    for(frame=0;frame<24;++frame) {
        unsigned native=32+frame%10,displaced=0;
        setup(0,0xffffffffu,.8f,1,0);
        for(i=0;i<native;++i) {
            MEM32(entry(i)+0x4c)=0xff102030;MEM32(entry(i)+0x50)=0;
        }
        MEM32(QUEUE+0x5810)=native;
        for(i=0;i<140;++i) {
            unsigned car=frame&1?139-i:i;
            lamp(NODE,OWNER+0x1000+car*0x1000,13);
        }
        assert(MEM32(QUEUE+0x5810)==256);
        primary_driver();lamp(NODE,OWNER,13);group=player_front(0xffffffffu);
        for(j=0;j<2;++j) {
            pointers[j]=group->record[j];
            memcpy(stable[j],(const void *)XBOX_PTR(pointers[j]),0x54);
            if(!frame)memcpy(baseline[j],stable[j],0x54);
            else assert(!memcmp(baseline[j],stable[j],0x54));
        }
        for(i=0;i<s_car_groups;++i) {
            struct car_light_group *traffic=s_car_group+i;
            if(traffic->priority || traffic->record[0])continue;
            ++displaced;
            lamp(NODE,traffic->owner,100+(float)i);
        }
        assert(displaced>=2);
        lamp(NODE,OWNER,13);lamp(NODE+0x200,OWNER,13);environment(OWNER);
        for(j=0;j<2;++j)assert(!memcmp(stable[j],(const void *)XBOX_PTR(pointers[j]),0x54));
        queue_permutation();
    }
    /* Host groups can be exhausted independently by missing-slot retries.
     * Spare primary-driver groups must still admit its front fields. */
    setup(0,0xffffffffu,.8f,1,0);car_light_frame();
    for(i=0;i<CAR_GROUP_SLOTS;++i) {
        unsigned bucket=car_light_group_bucket(OWNER+0x1000+i*0x1000,1,0xffff0000u,&group);
        assert(!group && bucket<CAR_GROUP_HASH_SLOTS);
        group=s_car_group+s_car_groups++;
        memset(group,0,sizeof *group);group->owner=OWNER+0x1000+i*0x1000;
        group->kind=1;group->color=0xffff0000u;s_car_group_hash[bucket]=(uint16_t)s_car_groups;
    }
    full_native_queue(2);primary_driver();lamp(NODE,OWNER,13);
    assert(s_car_groups==CAR_GROUP_SLOTS+1);player_front(0xffffffffu);queue_permutation();
    /* The identity chain, effect mask and world activation are still gates.
     * A graph belonging to another driver never receives replacement power. */
    setup(0,0xffffffffu,.8f,1,0);primary_driver();full_native_queue(2);
    lamp(NODE,OWNER+0x1000,13);assert(!s_car_groups);queue_permutation();
    MEM32(OWNER+4)=0;lamp(NODE,OWNER,13);assert(!s_car_groups);queue_permutation();
    setup(0,0xffffffffu,.8f,0,0);primary_driver();full_native_queue(2);run();
    assert(!s_car_groups);queue_permutation();
    setup(0,0xffffffffu,.8f,1,0);primary_driver();full_native_queue(2);
    MEM8(WORLD+0x2a8)=1;run();assert(!s_car_groups);queue_permutation();
    setup(0,0xffffffffu,.8f,1,0);MEM32(0x3c5cdc)=0x07fffff0;
    assert(!car_light_player(OWNER));
    MEM32(0x3c5cdc)=0x180000;MEM32(0x180054)=0x07fffff0;
    assert(!car_light_player(OWNER));
    MEM32(0x180054)=0x181000;MEM32(0x1810d8)=0x07fffffc;
    assert(!car_light_player(OWNER));
    puts("PASS: primary-driver fill+beam survive full native/traffic queues, 24 frame/order changes, paired/env/displaced callbacks, identical-image aliases, multiple front colors and 256 occupied host groups; queue pointer permutation/order and activation/identity gates");
}
int main(void)
{
    unsigned kind;
    direction_checks();axis_checks();current_axis_checks();fading_checks();
    grouping_checks();reset_lifecycle_checks();banked_pool_checks();crowded_queue_checks();scalability_checks();player_priority_checks();
    setup(0,0xffffffffu,.8f,0,0);run();assert(!MEM32(QUEUE+0x5810)&&!flares);
    setup(0,0xffffffffu,0,1,0);run();assert(!MEM32(QUEUE+0x5810)&&!flares);
    setup(0,0xffffffffu,.8f,1,0);MEM8(WORLD+0x2a8)=1;run();assert(!MEM32(QUEUE+0x5810));
    setup(0,0xffffffffu,.8f,1,0);MEM32(WORLD+0x24c)=0;run();assert(!MEM32(QUEUE+0x5810));
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
        else {
            uint32_t color=MEM32(entry(0)+0x4c);
            assert((color&255)==((color>>8)&255) && (color&255)==((color>>16)&255));
        }
        assert(MEMF(entry(0)+0x40)==(kind==1?4.4f:kind==2?6.8f:9.0f));
        setup(kind,0xffffffffu,.8f,0,0);run();assert(!MEM32(QUEUE+0x5810));
    }
    for(kind=5;kind<=6;++kind) {
        setup(kind,0xffff9900u,.8f,1u<<kind,0);run();assert(MEM32(QUEUE+0x5810)==1);
        assert((MEM32(entry(0)+0x4c)&255)==0);
        setup(kind,0xffff9900u,.8f,1u<<kind,0);
        MEM8(OWNER+0xed)=1;MEMF(WORLD+0x1c0)=4;MEMF(OWNER+0x12c)=1;
        run();assert(!MEM32(QUEUE+0x5810)&&!flares);
    }
    setup(4,0xffff0000u,.8f,1u<<4,0);run();assert(MEM32(QUEUE+0x5810)==1);
    setup(9,0xff0000ffu,.8f,1u<<9,0);run();assert(MEM32(QUEUE+0x5810)==1);
    assert((MEM32(entry(0)+0x4c)&0xffff00)==0);
    puts("PASS: warm/dim native lenses and fields; current-frame steering and traversal; smooth brightness/paired-source fades; eight headings; banked near-field gradient; stable paired lamps; close traffic/street/color independence; real native resets, skipped/env-only stamp wraps, foreign-queue isolation and full-queue reuse; 80 traffic cars; activation, queue bounds and ABI");
    return 0;
}
'''


def main():
    if not shutil.which("cl"):
        raise SystemExit("Run scripts/enter-env.ps1 first so cl.exe is available.")
    audit_assets()
    native = native_projector.generated().replace(
        "void sub_000CF2DA(void)", "void sub_000CF2DA_original(void)")
    native = native.replace("void sub_0022FDFD(void)", "void sub_0022FDFD_original(void)")
    source = native_projector.SOURCE.split("static unsigned submit", 1)[0]
    source = source.replace("memory[0x400000]", "memory[0x500000]")
    source = source.replace('void sub_001A842A(void) { assert(!"unexpected flare submission"); }',
                            'static unsigned flares; static uint32_t flare_color,flare_node;\nvoid sub_001A842A(void) { ++flares; flare_color=MEM32(flare_node+0x64); g_esp+=16; }')
    source += CHECKS.replace("static unsigned flares;", "")
    with tempfile.TemporaryDirectory(prefix="mm3-car-lighting-") as temporary:
        folder = Path(temporary)
        shutil.copyfile(ROOT / "src/mm3_recomp_types.h", folder / "recomp_types.h")
        declarations = "\n".join(f"void {name}(void);" for name in native_projector.NATIVE)
        declarations += "\nvoid sub_000CF2DA_original(void);\nvoid sub_0022FDFD_original(void);\n"
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
