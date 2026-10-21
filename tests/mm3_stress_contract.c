/* Execute the production stress adapter with controlled native-call seams.
 * This checks ABI/context safety and proof reporting; real road placement is
 * deliberately left to the separate game run. */
#define _CRT_SECURE_NO_WARNINGS
#include <assert.h>
#include <errno.h>
#include <float.h>
#include <fenv.h>
#include <stdio.h>
#include <windows.h>
#include "recomp_types.h"

ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp,g_seh_ebp,g_fs_base,g_eflags;
RECOMP_TLS double g_fp_stack[8];
RECOMP_TLS int g_fp_top,g_fp_cmp,g_df;
RECOMP_TLS uint16_t g_fp_cc,g_fp_control_word;
RECOMP_TLS RecompXmm g_xmm0,g_xmm1,g_xmm2,g_xmm3,g_xmm4,g_xmm5,g_xmm6,g_xmm7;

#include "../src/mm3_stress_debug.c"

static const uint32_t manager_va=0x620000,repository_va=0x630000;
static const uint32_t vector_va=0x640000,target_va=0x650000,target_vector_va=0x660000;
static unsigned resize_calls,activate_calls,ground_calls,reset_calls,disable_calls;
static int native_failure,ground_bad_position,ground_fail_second,expect_rejected;
static int rotated_placement,placement_idle,bad_stack_stage;
static uint32_t relocation_actor;
static unsigned relocation_stage;

static void clobber_context(void)
{
    g_eax=0xF1;g_ecx=0xF2;g_edx=0xF3;g_ebx=0xF4;g_esi=0xF5;g_edi=0xF6;
    g_ebp=0xF7;g_seh_ebp=0xF8;g_fs_base=0xF9;g_eflags=0xFA;g_df=1;
    for(unsigned i=0;i<8;++i)g_fp_stack[i]=100.0+i;
    g_fp_top=4;g_fp_cmp=-1;g_fp_cc=0x4500;g_fp_control_word=0x127F;
    RecompXmm *xmm[]={&g_xmm0,&g_xmm1,&g_xmm2,&g_xmm3,&g_xmm4,&g_xmm5,&g_xmm6,&g_xmm7};
    for(unsigned i=0;i<8;++i)memset(xmm[i],0xEF,sizeof *xmm[i]);
    fesetround(FE_DOWNWARD);errno=EDOM;SetLastError(987);
}

void sub_001507DA(void)
{
    unsigned count=g_eax;
    assert(g_edi==manager_va && count<=32 && !g_df && MEM32(g_esp)==0);
    ++resize_calls;
    if(!native_failure)MEM32(repository_va+0xB8)=vector_va+count*8;
    g_esp+=4;clobber_context();g_eax=native_failure?0:1;
}

void sub_0013B783(void)
{
    uint32_t actor=g_ecx-0x24;
    assert(MEM32(g_esp)==0 && MEM32(g_esp+4)==target_va && !g_df);
    assert(MEM32(actor)==0x37EE10 && MEM32(actor+0x2950)==0);
    ++activate_calls;
    MEM32(actor+0x2950)=2;
    MEM32(actor+0x9C)=target_va;
    uint32_t body=controller_body(MEM32(actor+0x28),0);
    /* Native acceptance and nearby placement are independent observations. */
    MEMF(body+0xC8)=activate_calls==1?30.0f:1000.0f;
    MEMF(body+0xCC)=10.0f;MEMF(body+0xD0)=40.0f;
    g_esp+=8;clobber_context();g_eax=1;
}

void sub_0013B8C7(void)
{
    assert(MEM32(g_esp)==0&&!g_df&&!relocation_stage);
    relocation_actor=MEM32(g_esp+4);
    assert(MEM32(relocation_actor)==0x37EE10&&MEM32(relocation_actor+0x9C)==target_va);
    ++reset_calls;relocation_stage=1;
    g_esp+=8+(bad_stack_stage==1?4:0);clobber_context();
}

void sub_0021BCA4(void)
{
    assert(MEM32(g_esp)==0&&!g_df&&relocation_stage==1);
    assert(g_ecx==MEM32(relocation_actor+0x28)&&MEM32(g_ecx)==0x384C80);
    ++disable_calls;relocation_stage=2;
    g_esp+=4+(bad_stack_stage==2?4:0);clobber_context();
}

void sub_00153194(void)
{
    uint32_t actor=g_edx,point=g_ecx,direction=g_eax;
    assert(MEM32(g_esp)==0&&!g_df&&relocation_stage==2&&actor==relocation_actor);
    assert(MEM32(actor+0x9C)==target_va);
    assert(fabsf(MEMF(direction)-(rotated_placement?1.0f:0.0f))<0.0001f);
    assert(MEMF(direction+4)==0);
    assert(fabsf(MEMF(direction+8)-(rotated_placement?0.0f:1.0f))<0.0001f);
    ++ground_calls;
    unsigned slot=ground_calls-1;
    float side=(slot&1u)?-3.0f:3.0f,behind=14.0f+9.0f*(float)(slot/2);
    assert(fabsf(MEMF(point)-(rotated_placement?-behind:side))<0.0001f);
    assert(MEMF(point+4)==10.0f);
    assert(fabsf(MEMF(point+8)-(rotated_placement?-side:-behind))<0.0001f);
    uint32_t body=controller_body(MEM32(actor+0x28),0);
    int accepted=!ground_fail_second||ground_calls!=2;
    if(accepted)for(unsigned i=0;i<3;++i)MEMF(body+0xC8+i*4)=MEMF(point+i*4);
    if(ground_bad_position)MEMF(body+0xC8)=NAN;
    if(placement_idle)MEM32(actor+0x2950)=0;
    relocation_stage=0;
    g_esp+=4+(bad_stack_stage==3?4:0);clobber_context();
    /* No boolean contract: successful relocation returns AL0 and a failed
     * move returns AL1, so proof must come from the actual body and mode. */
    g_eax=accepted?0:1;
}

int nv2a_d3d_set_readback_poll_us(int us){return us>=0&&us<=1000;}
int nv2a_d3d_get_readback_poll_us(void){return 250;}
int nv2a_d3d_set_readback_fence(int enabled){return enabled==0||enabled==1;}
int nv2a_d3d_get_readback_fence(void){return 0;}
int mcpx_apu_set_hires_wait_enabled(int enabled){return enabled==0||enabled==1;}
int mcpx_apu_get_hires_wait_enabled(void){return 0;}
static int native_apu_mode, native_apu_set_calls, native_apu_reject_enable;
int mm3_apu_mmio_set_enabled(int enabled){
    ++native_apu_set_calls;
    if((enabled!=0&&enabled!=1)||(enabled&&native_apu_reject_enable))return 0;
    native_apu_mode=enabled;return 1;
}
int mm3_apu_mmio_get_enabled(void){return native_apu_mode;}
void nv2a_d3d_set_render_scale(float scale){(void)scale;}
float nv2a_d3d_get_render_scale(void){return 4.0f;}
void mm3_collision_profile_snapshot(MM3CollisionProfileSnapshot *out){memset(out,0,sizeof *out);}

static uint32_t initialize_car(uint32_t base)
{
    uint32_t controller=base+0x3000,vehicle=base+0x4000,car=base+0x4100;
    uint32_t simulation=base+0x4200,body=base+0x4300;
    MEM32(controller)=0x384C80;MEM32(controller+0x19C)=vehicle;
    MEM32(controller+0x1AC)=0x670000;
    MEM32(vehicle)=0x383768;MEM32(vehicle+8)=car;
    MEM32(car+0x3C)=simulation;MEM32(simulation+4)=body;
    MEM32(car+0x40)=base+0x4600;MEM32(base+0x4600)=base+0x4700;
    MEMF(body+0xCC)=10.0f;
    MEMF(body+0xC4)=1.0f;
    return controller;
}

static void initialize_fixture(void)
{
    memset((void *)g_xbox_mem_offset,0,0x800000);
    MEM32(0x3C5780)=0x600000;MEM32(0x60E5D8)=manager_va;
    MEM32(manager_va+4)=repository_va;
    MEM32(repository_va+0xB4)=MEM32(repository_va+0xB8)=vector_va;
    MEM32(repository_va+0x104)=target_vector_va;MEM32(repository_va+0x108)=target_vector_va+4;
    MEM32(target_vector_va)=target_va;MEM32(target_va)=0x37FAB0;
    MEM32(target_va+0x28)=initialize_car(0x200000);
    for(unsigned i=0;i<32;++i){
        uint32_t actor=0x100000+i*0x6000;
        MEM32(vector_va+i*8)=actor;MEM32(actor)=0x37EE10;
        MEM32(actor+0x24)=0x37ED94;MEM32(actor+0x2954)=0x37ED48;
        MEM32(actor+0x28)=initialize_car(actor);
        MEM32(actor+0x94)=0x500000;MEM32(actor+0x294C)=0x510000;
    }
    MEM32(0x510000)=0x511000;
    MEM32(0x384CFC)=0x21BCA4;
    resize_calls=activate_calls=ground_calls=reset_calls=disable_calls=0;native_failure=ground_bad_position=0;
    ground_fail_second=1;
    expect_rejected=rotated_placement=placement_idle=bad_stack_stage=0;
    relocation_actor=0;relocation_stage=0;
    sampled=0;police_valid=0;completed=serial=0;InterlockedExchange(&requested,-1);
}

static void check_saved_context_call(unsigned count)
{
    uint32_t expected[]={1,2,3,0x780000,5,6,7,8,9,10,11};
    g_eax=1;g_ecx=2;g_edx=3;g_esp=expected[3];g_ebx=5;g_esi=6;g_edi=7;
    g_ebp=8;g_seh_ebp=9;g_fs_base=10;g_eflags=11;g_df=1;
    for(unsigned i=0;i<8;++i)g_fp_stack[i]=i+0.5;
    g_fp_top=2;g_fp_cmp=1;g_fp_cc=0x4000;g_fp_control_word=0x37F;
    RecompXmm *xmm[]={&g_xmm0,&g_xmm1,&g_xmm2,&g_xmm3,&g_xmm4,&g_xmm5,&g_xmm6,&g_xmm7};
    for(unsigned i=0;i<8;++i)memset(xmm[i],(int)i,sizeof *xmm[i]);
    fesetround(FE_TONEAREST);unsigned mxcsr=_mm_getcsr();errno=ERANGE;SetLastError(123);
    assert(set_police(count,MEM32(target_va+0x28))==(!native_failure&&!expect_rejected));
    uint32_t actual[]={g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp,g_seh_ebp,g_fs_base,g_eflags};
    assert(!memcmp(actual,expected,sizeof actual));
    assert(g_df==1&&g_fp_top==2&&g_fp_cmp==1&&g_fp_cc==0x4000&&g_fp_control_word==0x37F);
    for(unsigned i=0;i<8;++i){
        assert(g_fp_stack[i]==i+0.5);
        unsigned char expected_xmm[16];memset(expected_xmm,(int)i,sizeof expected_xmm);
        assert(!memcmp(xmm[i],expected_xmm,sizeof expected_xmm));
    }
    assert(errno==ERANGE&&GetLastError()==123&&_mm_getcsr()==mxcsr&&fegetround()==FE_TONEAREST);
}

int main(void)
{
    /* The real integer-only hook is inert before stress sampling, saturates,
     * and preserves every guest GPR/FP slot plus host errors/FP control. */
    audio_pool_diagnostics_enabled=0;audio_pool_factory_rejections=0;
    mm3_stress_audio_pool_empty();assert(!audio_pool_factory_rejections);
    audio_pool_diagnostics_enabled=1;
    uint32_t host_before[]={g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp,g_seh_ebp,g_fs_base,g_eflags};
    double fp_before[8];memcpy(fp_before,g_fp_stack,sizeof fp_before);
    fenv_t env_before,env_after;fegetenv(&env_before);
    unsigned csr_before=_mm_getcsr();errno=ERANGE;SetLastError(123);
    mm3_stress_audio_pool_empty();assert(audio_pool_factory_rejections==1);
    assert(errno==ERANGE&&GetLastError()==123&&_mm_getcsr()==csr_before);
    fegetenv(&env_after);assert(!memcmp(&env_before,&env_after,sizeof env_before));
    uint32_t host_after[]={g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp,g_seh_ebp,g_fs_base,g_eflags};
    assert(!memcmp(host_before,host_after,sizeof host_before)&&!memcmp(fp_before,g_fp_stack,sizeof fp_before));
    audio_pool_factory_rejections=0x7FFFFFFE;
    mm3_stress_audio_pool_empty();mm3_stress_audio_pool_empty();assert(audio_pool_factory_rejections==0x7FFFFFFF);
    audio_pool_factory_rejections=0;audio_pool_diagnostics_enabled=0;
    void *memory=VirtualAlloc(NULL,0x800000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(memory);g_xbox_mem_offset=(ptrdiff_t)memory;
    initialize_fixture();
    uint32_t audio_manager=0x690000,audio_owner,audio_active,audio_free;
    assert(!sample_audio_pool(&audio_owner,&audio_active,&audio_free));
    MEM32(0x407628)=audio_manager;MEM32(audio_manager+8)=17;MEM32(audio_manager+0x1C)=47;
    uint32_t audio_copy[8];memcpy(audio_copy,(void *)XBOX_PTR(audio_manager),sizeof audio_copy);
    assert(sample_audio_pool(&audio_owner,&audio_active,&audio_free)&&audio_owner==audio_manager&&audio_active==17&&audio_free==47);
    assert(!memcmp(audio_copy,(void *)XBOX_PTR(audio_manager),sizeof audio_copy));
    MEM32(audio_manager+8)=65;assert(!sample_audio_pool(&audio_owner,&audio_active,&audio_free));
    MEM32(audio_manager+8)=18;assert(!sample_audio_pool(&audio_owner,&audio_active,&audio_free));
    MEM32(audio_manager+8)=17;MEM32(audio_manager+0x1C)=65;assert(!sample_audio_pool(&audio_owner,&audio_active,&audio_free));
    MEM32(0x407628)=0xFFFFFFF0;assert(!sample_audio_pool(&audio_owner,&audio_active,&audio_free));
    MEM32(0x407628)=0x8000000;assert(!sample_audio_pool(&audio_owner,&audio_active,&audio_free));
    MEM32(0x407628)=audio_manager;MEM32(audio_manager+8)=64;MEM32(audio_manager+0x1C)=0;
    DWORD old_protection;
    assert(VirtualProtect((void *)XBOX_PTR(audio_manager),0x1000,PAGE_NOACCESS,&old_protection));
    assert(!sample_audio_pool(&audio_owner,&audio_active,&audio_free));
    assert(VirtualProtect((void *)XBOX_PTR(audio_manager),0x1000,old_protection,&old_protection));
    assert(sample_audio_pool(&audio_owner,&audio_active,&audio_free)&&audio_active==64&&!audio_free);
    check_saved_context_call(2);
    assert(resize_calls==1&&activate_calls==2&&activation_attempted==2&&activation_accepted==2);
    assert(reset_calls==2&&disable_calls==2&&ground_calls==2&&placement_attempted==2&&placement_accepted==1&&placement_verified==1);
    uint32_t player=MEM32(target_va+0x28),body=controller_body(player,0);
    mm3_stress_tick(player,body);
    assert(police_active_count==2&&police_body_count==2&&police_nearby_count==1);
    assert(audio_pool_diagnostics_enabled&&audio_pool_zero_free_samples==1&&!audio_pool_invalid_samples);
    assert(strstr(snapshot,"\"audio_pool\":{\"available\":true")&&strstr(snapshot,"\"active_wrappers\":64,\"free_resources\":0"));
    mm3_stress_audio_pool_empty();assert(audio_pool_factory_rejections==1);
    audio_pool_zero_free_samples=UINT32_MAX;increment_sample_count(&audio_pool_zero_free_samples);
    assert(audio_pool_zero_free_samples==UINT32_MAX);
    puts(snapshot);

    initialize_fixture();ground_fail_second=0;check_saved_context_call(12);
    assert(reset_calls==12&&disable_calls==12&&ground_calls==12&&placement_accepted==12&&placement_verified==12);
    player=MEM32(target_va+0x28);mm3_stress_tick(player,controller_body(player,0));
    assert(police_active_count==12&&police_nearby_count==12);
    uint32_t original_ids[32];memcpy(original_ids,police_ids,sizeof original_ids);
    activate_calls=ground_calls=reset_calls=disable_calls=0;
    check_saved_context_call(12);
    assert(resize_calls==1&&!activate_calls&&reset_calls==12&&disable_calls==12&&ground_calls==12);
    assert(!memcmp(original_ids,police_ids,sizeof original_ids));
    expect_rejected=1;check_saved_context_call(0);
    assert(shrink_rejected&&resize_calls==1&&police_count==12&&police_active_count==12);
    assert(!activation_attempted&&!placement_attempted);
    check_saved_context_call(6);
    assert(shrink_rejected&&resize_calls==1&&!memcmp(original_ids,police_ids,sizeof original_ids));

    /* All16 bounded slots stay in two centred columns, with6m lateral and
     * 9m longitudinal spacing. The formation follows the player's heading. */
    initialize_fixture();ground_fail_second=0;rotated_placement=1;
    player=MEM32(target_va+0x28);body=controller_body(player,0);
    MEMF(body+0xBC)=1.0f;MEMF(body+0xC4)=0.0f;
    check_saved_context_call(16);
    assert(reset_calls==16&&disable_calls==16&&ground_calls==16&&placement_attempted==16&&
           placement_accepted==16&&placement_verified==16);
    for(unsigned i=0;i<16;++i){
        uint32_t actor=police_ids[i],placed_body=controller_body(MEM32(actor+0x28),0);
        assert(fabsf(MEMF(placed_body+0xC8)-(-14.0f-9.0f*(float)(i/2)))<0.0001f);
        assert(fabsf(MEMF(placed_body+0xD0)-((i&1u)?3.0f:-3.0f))<0.0001f);
    }

    initialize_fixture();check_saved_context_call(0);
    assert(!resize_calls&&!activate_calls&&!placement_attempted&&!shrink_rejected);

    initialize_fixture();MEM32(target_vector_va+4)=target_va+0x100;
    MEM32(target_va+0x100)=0x37FAB0;MEM32(target_va+0x128)=MEM32(target_va+0x28);
    MEM32(repository_va+0x108)=target_vector_va+8;
    check_saved_context_call(2);
    assert(resize_calls==1&&!activate_calls&&!activation_attempted&&!activation_target);

    initialize_fixture();MEM32(0x104000)=0;check_saved_context_call(2);
    assert(activate_calls==1&&activation_attempted==1&&activation_accepted==1);

    initialize_fixture();native_failure=1;check_saved_context_call(2);
    assert(!activate_calls&&!activation_attempted);

    initialize_fixture();ground_bad_position=1;check_saved_context_call(1);
    assert(activate_calls==1&&reset_calls==1&&disable_calls==1&&ground_calls==1&&!placement_accepted&&!placement_verified);

    initialize_fixture();placement_idle=1;check_saved_context_call(1);
    assert(ground_calls==1&&!placement_accepted&&!placement_verified);

    initialize_fixture();MEM32(0x384CFC)=0;check_saved_context_call(1);
    assert(activate_calls==1&&!reset_calls&&!disable_calls&&!ground_calls&&!placement_attempted);

    for(int stage=1;stage<=3;++stage){
        initialize_fixture();bad_stack_stage=stage;check_saved_context_call(2);
        assert(reset_calls==1&&disable_calls==(stage>=2)&&ground_calls==(stage>=3));
        assert(placement_attempted==1&&!placement_accepted&&!placement_verified);
    }

    initialize_fixture();MEM32(repository_va+0xB8)=vector_va+32*8;
    for(unsigned i=0;i<32;++i){
        uint32_t actor=MEM32(vector_va+i*8),car_body=controller_body(MEM32(actor+0x28),0);
        MEM32(actor+0x2950)=0xFFFFFFFFu;
        MEMF(car_body+0xC8)=-FLT_MAX;MEMF(car_body+0xCC)=FLT_MAX;MEMF(car_body+0xD0)=-FLT_MAX;
    }
    mm3_stress_tick(MEM32(target_va+0x28),controller_body(MEM32(target_va+0x28),0));
    assert(police_count==32&&police_body_count==32&&!police_nearby_count);
    assert(strstr(snapshot,"\"ok\":true")&&strlen(snapshot)<MM3_STRESS_REPLY_CAPACITY);
    puts(snapshot);
    puts("PASS: activation/full native respawn ABI and order, position/mode proof independent of AL, complete context restoration, invalid placement rejection and32-actor snapshot capacity");
    char reply[MM3_STRESS_REPLY_CAPACITY];
    assert(mm3_stress_command("stress audio_wait 1",reply,sizeof reply));
    assert(strstr(reply,"\"ok\":true")&&strstr(reply,"\"requested_audio_wait\":1")&&strstr(reply,"\"effective_audio_wait\":0"));
    assert(mm3_stress_command("stress audio_wait 2",reply,sizeof reply)&&strstr(reply,"\"ok\":false"));
    assert(mm3_stress_command("stress native_apu 1",reply,sizeof reply));
    assert(strstr(reply,"\"ok\":true")&&strstr(reply,"\"requested_native_apu\":1")&&strstr(reply,"\"effective_native_apu\":1"));
    int before_query=native_apu_set_calls;
    assert(mm3_stress_command("stress native_apu",reply,sizeof reply)&&strstr(reply,"\"effective_native_apu\":1"));
    assert(native_apu_set_calls==before_query);
    assert(mm3_stress_command("stress native_apu 0",reply,sizeof reply)&&strstr(reply,"\"effective_native_apu\":0"));
    native_apu_reject_enable=1;
    assert(mm3_stress_command("stress native_apu 1",reply,sizeof reply)&&strstr(reply,"\"ok\":false")&&strstr(reply,"\"effective_native_apu\":0"));
    assert(mm3_stress_command("stress native_apu 2",reply,sizeof reply)&&strstr(reply,"\"ok\":false")&&!native_apu_mode);
    before_query=native_apu_set_calls;
    assert(mm3_stress_command("stress native_apu 1 extra",reply,sizeof reply)&&strstr(reply,"\"ok\":false"));
    assert(mm3_stress_command("stress native_apu junk",reply,sizeof reply)&&strstr(reply,"\"ok\":false"));
    assert(native_apu_set_calls==before_query&&!native_apu_mode);
    VirtualFree(memory,0,MEM_RELEASE);return 0;
}
