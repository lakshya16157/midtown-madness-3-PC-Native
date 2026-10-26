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
native_projector.NATIVE += ("sub_0022FDFD", "sub_000DFE0E")


def audit_assets():
    sys.path.insert(0, str(ROOT / "tools/car-models"))
    import mm3_model
    from ensure_night_lights import ensure_night_light
    models = shared = ambient = fallback = unlit_dlc = 0
    installed = sorted((ROOT / "game_files/Data/Shared/Cars").glob("*/body.cmp"))
    installed += sorted((ROOT / "game_files/TDATA").rglob("body.cmp"))
    for path in installed:
        # Downloadable content is signed: an edited file is reported as
        # damaged content, so an unlit DLC model (car213) stays authored.
        signed = "TDATA" in path.parts
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
        elif signed:
            unlit_dlc += 1
            models += 1
            continue
        else:
            raise AssertionError(f"No front-light source; run ensure_night_lights.py: {path}")
        assert ensure_night_light(path.read_bytes()) == (path.read_bytes(), None), path
        models += 1
    assert shared and ambient and fallback, (models, shared, ambient, fallback)
    print(f"PASS: {models} installed models; {shared} shared-beam models, "
          f"{ambient} front-lamp-only traffic models, {fallback} zero-color fallbacks, "
          f"{unlit_dlc} unlit signed DLC models left authored")


CHECKS = r'''
#include "mm3_light_queue.c"
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
    memset(g_fp_stack,0,sizeof g_fp_stack);native_constants();flares=0;flare_color=0;flare_driver=0;
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
static void time_of_day_checks(void)
{
    const uint32_t times[]={0,1,2,3,4,0xffffffffu};
    const uint32_t sequence[]={3,0,2,1,3,2};
    const unsigned nonfront[]={1,2,3,4,5,6,9};
    unsigned i,j;
    for(i=0;i<sizeof times/sizeof times[0];++i) {
        unsigned dark=times[i]==2 || times[i]==3;
        /* Real native lens rendering and custom beam/fill must agree even
         * if the game retains front-light mask bits during a daytime race. */
        setup(0,0xffffffffu,.8f,0x101,0);MEM32(0x407c30)=times[i];run();
        assert(MEM32(QUEUE+0x5810)==(dark?2u:0u) && flares==dark);
        assert(MEM32(STACK+12)==0x101 && MEMF(NODE+0x54)==.8f);
        /* The source-only/offscreen path observes the same time gate. */
        setup(0,0xffffffffu,.8f,0x101,0);MEM32(0x407c30)=times[i];
        mm3_car_lighting_emit(NODE,0x101,(const float *)XBOX_PTR(MATRIX));
        assert(MEM32(QUEUE+0x5810)==(dark?2u:0u) && !flares);
        /* Zero-color Formula 1 handles gain illumination in evening as well
         * as night; neither native projectors nor host axes leak into day. */
        setup(8,0,0,0x101,0);MEM32(0x407c30)=times[i];run();
        assert(MEM32(QUEUE+0x5810)==(dark?2u:0u) && !flares);
        assert(MEM32(STACK+12)==0x101 && MEM32(NODE+0x64)==0 && MEMF(NODE+0x54)==0);
        assert(s_car_axis[car_axis_slot(OWNER)].owner==(dark?OWNER:0u));
        setup(8,0xffffffffu,.8f,0x101,0);MEM32(0x407c30)=times[i];run();
        assert(!MEM32(QUEUE+0x5810) && !flares && MEM32(STACK+12)==0x101);
        for(j=0;j<sizeof nonfront/sizeof nonfront[0];++j) {
            unsigned kind=nonfront[j];
            uint32_t night_color,night_flare_color;
            unsigned night_flares;
            setup(kind,0xffff0000u,.8f,1u<<kind,0);run();
            assert(MEM32(QUEUE+0x5810)==1);
            night_color=MEM32(entry(0)+0x4c);night_flares=flares;night_flare_color=flare_color;
            setup(kind,0xffff0000u,.8f,1u<<kind,0);MEM32(0x407c30)=times[i];run();
            assert(MEM32(QUEUE+0x5810)==1 && flares==night_flares);
            /* A reversing lamp's lens glow is dimmer by day; every other lens
             * keeps its authored glow in every period. */
            assert(flare_color==(kind==3 && !dark ?
                   car_light_color(night_flare_color,CAR_REVERSE_GLOW_DAY) : night_flare_color));
            if(kind==3 && !dark) {
                /* By day a reversing lamp keeps only a faint field at its lens,
                 * not the lane-wide patch on the road. */
                assert(MEM32(entry(0)+0x4c)==0xff060000u);
                assert(MEMF(entry(0)+0x40)==CAR_REVERSE_RADIUS_DAY*2);
                assert(MEMF(entry(0)+0x44)==CAR_REVERSE_RADIUS_DAY*2 &&
                       MEMF(entry(0)+0x48)==CAR_REVERSE_RADIUS_DAY*2);
            } else if(kind==1 || kind==2 || kind==5 || kind==6) {
                float night_radius=kind==1?CAR_TAIL_RADIUS_NIGHT:kind==2?CAR_BRAKE_RADIUS_NIGHT:
                                   CAR_SIGNAL_RADIUS_NIGHT;
                float day_radius=kind==1?CAR_TAIL_RADIUS_DAY:kind==2?CAR_BRAKE_RADIUS_DAY:
                                 CAR_SIGNAL_RADIUS_DAY;
                float diameter=(dark?night_radius:day_radius)*2;
                uint32_t day_color=kind==1?0xff030000u:kind==2?0xff080000u:0xff060000u;
                if(kind==1) assert(diameter==(dark?4.4f:.7f));
                if(kind>=5) assert(diameter==(dark?2.8f:.8f));
                /* Evening keeps night lamps on with a smaller, softer field. */
                if(times[i]==2) {
                    diameter=night_radius*MM3_EVENING_LIGHT_REACH*2;
                    night_color=kind==1?0xff220000u:kind==2?0xff3d0000u:0xff170000u;
                }
                assert(MEM32(entry(0)+0x4c)==(dark?night_color:day_color));
                assert(MEMF(entry(0)+0x40)==diameter);
                assert(MEMF(entry(0)+0x44)==diameter && MEMF(entry(0)+0x48)==diameter);
            } else if(times[i]==2) {
                uint32_t red=(MEM32(entry(0)+0x4c)>>16)&255u;
                assert(red && red<((night_color>>16)&255u));
            } else assert(MEM32(entry(0)+0x4c)==night_color);
        }
    }
    /* Retain authored parameters and masks across successive races. Native
     * reset must discard prior night fields before a daytime frame. */
    setup(0,0xffffffffu,.8f,0x101,0);
    for(i=0;i<sizeof sequence/sizeof sequence[0];++i) {
        unsigned dark=sequence[i]==2 || sequence[i]==3;
        native_reset(QUEUE);flares=0;MEM32(0x407c30)=sequence[i];
        again(0,0x101);
        assert(MEM32(QUEUE+0x5810)==(dark?2u:0u) && flares==dark);
        assert(MEM32(NODE+0x64)==0xffffffffu && MEMF(NODE+0x54)==.8f);
        assert(MEM32(STACK+12)==0x101);
    }
    /* Both native draws and source-only collection use the same rear-light
     * daylight attenuation. Repeated period switches cannot alter lenses or
     * leave the old daytime/evening/night field size and intensity behind. */
    for(j=1;j<=2;++j) {
        setup(j,0xffff0000u,.8f,1u<<j,0);
        for(i=0;i<sizeof sequence/sizeof sequence[0];++i) {
            unsigned dark=sequence[i]==2 || sequence[i]==3;
            uint32_t color=j==1?(dark?0xff2d0000u:0xff030000u):
                                     (dark?0xff520000u:0xff080000u);
            float diameter=j==1?(dark?4.4f:.7f):(dark?6.8f:1.0f);
            if(sequence[i]==2) {
                color=j==1?0xff220000u:0xff3d0000u;
                diameter=(j==1?CAR_TAIL_RADIUS_NIGHT:CAR_BRAKE_RADIUS_NIGHT)*
                         MM3_EVENING_LIGHT_REACH*2;
            }
            native_reset(QUEUE);flares=0;MEM32(0x407c30)=sequence[i];
            mm3_car_lighting_emit(NODE,1u<<j,(const float *)XBOX_PTR(MATRIX));
            assert(MEM32(QUEUE+0x5810)==1 && !flares);
            assert(MEM32(entry(0)+0x4c)==color);
            assert(MEMF(entry(0)+0x40)==diameter);
            assert(MEMF(entry(0)+0x44)==diameter && MEMF(entry(0)+0x48)==diameter);
            again(j,1u<<j);
            assert(MEM32(QUEUE+0x5810)==1 && MEM32(entry(0)+0x4c)==color);
            assert(MEM32(NODE+0x64)==0xffff0000u && MEMF(NODE+0x54)==.8f);
            assert(MEM32(STACK+12)==(1u<<j));
        }
    }
    puts("PASS: front lenses/fields only light evening/night; morning/noon tail, brake, indicator and reverse spill stays faint/tight while native lenses keep authoring (reverse glow dimmer by day); evening fields are smaller and dimmer than night; emergency unchanged by day; native/offscreen sources and repeated period transitions agree");
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
    /* Off-screen cars still get their fields (no per-volume frustum test);
     * the frame budget decides what is drawn. */
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
        for(i=0;i<20;++i) {
            uint32_t owner=OWNER+i*0x1000;
            lamp(NODE,owner,1000);lamp(NODE+0x200,owner,1001.4f);lamp(NODE,owner,1000);
        }
        assert(MEM32(QUEUE+0x5810)==40 && s_car_groups==20);
    }
}
static void primary_driver(void);
static void camera_at(float x,float y,float z);
static void camera_behind(float back);
/* PC-sized queue (512): 300 street-lamp records first, then 40 cars whose
 * two front lenses each arrive twice (pre-cull and visible draw). Every car
 * keeps exactly one pool and one beam. Beams sort past slot 256 and must be
 * found there, not queued again per lens (stacked, then flickering beams). */
static void extended_queue_checks(void)
{
    enum { BIG=0x200000, BIG_RECORDS=0x280000 };
    uint32_t slots;
    unsigned i,pass;
    setup(0,0xffffffffu,.8f,1,0);
    MEM32(0x3c5d8c)=BIG;
    for(i=0;i<256;++i) MEM32(BIG+0x5410+i*4)=BIG_RECORDS+i*0x54;
    s_requested=512;mm3_light_queue_allocated(BIG);mm3_light_queue_attach();
    assert(mm3_light_queue_capacity(BIG)==512);
    native_reset(BIG);
    slots=mm3_light_queue_slots(BIG);
    for(i=0;i<300;++i) {
        uint32_t record=MEM32(slots+i*4);
        MEMF(record)=MEMF(record+0x14)=MEMF(record+0x28)=1;
        MEM32(record+0x4c)=0xff102030;MEM32(record+0x50)=0;
    }
    MEM32(BIG+0x5810)=300;
    for(i=0;i<40;++i)
        for(pass=0;pass<2;++pass) {
            lamp(NODE,OWNER+i*0x1000,13+i*.05f);
            lamp(NODE+0x200,OWNER+i*0x1000,14.4f+i*.05f);
        }
    assert(MEM32(BIG+0x5810)==380 && s_car_groups==40);
    for(i=0;i<380;++i) assert(MEM32(MEM32(slots+i*4)+0x50)==(i>=340?1u:0u));
    for(i=0;i<40;++i) assert(s_car_group[i].count==2 && s_car_group[i].record[0] && s_car_group[i].record[1]);
    /* 120 more cars fill the queue: pools stop once 480 records are queued
     * (50 more cars), the next 32 cars still get their beams, and nothing is
     * queued twice. */
    for(i=40;i<160;++i)
        for(pass=0;pass<2;++pass) {
            lamp(NODE,OWNER+i*0x1000,13+i*.05f);
            lamp(NODE+0x200,OWNER+i*0x1000,14.4f+i*.05f);
        }
    assert(MEM32(BIG+0x5810)==512);
    {
        unsigned points=0,cones=0;
        for(i=0;i<512;++i) {
            if(MEM32(MEM32(slots+i*4)+0x50)) ++cones; else ++points;
            if(i) assert(MEM32(MEM32(slots+(i-1)*4)+0x50)<=MEM32(MEM32(slots+i*4)+0x50));
        }
        assert(points==390 && cones==122);
    }
    s_queue=s_slots=s_allocation=s_requested=0;s_capacity=MM3_LIGHT_QUEUE_RETAIL;
}
/* Frame budget at the consumer. 807 of 1024 records queued: 700 street-lamp
 * points along +z at 11..710 m (in queue order the nearest last), 60 car
 * tail pools at 50..109 m, 40 spots at 15..210 m, 4 driver records 2 km away,
 * a dark record and 2 tiny bulb pools (30 m and 400 m). The near lamps and
 * spots, every car light and the driver's car are drawn; the far lamps, the
 * far tiny pool and the dark record are not; points stay before spots and
 * the pointer array keeps every record. */
static uint32_t budget_record(uint32_t slots,unsigned i,float z,float size,uint32_t color,unsigned spot)
{
    uint32_t record=MEM32(slots+i*4);
    memset((void *)XBOX_PTR(record),0,0x54);
    MEMF(record)=MEMF(record+0x14)=MEMF(record+0x28)=1;
    MEMF(record+0x30)=0;MEMF(record+0x34)=5;MEMF(record+0x38)=z;
    MEMF(record+0x40)=MEMF(record+0x44)=MEMF(record+0x48)=size;
    MEM32(record+0x4c)=color;MEM32(record+0x50)=spot;
    return record;
}
static unsigned budget_frame(uint32_t queue,float drift)
{
    uint32_t slots=mm3_light_queue_slots(queue),record;
    unsigned i,n=0;
    for(i=0;i<700;++i) budget_record(slots,n++,710-i+drift,18,0xffffd9a0u,0);
    for(i=0;i<60;++i) mm3_light_queue_tag(budget_record(slots,n++,50+i+drift,4.4f,0xff380000u,0),1);
    budget_record(slots,n++,30+drift,3,0xff806040u,0);
    budget_record(slots,n++,400+drift,3,0xff806040u,0);
    budget_record(slots,n++,20+drift,18,0xff000000u,0);
    for(i=0;i<4;++i) mm3_light_queue_tag(budget_record(slots,n++,2000+drift,4.4f,0xff380000u,0),2);
    for(i=0;i<40;++i) budget_record(slots,n++,15+i*5+drift,12,0xff808080u,1);
    for(;n<1024;++n) { record=budget_record(slots,n,50,18,0xff808080u,0); MEM32(record+0x50)=1; }
    MEM32(queue+0x5810)=807;
    mm3_light_queue_frame(queue);
    return MEM32(queue+0x5810);
}
static void budget_checks(void)
{
    enum { BIG=0x200000, BIG_RECORDS=0x280000 };
    uint32_t slots,seen[1024],record;
    unsigned i,j,drawn,frame,cars,drivers,spots,near_spots,near_lamps,far_lamps,tiny_near,tiny_far,dark;
    camera_at(0,5,0);
    MEM32(0x3c5d8c)=BIG;
    for(i=0;i<256;++i) MEM32(BIG+0x5410+i*4)=BIG_RECORDS+i*0x54;
    s_requested=1024;mm3_light_queue_allocated(BIG);mm3_light_queue_attach();
    assert(mm3_light_queue_capacity(BIG)==1024);
    slots=mm3_light_queue_slots(BIG);
    s_budget=512;s_threshold=0;
    /* The first crowded frame may overshoot by a quarter rather than cut
     * hard; within a second the drawn set settles to the budget. */
    drawn=budget_frame(BIG,0);
    assert(drawn<=4+640 && drawn>512);
    for(frame=0;frame<90;++frame) drawn=budget_frame(BIG,frame*.01f);
    drawn=budget_frame(BIG,0);
    assert(drawn<=512+8 && drawn>=500);
    cars=drivers=spots=near_spots=near_lamps=far_lamps=tiny_near=tiny_far=dark=0;
    for(i=0;i<drawn;++i) {
        record=MEM32(slots+i*4);
        if(i) assert(MEM32(MEM32(slots+(i-1)*4)+0x50)<=MEM32(record+0x50));
        if(MEM32(record+0x50)) { ++spots; near_spots+=MEMF(record+0x38)<=100; continue; }
        if(MEMF(record+0x38)>=2000) ++drivers;
        else if(MEMF(record+0x40)==4.4f) { ++cars; assert(MEM32(record+0x4c)==0xff380000u); }
        else if(MEMF(record+0x40)==3) { if(MEMF(record+0x38)<100) ++tiny_near; else ++tiny_far; }
        else if(!(MEM32(record+0x4c)&0xffffff)) ++dark;
        else if(MEMF(record+0x38)<=100) { ++near_lamps; assert(MEM32(record+0x4c)==0xffffd9a0u); }
        else if(MEMF(record+0x38)>=650) ++far_lamps;
    }
    assert(drivers==4 && cars==60 && tiny_near==1 && !tiny_far && !dark);
    assert(near_lamps==90 && !far_lamps && near_spots==18 && spots<40);
    /* Every allocated record is still in the array exactly once. */
    for(i=0;i<1024;++i) seen[i]=MEM32(slots+i*4);
    for(i=0;i<1024;++i) for(j=i+1;j<1024;++j) assert(seen[i]!=seen[j]);
    /* Lights near the cut-off fade over frames instead of blinking: a lamp
     * just inside the budget changes colour gradually as demand rises. */
    {
        float before=s_threshold;
        drawn=budget_frame(BIG,1.0f);
        assert(fabsf(s_threshold-before)<=.35f*fabsf(before)+1e-6f);
    }
    /* Quiet frames: nothing over budget, no fading beyond the tiny floor. */
    for(frame=0;frame<200;++frame) {
        MEM32(BIG+0x5810)=0;
        budget_record(slots,0,40,18,0xffffd9a0u,0);
        mm3_light_queue_tag(budget_record(slots,1,30,4.4f,0xff380000u,0),1);
        MEM32(BIG+0x5810)=2;mm3_light_queue_frame(BIG);
    }
    assert(MEM32(BIG+0x5810)==2 && s_threshold==0);
    assert(MEM32(MEM32(slots)+0x4c)==0xffffd9a0u || MEM32(MEM32(slots+4)+0x4c)==0xffffd9a0u);
    /* MM3_LIGHT_BUDGET=0 draws everything, as before. */
    s_budget=0;s_threshold=0;
    assert(budget_frame(BIG,0)==807);
    s_budget=~0u;s_threshold=0;
    MEM32(0x3c5588)=0;
    s_queue=s_slots=s_allocation=s_requested=0;s_capacity=MM3_LIGHT_QUEUE_RETAIL;
}
/* A live camera at `eye` looking along +z, as native F3F93 leaves it:
 * world-to-camera rows (identity rotation, translation -eye), the camera
 * position at +0x1EA8 and side planes through it. */
static void camera_at(float x,float y,float z)
{
    const uint32_t view=0x170000;
    const float eye[3]={x,y,z};
    static const float n[5][3]={{-.70710678f,0,-.70710678f},{.70710678f,0,-.70710678f},
                                {0,-.70710678f,-.70710678f},{0,.70710678f,-.70710678f},{0,0,-1}};
    unsigned i,j;
    MEM32(0x3c5588)=view;MEM32(view+0x1ea4)=5;
    for(i=0;i<3;++i) {
        for(j=0;j<3;++j) MEMF(view+0x1f24+i*16+j*4)=i==j?1.0f:0.0f;
        MEMF(view+0x1f54+i*4)=-eye[i];MEMF(view+0x1ea8+i*4)=eye[i];
    }
    for(i=0;i<5;++i) {
        float d=-(n[i][0]*eye[0]+n[i][1]*eye[1]+n[i][2]*eye[2])-(i==4?1.0f:0.0f);
        for(j=0;j<3;++j) MEMF(view+0x1e54+i*16+j*4)=n[i][j];
        MEMF(view+0x1e60+i*16)=d;
    }
}
/* A camera `back` metres behind the lamps (at z=-8), looking along +z. */
static void camera_behind(float back) {camera_at(13,.7f,-8-back);}
/* A car's fields do not depend on the camera: a pass seen from another view,
 * or a camera that moves, cannot change a tail lamp or beam from one frame to
 * the next (far/faint lights are the frame budget's job). */
static void distance_detail_checks(void)
{
    static const float backs[]={30,90,150,250,1000};
    uint32_t near_tail,near_pool,near_beam;
    unsigned i;
    setup(1,0xffff0000u,.8f,2,0);run();
    assert(MEM32(QUEUE+0x5810)==1);near_tail=MEM32(entry(0)+0x4c);
    setup(0,0xffffffffu,.8f,1,0);run();
    assert(MEM32(QUEUE+0x5810)==2);near_pool=MEM32(entry(0)+0x4c);near_beam=MEM32(entry(1)+0x4c);
    for(i=0;i<sizeof backs/sizeof backs[0];++i) {
        setup(1,0xffff0000u,.8f,2,0);camera_behind(backs[i]);run();
        assert(MEM32(QUEUE+0x5810)==1 && MEM32(entry(0)+0x4c)==near_tail);
        /* Same frame, second pass from another camera: unchanged. */
        camera_behind(backs[(i+2)%5]);again(1,2);
        assert(MEM32(QUEUE+0x5810)==1 && MEM32(entry(0)+0x4c)==near_tail);
        setup(0,0xffffffffu,.8f,1,0);camera_behind(backs[i]);run();
        assert(MEM32(QUEUE+0x5810)==2 && MEM32(entry(0)+0x4c)==near_pool && MEM32(entry(1)+0x4c)==near_beam);
    }
}
static void primary_driver(void)
{
    enum { MANAGER=0x180000, RACE=0x181000, PLAYER=0x182000 };
    MEM32(0x3c5cdc)=MANAGER;MEM32(MANAGER+0x54)=RACE;
    MEM32(RACE+0xd8)=PLAYER;MEM32(PLAYER+8)=OWNER;MEM32(OWNER+4)=PLAYER;
    MEM8(PLAYER+0x657)=1; /* Native headlight switch: on, as at a night start. */
    MEM32(PLAYER+0x488)=1; /* The local driver. */
    assert(car_light_player(OWNER));
}
/* The player's front lenses/beams follow the game's own headlight switch
 * (Light control); every other car and every rear lamp keeps its rule. */
static void headlight_switch_checks(void)
{
    const uint32_t switch_byte=0x182000+0x657;
    uint32_t time;
    for(time=0;time<4;++time) {
        unsigned dark=time>=2;
        /* Switched off: no lens, beam or near field, even at night. */
        setup(0,0xffffffffu,.8f,0x101,0);primary_driver();MEM8(switch_byte)=0;
        MEM32(0x407c30)=time;run();
        assert(!MEM32(QUEUE+0x5810) && !flares && MEM32(STACK+12)==0x101);
        /* Switched on: lit in every period, daytime included; its lens glow
         * is drawn as the driver's (the glow report's attribution). */
        setup(0,0xffffffffu,.8f,0x101,0);primary_driver();MEM32(0x407c30)=time;run();
        assert(MEM32(QUEUE+0x5810)==2 && flares==1 && flare_driver==1 && !glow_driver);
        /* The shared road-light handle obeys the same switch. */
        setup(8,0,0,0x101,0);primary_driver();MEM8(switch_byte)=0;MEM32(0x407c30)=time;run();
        assert(!MEM32(QUEUE+0x5810));
        /* Source-only collection agrees with the native draw. */
        setup(0,0xffffffffu,.8f,0x101,0);primary_driver();MEM8(switch_byte)=0;MEM32(0x407c30)=time;
        mm3_car_lighting_emit(NODE,0x101,(const float *)XBOX_PTR(MATRIX));
        assert(!MEM32(QUEUE+0x5810));
        /* Switched off, the tail lamps go dark with it in every period (by
         * day their lenses used to stay lit). */
        setup(1,0xffff0000u,.8f,2,0);primary_driver();MEM8(switch_byte)=0;MEM32(0x407c30)=time;run();
        assert(!MEM32(QUEUE+0x5810) && !flares);
        setup(1,0xffff0000u,.8f,2,0);primary_driver();MEM32(0x407c30)=time;run();
        assert(MEM32(QUEUE+0x5810)==1);
        /* Brake lamps always work. */
        setup(2,0xffff0000u,.8f,4,0);primary_driver();MEM8(switch_byte)=0;MEM32(0x407c30)=time;run();
        assert(MEM32(QUEUE+0x5810)==1);
        /* A traffic car keeps the time-of-day rule; its glow is not the driver's. */
        setup(0,0xffffffffu,.8f,0x101,0);MEM32(0x407c30)=time;run();
        assert(MEM32(QUEUE+0x5810)==(dark?2u:0u) && !flare_driver);
        /* A non-local driver keeps it too, whatever its switch byte says. */
        setup(0,0xffffffffu,.8f,0x101,0);primary_driver();MEM32(0x182000+0x488)=0;
        MEM8(switch_byte)=0;MEM32(0x407c30)=time;run();
        assert(MEM32(QUEUE+0x5810)==(dark?2u:0u));
    }
    /* Diagnostic probe: switched off at night, the road 6m ahead of the
     * driver's lenses is checked against the drawn lights; a street lamp's
     * pool over it counts as a world light. Switched on, nothing is probed. */
    {
        uint32_t record;
        /* A new frame: earlier checks tagged these records in this one. */
        setup(0,0xffffffffu,.8f,0x101,0);primary_driver();MEM8(switch_byte)=0;
        mm3_light_queue_frame(QUEUE);run();
        assert(!MEM32(QUEUE+0x5810) && s_probe_frame==s_frame);
        assert(s_probe_point[0]==13.0f && fabsf(s_probe_point[1]-.7f)<1e-5f && fabsf(s_probe_point[2]+2.0f)<1e-5f);
        record=entry(0);memset((void *)XBOX_PTR(record),0,0x54);
        MEMF(record)=MEMF(record+0x14)=MEMF(record+0x28)=MEMF(record+0x3c)=1;
        MEMF(record+0x30)=13;MEMF(record+0x34)=.7f;MEMF(record+0x38)=-4;
        MEMF(record+0x40)=MEMF(record+0x44)=MEMF(record+0x48)=10;MEM32(record+0x4c)=0x00ffd080u;
        /* (The budget's cut-off still holds the busy frames of earlier checks.) */
        MEM32(QUEUE+0x5810)=1;s_usage_on=1;s_threshold=0;memset(&s_probe,0,sizeof s_probe);
        mm3_light_queue_frame(QUEUE);
        assert(s_probe.frames==1 && s_probe.covered[0]==1 && !s_probe.covered[1] && !s_probe.covered[2]);
        assert(fabsf(s_probe.best-.84f)<1e-4f && s_probe.best_color==0x00ffd080u && !s_probe.best_cone);
        setup(0,0xffffffffu,.8f,0x101,0);primary_driver();memset(&s_probe,0,sizeof s_probe);run();
        assert(s_probe_frame!=s_frame);
        s_usage_on=-1;memset(&s_usage,0,sizeof s_usage);memset(&s_probe,0,sizeof s_probe);
    }
    puts("PASS: player headlights follow the native Light switch in every period (lens, beam, road handle, offscreen), tails with them; brake lamps and traffic unchanged");
}
/* Native 21C6C8 sets each driven vehicle's mask through DFE0E (ECX graph,
 * EDI vehicle). Without any race association (cruise) that call alone lets
 * the driver's own switch govern its lamps. */
static void native_vehicle_mask(uint32_t vehicle, uint32_t bits)
{
    uint32_t saved[2],saved_esp=g_esp,saved_ecx=g_ecx,saved_edi=g_edi,saved_eax=g_eax;
    memcpy(saved,(const void *)XBOX_PTR(STACK),sizeof saved);
    MEM32(STACK)=0x0021C789u;MEM32(STACK+4)=0;
    g_esp=STACK;g_ecx=OWNER;g_edi=vehicle;g_eax=bits;
    sub_000DFE0E();
    assert(g_esp==STACK+8 && (MEM32(OWNER+0x124)&bits&0x7fu)==(bits&0x7fu));
    memcpy((void *)XBOX_PTR(STACK),saved,sizeof saved);
    g_esp=saved_esp;g_ecx=saved_ecx;g_edi=saved_edi;g_eax=saved_eax;
}
static void driven(uint32_t vehicle, unsigned local, unsigned on, uint32_t time)
{
    MEM32(vehicle+8)=OWNER;MEM32(vehicle+0x488)=local;MEM8(vehicle+0x657)=(uint8_t)on;
    native_vehicle_mask(vehicle,0x83);MEM32(0x407c30)=time;
}
static void cruise_switch_checks(void)
{
    enum { VEHICLE=0x184000, OTHER=0x186000 };
    uint32_t time;
    for(time=0;time<4;++time) {
        unsigned dark=time>=2;
        setup(0,0xffffffffu,.8f,0x101,0);driven(VEHICLE,1,0,time);run();
        assert(!MEM32(QUEUE+0x5810) && !flares && MEM32(STACK+12)==0x101);
        setup(0,0xffffffffu,.8f,0x101,0);driven(VEHICLE,1,1,time);run();
        assert(MEM32(QUEUE+0x5810)==2 && flares==1);
        setup(1,0xffff0000u,.8f,2,0);driven(VEHICLE,1,0,time);run();
        assert(!MEM32(QUEUE+0x5810));
        /* The same graph driven by another vehicle follows that vehicle. */
        setup(0,0xffffffffu,.8f,0x101,0);driven(VEHICLE,1,0,time);driven(OTHER,1,1,time);run();
        assert(MEM32(QUEUE+0x5810)==2);
        /* A stale pairing (graph no longer the vehicle's) is not trusted. */
        setup(0,0xffffffffu,.8f,0x101,0);driven(VEHICLE,1,0,time);MEM32(VEHICLE+8)=0;run();
        assert(MEM32(QUEUE+0x5810)==(dark?2u:0u));
        /* Opponents (not the local driver) keep the time-of-day rule. */
        setup(0,0xffffffffu,.8f,0x101,0);driven(VEHICLE,2,0,time);run();
        assert(MEM32(QUEUE+0x5810)==(dark?2u:0u));
    }
    /* Other DFE0E callers neither register nor change the native result. */
    memset(s_car_switch,0,sizeof s_car_switch);
    setup(0,0xffffffffu,.8f,0x101,0);MEM32(VEHICLE+8)=OWNER;MEM32(VEHICLE+0x488)=1;
    MEM8(VEHICLE+0x657)=0;
    {
        uint32_t saved[2];
        memcpy(saved,(const void *)XBOX_PTR(STACK),sizeof saved);
        MEM32(STACK)=0x00133E97u;MEM32(STACK+4)=0;g_esp=STACK;g_ecx=OWNER;g_edi=VEHICLE;g_eax=0x60;
        MEM32(OWNER+0x124)=0x7f;sub_000DFE0E();
        assert(g_esp==STACK+8 && MEM32(OWNER+0x124)==0x1f);
        memcpy((void *)XBOX_PTR(STACK),saved,sizeof saved);g_esp=STACK;g_ecx=NODE;g_edi=g_eax=0;
    }
    MEM32(0x407c30)=3;run();assert(MEM32(QUEUE+0x5810)==2);
    puts("PASS: cruise driver pairing via the native vehicle mask update: switch off/on, night tails, re-driven graphs, stale pairs, opponents and other callers");
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
    /* The player's vehicle now drives another graph. (A graph's own +4 is
     * itself in play, never the vehicle, so it is not part of the chain.) */
    MEM32(0x182000+8)=OWNER+0x2000;lamp(NODE,OWNER,13);assert(!s_car_groups);queue_permutation();
    MEM32(0x182000+8)=OWNER;MEM32(OWNER+4)=OWNER;assert(car_light_player(OWNER));
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
    {
        /* With every shared record taken, the driver's own tail lamp (found
         * through the native vehicle mask update, as in cruise) reclaims one;
         * a traffic car's tail lamp does not. */
        enum { VEHICLE=0x184000 };
        setup(1,0xffff0000u,.8f,2,0);driven(VEHICLE,1,1,3);full_native_queue(2);run();
        assert(MEM32(QUEUE+0x5810)==256 && s_car_groups==1 && s_car_group[0].priority);
        assert(s_car_group[0].record[0] && MEM32(s_car_group[0].record[0]+0x4c)==0xff2d0000u);
        queue_permutation();
        memset(s_car_switch,0,sizeof s_car_switch);
        setup(1,0xffff0000u,.8f,2,0);full_native_queue(2);run();
        assert(MEM32(QUEUE+0x5810)==256 && !s_car_groups);queue_permutation();
    }
    puts("PASS: primary-driver fill+beam survive full native/traffic queues, 24 frame/order changes, paired/env/displaced callbacks, identical-image aliases, multiple front colors and 256 occupied host groups; queue pointer permutation/order and activation/identity gates");
}
int main(void)
{
    unsigned kind;
    time_of_day_checks();
    direction_checks();axis_checks();current_axis_checks();fading_checks();
    grouping_checks();reset_lifecycle_checks();banked_pool_checks();crowded_queue_checks();scalability_checks();player_priority_checks();
    extended_queue_checks();distance_detail_checks();budget_checks();
    headlight_switch_checks();cruise_switch_checks();
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
        assert(MEMF(entry(0)+0x40)==(kind==1?4.4f:kind==2?6.8f:3.6f));
        if(kind==3) {
            /* A lane-wide field behind the car along the lamp's rearward
             * facing, not a 9m cube around the bumper. */
            assert(MEMF(entry(0)+0x44)==3.0f && MEMF(entry(0)+0x48)==6.0f);
            assert(fabsf(MEMF(entry(0)+0x30)-13)<1e-4f && fabsf(MEMF(entry(0)+0x34)-.4f)<1e-4f);
            assert(fabsf(MEMF(entry(0)+0x38)+6.5f)<1e-4f && fabsf(MEMF(entry(0)+0x28)-1)<1e-4f);
            setup(kind,0xffffffffu,.8f,1u<<kind,1.5707964f);run();
            assert(MEM32(QUEUE+0x5810)==1 && fabsf(MEMF(entry(0)+0x30)-14.5f)<1e-4f);
            assert(fabsf(MEMF(entry(0)+0x38)+8)<1e-4f && fabsf(MEMF(entry(0)+0x20)-1)<1e-4f);
        }
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
    native = native.replace("void sub_000DFE0E(void)", "void sub_000DFE0E_original(void)")
    source = native_projector.SOURCE.split("static unsigned submit", 1)[0]
    source = source.replace("memory[0x400000]", "memory[0x500000]")
    source = source.replace('void sub_001A842A(void) { assert(!"unexpected flare submission"); }',
                            'static unsigned flares; static uint32_t flare_color,flare_node;\n'
                            'static int glow_driver,flare_driver;\n'
                            'void mm3_glow_driver_scope(int driver) { glow_driver=driver; }\n'
                            'int mm3_exhaust_enabled(void) { return 0; }\n'
                            'void mm3_exhaust_lamp(uint32_t owner, uint32_t kind, const float transform[12]) '
                            '{ (void)owner; (void)kind; (void)transform; }\n'
                            'void sub_001A842A(void) { ++flares; flare_color=MEM32(flare_node+0x64); '
                            'flare_driver=glow_driver; g_esp+=16; }')
    source += CHECKS.replace("static unsigned flares;", "")
    with tempfile.TemporaryDirectory(prefix="mm3-car-lighting-") as temporary:
        folder = Path(temporary)
        shutil.copyfile(ROOT / "src/mm3_recomp_types.h", folder / "recomp_types.h")
        declarations = "\n".join(f"void {name}(void);" for name in native_projector.NATIVE)
        declarations += "\nvoid sub_000CF2DA_original(void);\nvoid sub_0022FDFD_original(void);\nvoid sub_000DFE0E_original(void);\n"
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
