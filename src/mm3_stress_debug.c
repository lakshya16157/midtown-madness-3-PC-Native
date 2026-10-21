/* Opt-in local test control. Native calls and memory sampling run exclusively
 * on the primary player update thread. The pipe only reads copied snapshots. */
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <errno.h>
#include <fenv.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <xmmintrin.h>
#include "recomp_types.h"
#include "mm3_collision_profile.h"
#include "mm3_apu_mmio.h"
#include "mm3_stress_debug.h"

extern void sub_001507DA(void);
extern void sub_0013B783(void);
extern void sub_0013B8C7(void);
extern void sub_0021BCA4(void);
extern void sub_00153194(void);
extern int nv2a_d3d_set_readback_poll_us(int us);
extern int nv2a_d3d_get_readback_poll_us(void);
extern int nv2a_d3d_set_readback_fence(int enabled);
extern int nv2a_d3d_get_readback_fence(void);
extern int mcpx_apu_set_hires_wait_enabled(int enabled);
extern int mcpx_apu_get_hires_wait_enabled(void);
extern void nv2a_d3d_set_render_scale(float scale);
extern float nv2a_d3d_get_render_scale(void);
static SRWLOCK state_lock = SRWLOCK_INIT;
static volatile LONG requested = -1;
static unsigned serial, completed;
static int native_ok, running, shrink_rejected;
static DWORD sampled;
static char snapshot[MM3_STRESS_REPLY_CAPACITY] = "{\"ok\":false,\"error\":\"no player sample\"}";
static uint32_t police_ids[32];
static uint32_t police_active_ids[32];
static unsigned police_count, police_active_count;
static unsigned police_body_count, police_nearby_count;
static unsigned activation_attempted, activation_accepted;
static unsigned placement_attempted, placement_accepted, placement_verified;
static uint32_t activation_target;
static struct { uint32_t body, mode; float xyz[3]; } police_positions[32];
static int police_valid;
/* Diagnostic only: enabled once the existing opt-in player sampler runs.
 * Integer atomics have no guest/FP/error-state effects, including in a factory
 * caller on another guest thread. Counts saturate instead of wrapping. */
static volatile LONG audio_pool_diagnostics_enabled;
static volatile LONG audio_pool_factory_rejections;
static uint32_t audio_pool_zero_free_samples, audio_pool_invalid_samples;

void mm3_stress_audio_pool_empty(void)
{
    if (audio_pool_diagnostics_enabled) {
        LONG value=InterlockedCompareExchange(&audio_pool_factory_rejections,0,0);
        while (value<0x7FFFFFFF) {
            LONG previous=InterlockedCompareExchange(&audio_pool_factory_rejections,value+1,value);
            if (previous==value) break;
            value=previous;
        }
    }
}

static void increment_sample_count(uint32_t *value)
{
    if (*value!=UINT32_MAX) ++*value;
}

static int readable(uint32_t address, size_t bytes)
{
    MEMORY_BASIC_INFORMATION info;
    uintptr_t p = XBOX_PTR(address);
    if (address < 0x10000 || address >= 0x08000000 || bytes > 0x08000000u-address)
        return 0;
    return VirtualQuery((void *)p, &info, sizeof info) && info.State == MEM_COMMIT &&
        !(info.Protect & (PAGE_NOACCESS|PAGE_GUARD)) &&
        p+bytes <= (uintptr_t)info.BaseAddress+info.RegionSize;
}

/* Retail17BC0C creates64 spatial-output resources. 17BB4B searches the
 * active list at+4/count+8 before acquiring from deque+0C/count+1C;
 * 17BAFD returns the resource when its shared wrapper refcount reaches0.
 * Read only the validated owner fields, never traverse a transient deque. */
static int sample_audio_pool(uint32_t *manager,uint32_t *active,uint32_t *free_count)
{
    *manager=*active=*free_count=0;
    if (!readable(0x407628u,4)) return 0;
    uint32_t owner=MEM32(0x407628u);
    if (!readable(owner,0x20)) return 0;
    uint32_t active_value=MEM32(owner+8),free_value=MEM32(owner+0x1C);
    if (active_value>64 || free_value>64 || active_value+free_value>64) return 0;
    *manager=owner;*active=active_value;*free_count=free_value;
    return 1;
}

static uint32_t police_manager(void)
{
    uint32_t module = MEM32(0x3C5780u), manager;
    if (!readable(module, 0xE670)) return 0;
    manager = MEM32(module+0xE5D8);
    return readable(manager, 0x10) ? manager : 0;
}

/* The police factory 13BB36 creates a native 21B40A controller at actor+28.
 * 21F1CE constructs its +19C vehicle interface with vtable 383768 (1D735B).
 * This is the same car/simulation/body chain used by the player ground reset. */
static uint32_t controller_body(uint32_t controller, int require_ground_reset)
{
    uint32_t vehicle, car, simulation, body, wheels;
    if (!readable(controller,0x7A0)) return 0;
    vehicle=MEM32(controller+0x19C);
    if (!readable(vehicle,0x2C) || MEM32(vehicle)!=0x00383768u) return 0;
    car=MEM32(vehicle+8);
    if (!readable(car,0x48)) return 0;
    simulation=MEM32(car+0x3C);
    if (!readable(simulation,8)) return 0;
    body=MEM32(simulation+4);
    if (!readable(body,0x1C4)) return 0;
    if (require_ground_reset) {
        wheels=MEM32(car+0x40);
        if (!readable(MEM32(controller+0x1AC),0x1504C) ||
            !readable(wheels,4) || !readable(MEM32(wheels),0x100)) return 0;
    }
    return body;
}

/* 14FAFA registers one 151114 wrapper per native player in repo+100. Its
 * 37FAB0 vtable +0C is 150DE5, the position interface expected by 1522C2.
 * Reject ambiguous matches instead of treating the native player as that AI. */
static uint32_t player_ai_target(uint32_t manager, uint32_t player)
{
    uint32_t repository=MEM32(manager+4), begin, end, found=0;
    if (!controller_body(player,0) || !readable(repository,0x10C)) return 0;
    begin=MEM32(repository+0x104);end=MEM32(repository+0x108);
    if (end<begin || (end-begin)%4 || (end-begin)/4>32 ||
        (end>begin && !readable(begin,end-begin))) return 0;
    for (uint32_t entry=begin;entry<end;entry+=4) {
        uint32_t wrapper=MEM32(entry);
        if (!readable(wrapper,0x70) || MEM32(wrapper)!=0x0037FAB0u ||
            MEM32(wrapper+0x28)!=player) continue;
        if (found) return 0;
        found=wrapper;
    }
    return found;
}

/* Used only inside set_police's complete guest/host context save on the game
 * thread. 13B783 takes ECX=actor+24 and one stack argument, target AI wrapper;
 * ret4. It initializes pursuit through 1522C2 and its native state callbacks.
 * 13B979 -> 13BA0C -> 153482 chooses road placement and invokes 21F806/21ADF3.
 * Native acceptance does not guarantee that this road search found a point. */
static void activate_idle_police(uint32_t manager,uint32_t player)
{
    uint32_t candidates[32];unsigned candidate_count=0;
    activation_target=player_ai_target(manager,player);
    if (!activation_target) return;
    for (unsigned i=0;i<police_count;++i) {
        uint32_t actor=police_ids[i], road, tuning;
        if (!readable(actor,0x298C) || MEM32(actor)!=0x0037EE10u ||
            MEM32(actor+0x24)!=0x0037ED94u ||
            MEM32(actor+0x2954)!=0x0037ED48u || MEM32(actor+0x2950) ||
            !controller_body(MEM32(actor+0x28),1)) continue;
        tuning=MEM32(actor+0x94);road=MEM32(actor+0x294C);
        if (!readable(tuning,0xC0) || !readable(road,4) ||
            !readable(MEM32(road),8)) continue;
        candidates[candidate_count++]=actor;
    }
    for (unsigned i=0;i<candidate_count;++i) {
        uint32_t call_sp=g_esp;
        ++activation_attempted;
        g_ecx=candidates[i]+0x24;g_df=0;
        PUSH32(g_esp,activation_target);PUSH32(g_esp,0);
        sub_0013B783();
        if (g_eax&0xFF) ++activation_accepted;
        if (g_esp!=call_sp) break; /* stop if the verified ABI ever disagrees */
    }
}

/* The native pursuit spawn deliberately selects distant roads. Arrange at most
 * 16 active cars behind the player using its full custom-point respawn path:
 * 13BA0C calls 13B8C7(actor), then 153482 calls controller+7C (21BCA4),
 * then 153194(EDX=actor,ECX=point,EAX=forward). The latter performs the native
 * reset/ground fallback, tuning and pursuit-origin rebuild. It has no boolean
 * result contract; only the resulting active body position proves placement.
 * No actor transform, execution mode or physics memory is written directly. */
static void place_active_police(uint32_t player)
{
    uint32_t player_body=controller_body(player,0);
    if (!activation_target || !player_body) return;
    float origin[]={MEMF(player_body+0xC8),MEMF(player_body+0xCC),MEMF(player_body+0xD0)};
    float heading=atan2f(MEMF(player_body+0xBC),MEMF(player_body+0xC4));
    if (!isfinite(origin[0]) || !isfinite(origin[1]) || !isfinite(origin[2]) || !isfinite(heading))return;
    float forward_x=sinf(heading),forward_z=cosf(heading);
    unsigned slot=0;
    for (unsigned i=0;i<police_count && slot<16;++i) {
        uint32_t actor=police_ids[i],controller=MEM32(actor+0x28);
        uint32_t body=controller_body(controller,1);
        if (!body || !MEM32(actor+0x2950) || MEM32(controller)!=0x00384C80u ||
            !readable(0x00384C80u,0x80) || MEM32(0x00384CFCu)!=0x0021BCA4u ||
            !readable(MEM32(actor+0x94),0xC0) ||
            MEM32(actor+0x9C)!=activation_target)continue;
        /* Columns 6 m apart keep native reset points near the road centre on
         * Washington's narrow streets. Longitudinal spacing remains 9 m. */
        float behind=14.0f+9.0f*(float)(slot/2),side=(slot&1u)?-3.0f:3.0f;
        float xyz[]={origin[0]-forward_x*behind+forward_z*side,origin[1],
                     origin[2]-forward_z*behind-forward_x*side};
        uint32_t call_sp=g_esp,point=call_sp-0x100,direction=point+0x10;
        ++slot;++placement_attempted;
        for(unsigned j=0;j<3;++j)MEMF(point+j*4)=xyz[j];
        MEMF(direction)=forward_x;MEMF(direction+4)=0;MEMF(direction+8)=forward_z;
        g_esp=point-16;g_df=0;
        PUSH32(g_esp,actor);PUSH32(g_esp,0);
        sub_0013B8C7(); /* ret4 */
        if(g_esp!=point-16){g_esp=call_sp;break;}
        g_ecx=controller;g_df=0;PUSH32(g_esp,0);
        sub_0021BCA4(); /* native controller+7C, ret0 */
        if(g_esp!=point-16){g_esp=call_sp;break;}
        g_edx=actor;g_ecx=point;g_eax=direction;g_df=0;PUSH32(g_esp,0);
        sub_00153194(); /* ret0; native return AL is not placement acceptance */
        int stack_ok=g_esp==point-16;
        g_esp=call_sp;
        if(!stack_ok)break;
        body=controller_body(controller,0);
        if(!MEM32(actor+0x2950) || !body || !isfinite(MEMF(body+0xC8)) ||
            !isfinite(MEMF(body+0xCC)) || !isfinite(MEMF(body+0xD0)))continue;
        double px=(double)MEMF(body+0xC8)-xyz[0],pz=(double)MEMF(body+0xD0)-xyz[2];
        if(px*px+pz*pz>5.0*5.0 || fabs((double)MEMF(body+0xCC)-xyz[1])>50.0)continue;
        ++placement_accepted;
        double dx=(double)MEMF(body+0xC8)-origin[0],dz=(double)MEMF(body+0xD0)-origin[2];
        if(dx*dx+dz*dz>150.0*150.0 || fabs((double)MEMF(body+0xCC)-origin[1])>50.0)continue;
        ++placement_verified;
    }
}

static int sample_police(void)
{
    uint32_t manager = police_manager(), repository, begin, end;
    police_count = police_active_count = 0;
    memset(police_positions,0,sizeof police_positions);
    if (!manager) return 0;
    repository = MEM32(manager+4);
    if (!readable(repository, 0xC0)) return 0;
    begin = MEM32(repository+0xB4); end = MEM32(repository+0xB8);
    if (end < begin || (end-begin)%8 || (end-begin)/8 > 32) return 0;
    if (end > begin && !readable(begin, end-begin)) return 0;
    for (uint32_t entry=begin; entry<end; entry+=8) {
        uint32_t actor = MEM32(entry);
        if (!readable(actor, 0x298C) || MEM32(actor) != 0x0037EE10u) return 0;
        unsigned index=police_count++;
        police_ids[index] = actor;
        police_positions[index].mode=MEM32(actor+0x2950);
        uint32_t body=controller_body(MEM32(actor+0x28),0);
        if (body && isfinite(MEMF(body+0xC8)) && isfinite(MEMF(body+0xCC)) &&
            isfinite(MEMF(body+0xD0))) {
            police_positions[index].body=body;
            for (unsigned j=0;j<3;++j) police_positions[index].xyz[j]=MEMF(body+0xC8+j*4);
        }
        /* Retail 14F351 excludes mode zero from its police spatial query;
         * 15140F sets this native execution mode, 151C4C resets it to zero.
         * This proves native activity, independently of camera visibility. */
        if (MEM32(actor+0x2950)) police_active_ids[police_active_count++]=actor;
    }
    return 1;
}

static int sample_traffic_pool(void)
{
    uint32_t manager=police_manager(),repository,begin,end;
    if(!manager)return -1;
    repository=MEM32(manager+4);
    if(!readable(repository,0x60))return -1;
    /* Retail 1503F4 registers repository+40 as "ambientcars". */
    begin=MEM32(repository+0x54);end=MEM32(repository+0x58);
    if(end<begin||(end-begin)%8||(end-begin)/8>512)return -1;
    if(end>begin&&!readable(begin,end-begin))return -1;
    for(uint32_t entry=begin;entry<end;entry+=8)
        if(!readable(MEM32(entry),4))return -1;
    return (int)((end-begin)/8);
}

static int set_police(unsigned count,uint32_t player)
{
    uint32_t manager = police_manager();
    activation_attempted=activation_accepted=0;activation_target=0;
    placement_attempted=placement_accepted=placement_verified=0;
    shrink_rejected=0;
    if (!manager || !sample_police() || !readable(g_esp-0x4000, 0x4000)) return 0;
    /* Retail pool removal does not establish a safe active-car teardown here.
     * Keep active test cars for same-total reuse until this process exits. */
    if (count<police_count && police_active_count) {shrink_rejected=1;return 0;}
    int same_total=count==police_count;
    uint32_t regs[] = {g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp,g_seh_ebp,g_fs_base,g_eflags};
    double fp[8]; memcpy(fp,g_fp_stack,sizeof fp);
    int top=g_fp_top, cmp=g_fp_cmp, df=g_df;
    uint16_t cc=g_fp_cc, control=g_fp_control_word;
    RecompXmm xmm[]={g_xmm0,g_xmm1,g_xmm2,g_xmm3,g_xmm4,g_xmm5,g_xmm6,g_xmm7};
    int saved_errno=errno, success;
    DWORD saved_error=GetLastError();
    unsigned saved_mxcsr=_mm_getcsr();
    fenv_t host_fp; int host_valid=feholdexcept(&host_fp)==0;
    g_esp -= 0x100;
    success=1;
    if(!same_total) {
        g_edi=manager; g_eax=count; g_df=0; PUSH32(g_esp,0);
        sub_001507DA();
        success=(g_eax&0xFF)!=0;
    }
    if (success && sample_police() && police_count==count) {
        activate_idle_police(manager,player);
        place_active_police(player);
    }
    g_eax=regs[0];g_ecx=regs[1];g_edx=regs[2];g_esp=regs[3];g_ebx=regs[4];
    g_esi=regs[5];g_edi=regs[6];g_ebp=regs[7];g_seh_ebp=regs[8];g_fs_base=regs[9];g_eflags=regs[10];
    memcpy(g_fp_stack,fp,sizeof fp);g_fp_top=top;g_fp_cmp=cmp;g_df=df;g_fp_cc=cc;g_fp_control_word=control;
    g_xmm0=xmm[0];g_xmm1=xmm[1];g_xmm2=xmm[2];g_xmm3=xmm[3];
    g_xmm4=xmm[4];g_xmm5=xmm[5];g_xmm6=xmm[6];g_xmm7=xmm[7];
    if(host_valid)fesetenv(&host_fp);_mm_setcsr(saved_mxcsr);errno=saved_errno;SetLastError(saved_error);
    return success;
}

static int appendf(char *text,size_t capacity,size_t *used,const char *format,...)
{
    va_list args;int count;
    if (*used>=capacity) return 0;
    va_start(args,format);
    count=vsnprintf(text+*used,capacity-*used,format,args);
    va_end(args);
    if (count<0 || (size_t)count>=capacity-*used) return 0;
    *used+=(size_t)count;
    return 1;
}

void mm3_stress_tick(uint32_t player, uint32_t body)
{
    if (running || !readable(body,0xD4)) return;
    DWORD now=GetTickCount();
    LONG request=InterlockedCompareExchange(&requested,-1,-1);
    if (request<0 && now-sampled<100) return;
    int saved_errno=errno;
    DWORD saved_error=GetLastError();
    unsigned saved_mxcsr=_mm_getcsr();
    fenv_t host_fp;int host_valid=feholdexcept(&host_fp)==0;
    running=1;
    AcquireSRWLockExclusive(&state_lock);
    request=InterlockedCompareExchange(&requested,-1,-1);
    if(request>=0) {
        native_ok=set_police((unsigned)request,player);
        completed=serial;
        InterlockedExchange(&requested,-1);
    }
    police_valid=sample_police();
    InterlockedExchange(&audio_pool_diagnostics_enabled,1);
    uint32_t audio_owner,audio_active,audio_free;
    int audio_valid=sample_audio_pool(&audio_owner,&audio_active,&audio_free);
    if (!audio_valid) increment_sample_count(&audio_pool_invalid_samples);
    else if (!audio_free) increment_sample_count(&audio_pool_zero_free_samples);
    LONG audio_rejections=InterlockedCompareExchange(&audio_pool_factory_rejections,0,0);
    LARGE_INTEGER qpc={0},frequency={0};
    MM3CollisionProfileSnapshot collision;
    mm3_collision_profile_snapshot(&collision);
    if(!QueryPerformanceCounter(&qpc)||!QueryPerformanceFrequency(&frequency)||frequency.QuadPart<=0) {
        strcpy_s(snapshot,sizeof snapshot,"{\"ok\":false,\"error\":\"performance clock unavailable\"}");
        goto done;
    }
    float x=MEMF(body+0xC8),y=MEMF(body+0xCC),z=MEMF(body+0xD0);
    float heading=atan2f(MEMF(body+0xBC),MEMF(body+0xC4));
    float vx=MEMF(body+0x20),vy=MEMF(body+0x24),vz=MEMF(body+0x28);
    float speed=sqrtf(vx*vx+vy*vy+vz*vz);
    int player_position_valid=isfinite(x)&&isfinite(y)&&isfinite(z);
    char ids[512]="", active_ids[512]="", nearby_ids[512]="", actors[6144]="";
    char count[32]="null", active_count[32]="null", body_count[32]="null", nearby_count[32]="null", contacts[32]="null", effects[32]="null";
    char traffic[32]="null",peds[32]="null",traffic_count[32]="null";
    int ambient=sample_traffic_pool();
    if(ambient>=0)snprintf(traffic_count,sizeof traffic_count,"%d",ambient);
    if(isfinite(MEMF(0x407C64)))snprintf(traffic,sizeof traffic,"%.8g",MEMF(0x407C64));
    if(isfinite(MEMF(0x407C60)))snprintf(peds,sizeof peds,"%.8g",MEMF(0x407C60));
    size_t n=0;
    police_body_count=police_nearby_count=0;
    if(police_valid) {
        snprintf(count,sizeof count,"%u",police_count);
        for(unsigned i=0;i<police_count;++i)
            if(!appendf(ids,sizeof ids,&n,"%s%u",i?",":"",police_ids[i]))goto snapshot_overflow;
        snprintf(active_count,sizeof active_count,"%u",police_active_count);
        n=0;
        for(unsigned i=0;i<police_active_count;++i)
            if(!appendf(active_ids,sizeof active_ids,&n,"%s%u",i?",":"",police_active_ids[i]))goto snapshot_overflow;
        size_t actor_used=0,nearby_used=0;
        for(unsigned i=0;i<police_count;++i) {
            uint32_t car_body=police_positions[i].body, mode=police_positions[i].mode;
            if(car_body) {
                float *xyz=police_positions[i].xyz;
                char distance[32]="null";
                ++police_body_count;
                if(player_position_valid) {
                    double dx=(double)xyz[0]-x,dz=(double)xyz[2]-z;
                    double horizontal=sqrt(dx*dx+dz*dz);
                    snprintf(distance,sizeof distance,"%.7g",horizontal);
                    if(mode && horizontal<=150.0 && fabs((double)xyz[1]-y)<=50.0) {
                        if(!appendf(nearby_ids,sizeof nearby_ids,&nearby_used,"%s%u",
                            police_nearby_count?",":"",police_ids[i]))goto snapshot_overflow;
                        ++police_nearby_count;
                    }
                }
                if(!appendf(actors,sizeof actors,&actor_used,
                    "%s{\"id\":%u,\"mode\":%u,\"body\":%u,\"x\":%.7g,\"y\":%.7g,\"z\":%.7g,\"distance\":%s}",
                    i?",":"",police_ids[i],mode,car_body,xyz[0],xyz[1],xyz[2],distance))goto snapshot_overflow;
            } else if(!appendf(actors,sizeof actors,&actor_used,
                "%s{\"id\":%u,\"mode\":%u,\"body\":0,\"x\":null,\"y\":null,\"z\":null,\"distance\":null}",
                i?",":"",police_ids[i],mode))goto snapshot_overflow;
        }
        snprintf(body_count,sizeof body_count,"%u",police_body_count);
        if(player_position_valid)snprintf(nearby_count,sizeof nearby_count,"%u",police_nearby_count);
    }
    if(collision.enabled) {
        snprintf(contacts,sizeof contacts,"%llu",(unsigned long long)collision.contact_events);
        snprintf(effects,sizeof effects,"%llu",(unsigned long long)collision.effects);
    }
    int written=snprintf(snapshot,sizeof snapshot,
        "{\"ok\":true,\"qpc_ms\":%.3f,\"scene\":{\"city\":%u,\"mode\":%u,\"time\":%u,\"traffic\":%s,\"peds\":%s},"
        "\"player\":{\"available\":%s,\"x\":%.9g,\"y\":%.9g,\"z\":%.9g,\"heading\":%.9g,\"speed\":%.9g},"
        "\"police\":{\"count\":%s,\"ids\":[%s],\"count_kind\":\"pool\",\"active_count\":%s,\"active_ids\":[%s],\"active_basis\":\"native_execution_mode_nonzero\","
        "\"body_count\":%s,\"nearby_count\":%s,\"nearby_ids\":[%s],\"nearby_horizontal_m\":150,\"nearby_vertical_m\":50,"
        "\"placement_basis\":\"validated_native_rigid_body_translation\",\"placement_attempted\":%u,\"placement_accepted\":%u,\"placement_verified\":%u,\"actors\":[%s]},"
        "\"audio_pool\":{\"available\":%s,\"manager\":%u,\"capacity\":64,\"active_wrappers\":%u,\"free_resources\":%u,"
        "\"zero_free_samples\":%u,\"invalid_samples\":%u,\"factory_empty_rejections\":%ld,\"counter_scope\":\"process_since_first_stress_tick\"},"
        "\"traffic\":{\"count\":%s,\"count_kind\":\"pool\",\"active_count\":null},\"collisions\":{\"total\":%s,\"effects\":%s,\"static\":null,\"vehicle\":null,\"wall\":null}}",
        frequency.QuadPart>0?qpc.QuadPart*1000.0/frequency.QuadPart:0,
        MEM32(0x407C18),MEM32(0x407C1C),MEM32(0x407C30),traffic,peds,
        isfinite(x)&&isfinite(y)&&isfinite(z)&&isfinite(heading)&&isfinite(speed)?"true":"false",
        isfinite(x)?x:0,isfinite(y)?y:0,isfinite(z)?z:0,isfinite(heading)?heading:0,isfinite(speed)?speed:0,
        count,ids,active_count,active_ids,body_count,nearby_count,nearby_ids,
        placement_attempted,placement_accepted,placement_verified,actors,
        audio_valid?"true":"false",audio_owner,audio_active,audio_free,
        audio_pool_zero_free_samples,audio_pool_invalid_samples,audio_rejections,traffic_count,contacts,effects);
    if(written<0 || (size_t)written>=sizeof snapshot)goto snapshot_overflow;
    goto done;
snapshot_overflow:
    strcpy_s(snapshot,sizeof snapshot,"{\"ok\":false,\"error\":\"stress snapshot capacity exceeded\"}");
done:
    sampled=now;
    ReleaseSRWLockExclusive(&state_lock);
    running=0;
    if(host_valid)fesetenv(&host_fp);
    _mm_setcsr(saved_mxcsr);
    errno=saved_errno;SetLastError(saved_error);
}

int mm3_stress_command(const char *command,char *reply,size_t size)
{
    if(strncmp(command,"stress ",7))return 0;
    if(!strcmp(command,"stress snapshot")) {
        AcquireSRWLockShared(&state_lock);
        if(GetTickCount()-sampled<500)snprintf(reply,size,"%s",snapshot);
        else snprintf(reply,size,"{\"ok\":false,\"error\":\"player update stale\"}");
        ReleaseSRWLockShared(&state_lock);return 1;
    }
    int value;char extra;
    float scale;
    if(!strcmp(command,"stress native_apu")) {
        snprintf(reply,size,"{\"ok\":true,\"effective_native_apu\":%d}",mm3_apu_mmio_get_enabled());return 1;
    }
    if(sscanf(command,"stress native_apu %d %c",&value,&extra)==1) {
        int ok=mm3_apu_mmio_set_enabled(value);
        snprintf(reply,size,"{\"ok\":%s,\"requested_native_apu\":%d,\"effective_native_apu\":%d}",ok?"true":"false",value,mm3_apu_mmio_get_enabled());return 1;
    }
    if(sscanf(command,"stress audio_wait %d %c",&value,&extra)==1) {
        int ok=mcpx_apu_set_hires_wait_enabled(value);
        snprintf(reply,size,"{\"ok\":%s,\"requested_audio_wait\":%d,\"effective_audio_wait\":%d}",ok?"true":"false",value,mcpx_apu_get_hires_wait_enabled());return 1;
    }
    if(sscanf(command,"stress fence %d %c",&value,&extra)==1) {
        int ok=nv2a_d3d_set_readback_fence(value);
        snprintf(reply,size,"{\"ok\":%s,\"requested_fence\":%d,\"effective_fence\":%d}",ok?"true":"false",value,nv2a_d3d_get_readback_fence());return 1;
    }
    if(sscanf(command,"stress scale %f %c",&scale,&extra)==1) {
        int ok=isfinite(scale)&&scale>=0.5f&&scale<=8.0f;
        if(ok)nv2a_d3d_set_render_scale(scale);
        snprintf(reply,size,"{\"ok\":%s,\"requested_scale\":%.9g,\"effective_scale\":%.9g}",ok?"true":"false",isfinite(scale)?scale:0,nv2a_d3d_get_render_scale());return 1;
    }
    if(sscanf(command,"stress poll %d %c",&value,&extra)==1) {
        int ok=nv2a_d3d_set_readback_poll_us(value);
        snprintf(reply,size,"{\"ok\":%s,\"requested_us\":%d,\"effective_us\":%d}",ok?"true":"false",value,nv2a_d3d_get_readback_poll_us());return 1;
    }
    value=-1;
    if(!strcmp(command,"stress clear"))value=0;
    else if(sscanf(command,"stress police %d %c",&value,&extra)!=1)value=-1;
    if(value>=0&&value<=16) {
        uint32_t before[32];unsigned old_count,request;
        AcquireSRWLockExclusive(&state_lock);
        old_count=police_valid?police_count:0;memcpy(before,police_ids,sizeof before);
        request=++serial;InterlockedExchange(&requested,value);
        ReleaseSRWLockExclusive(&state_lock);
        DWORD start=GetTickCount();
        while(GetTickCount()-start<5000) {
            Sleep(10);AcquireSRWLockShared(&state_lock);
            int done=completed==request;ReleaseSRWLockShared(&state_lock);if(done)break;
        }
        AcquireSRWLockExclusive(&state_lock);
        if(completed!=request) {InterlockedExchange(&requested,-1);snprintf(reply,size,"{\"ok\":false,\"error\":\"native update timeout\"}");}
        else {
            char ids[512]="";size_t n=0;unsigned created=0;
            for(unsigned i=0;i<police_count;++i) {
                unsigned j;for(j=0;j<old_count&&before[j]!=police_ids[i];++j){}
                if(j==old_count) {n+=(size_t)snprintf(ids+n,sizeof ids-n,"%s%u",created?",":"",police_ids[i]);++created;}
            }
            int pool_ok=native_ok&&police_valid&&police_count==(unsigned)value;
            snprintf(reply,size,"{\"ok\":%s,\"pool_ok\":%s,\"requested_total\":%d,\"total\":%u,\"created\":%u,\"ids\":[%s],\"count_kind\":\"pool\","
                "\"activation_attempted\":%u,\"activation_accepted\":%u,\"activation_target_found\":%s,"
                "\"placement_attempted\":%u,\"placement_accepted\":%u,\"placement_verified\":%u,\"active_count\":%u,\"body_count\":%u,\"nearby_count\":%u,"
                "\"shrink_rejected\":%s,\"error\":%s}",
                pool_ok?"true":"false",pool_ok?"true":"false",value,police_count,created,ids,
                activation_attempted,activation_accepted,activation_target?"true":"false",
                placement_attempted,placement_accepted,placement_verified,police_active_count,police_body_count,police_nearby_count,
                shrink_rejected?"true":"false",shrink_rejected?"\"active police pool cannot shrink; reuse the same total or restart the session\"":"null");
        }
        ReleaseSRWLockExclusive(&state_lock);return 1;
    }
    snprintf(reply,size,"{\"ok\":false,\"error\":\"invalid stress command\"}");return 1;
}
