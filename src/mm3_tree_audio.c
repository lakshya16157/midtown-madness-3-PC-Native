#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mm3_recomp_types.h"
#include "mm3_tree_audio.h"

extern void sub_000125EA(void);
extern void sub_000127A9(void);
extern void sub_00198FFB(void);
extern void sub_00198592_original(void);
extern void sub_001986B1_original(void);

#ifndef MM3_TREE_AUDIO_CLOCK
#define MM3_TREE_AUDIO_CLOCK GetTickCount64
#endif
#ifndef MM3_TREE_AUDIO_LOG
#define MM3_TREE_AUDIO_LOG(...) fprintf(stderr, __VA_ARGS__)
#endif

enum { AUDIO_SLOTS = 64, AUDIO_SUCCESSES = 32, AUDIO_STACK = 0x4000 };
typedef struct {
    uint32_t city, id, metadata, car;
    uint64_t attempted;
    int broken;
} TreeAudioContact;
static RECOMP_TLS TreeAudioContact s_audio_contacts[AUDIO_SLOTS], s_audio_selected;
static RECOMP_TLS uint32_t s_audio_success[AUDIO_SUCCESSES];
static RECOMP_TLS unsigned s_audio_success_count, s_audio_replacement;
static RECOMP_TLS int s_audio_active;

static int audio_read(uint32_t address, void *destination, size_t size)
{
    SIZE_T copied = 0;
    uintptr_t host;
    if (address < 0x10000u || size > UINT32_MAX || address > UINT32_MAX - size) return 0;
    host = (uintptr_t)address + (uintptr_t)g_xbox_mem_offset;
    if (host > UINTPTR_MAX - size) return 0;
    return ReadProcessMemory(GetCurrentProcess(), (const void *)host, destination, size, &copied)
        && copied == size;
}

static void audio_success(uint32_t script)
{
    unsigned i;
    if (!s_audio_active || !script) return;
    for (i = 0; i < s_audio_success_count; ++i) if (s_audio_success[i] == script) return;
    if (s_audio_success_count < AUDIO_SUCCESSES) s_audio_success[s_audio_success_count++] = script;
}

/* The native playback methods return false when a script has no loaded bank
 * or cannot obtain a voice. An event merely running is not proof of sound. */
void sub_00198592(void)
{
    uint32_t script = g_eax;
    sub_00198592_original();
    if (g_eax & 255u) audio_success(script);
}

void sub_001986B1(void)
{
    uint32_t script = 0;
    if (s_audio_active && g_esp <= UINT32_MAX - 8u) audio_read(g_esp + 4u, &script, sizeof script);
    sub_001986B1_original();
    if (g_eax & 255u) audio_success(script);
}

void mm3_tree_audio_begin(void)
{
    s_audio_active = 1;
    s_audio_success_count = 0;
    memset(&s_audio_selected, 0, sizeof s_audio_selected);
}

void mm3_tree_audio_select(uint32_t metadata, uint32_t city, uint32_t id, uint32_t car)
{
    if (!s_audio_active) return;
    /* Only the contact seam's guarded native tree classifier calls this. Both
     * cities author the existing "tree" bank: Paris uses an event sound, while
     * Washington also supplies the main LocalObject impact sound. */
    s_audio_selected.city = city;
    s_audio_selected.id = id;
    s_audio_selected.metadata = metadata;
    s_audio_selected.car = car;
}

typedef struct {
    uint32_t regs[11];
    double fp[8];
    RecompXmm xmm[8];
    int top, compare, direction;
    uint16_t status, control;
} AudioGuestState;

static AudioGuestState audio_save(void)
{
    AudioGuestState state = {0};
    const uint32_t regs[] = {g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp,g_seh_ebp,g_fs_base,g_eflags};
    const RecompXmm xmm[] = {g_xmm0,g_xmm1,g_xmm2,g_xmm3,g_xmm4,g_xmm5,g_xmm6,g_xmm7};
    memcpy(state.regs, regs, sizeof regs); memcpy(state.fp, g_fp_stack, sizeof state.fp);
    memcpy(state.xmm, xmm, sizeof xmm);
    state.top=g_fp_top;state.compare=g_fp_cmp;state.direction=g_df;
    state.status=g_fp_cc;state.control=g_fp_control_word;
    return state;
}

static void audio_restore(const AudioGuestState *state)
{
    g_eax=state->regs[0];g_ecx=state->regs[1];g_edx=state->regs[2];g_esp=state->regs[3];
    g_ebx=state->regs[4];g_esi=state->regs[5];g_edi=state->regs[6];g_ebp=state->regs[7];
    g_seh_ebp=state->regs[8];g_fs_base=state->regs[9];g_eflags=state->regs[10];
    memcpy(g_fp_stack,state->fp,sizeof state->fp);
    g_fp_top=state->top;g_fp_cmp=state->compare;g_df=state->direction;
    g_fp_cc=state->status;g_fp_control_word=state->control;
    g_xmm0=state->xmm[0];g_xmm1=state->xmm[1];g_xmm2=state->xmm[2];g_xmm3=state->xmm[3];
    g_xmm4=state->xmm[4];g_xmm5=state->xmm[5];g_xmm6=state->xmm[6];g_xmm7=state->xmm[7];
}

static int audio_native_tree(const float position[3], int *already_played)
{
    /* Reserve guest-stack data above the temporary call stack. The calls below
     * are the same string lookup and positional bank playback used by E1F49;
     * preserve the whole scratch region and guest machine state afterward. */
    unsigned char scratch[AUDIO_STACK];
    AudioGuestState state = audio_save();
    uint32_t base, string, matrix, name, script = 0;
    unsigned i;
    int played = 0;
    float transform[12] = {1,0,0,0,1,0,0,0,1,0,0,0};
    if (g_esp < 0x10000u + AUDIO_STACK || !audio_read(g_esp - AUDIO_STACK, scratch, sizeof scratch)) return 0;
    base = g_esp - AUDIO_STACK;
    name = g_esp - 16u; string = g_esp - 64u; matrix = g_esp - 128u;
    memcpy((void *)XBOX_PTR(name), "tree", 5);
    memset((void *)XBOX_PTR(string), 0, 32);
    memcpy(transform + 9, position, 3 * sizeof(float));
    memcpy((void *)XBOX_PTR(matrix), transform, sizeof transform);
    g_esp = matrix - 16u; g_df = 0;
    g_ecx = string;
    PUSH32(g_esp, name); PUSH32(g_esp, 0x000E2925u); sub_000125EA();
    PUSH32(g_esp, string); PUSH32(g_esp, 0x000E2931u); sub_00198FFB();
    script = g_eax;
    for (i = 0; i < s_audio_success_count; ++i) if (s_audio_success[i] == script && script) *already_played = 1;
    g_ecx = string;
    PUSH32(g_esp, 1); PUSH32(g_esp, 0x000E2A4Au); sub_000127A9();
    if (script && !*already_played) {
        g_eax = script;
        PUSH32(g_esp, matrix); PUSH32(g_esp, 0x000E2A7Fu); sub_00198592_original();
        played = (g_eax & 255u) != 0;
    }
    memcpy((void *)XBOX_PTR(base), scratch, sizeof scratch);
    audio_restore(&state);
    return played;
}

void mm3_tree_audio_finish(int converted, int contacting, const float position[3], float speed)
{
    TreeAudioContact *entry = NULL;
    uint64_t now;
    unsigned i, slot = AUDIO_SLOTS;
    int already_played = 0, played;
    const char *trace;
    s_audio_active = 0;
    if ((!converted && !contacting) || !s_audio_selected.city || !s_audio_selected.metadata
        || !s_audio_selected.car || !position || !isfinite(speed) || speed < 2.0f
        || !isfinite(position[0]) || !isfinite(position[1]) || !isfinite(position[2])) return;
    now = MM3_TREE_AUDIO_CLOCK();
    for (i = 0; i < AUDIO_SLOTS; ++i) {
        TreeAudioContact *candidate = &s_audio_contacts[i];
        if (candidate->city == s_audio_selected.city && candidate->id == s_audio_selected.id
            && candidate->metadata == s_audio_selected.metadata && candidate->car == s_audio_selected.car) {
            entry = candidate; break;
        }
        if ((!candidate->city || candidate->city != s_audio_selected.city) && slot == AUDIO_SLOTS) slot = i;
    }
    if (entry && now >= entry->attempted && now - entry->attempted < 400u
        && (!converted || entry->broken)) return;
    if (!entry) {
        if (slot == AUDIO_SLOTS) slot = s_audio_replacement++ % AUDIO_SLOTS;
        entry = &s_audio_contacts[slot];
    }
    *entry = s_audio_selected; entry->attempted = now; entry->broken = converted;
    played = audio_native_tree(position, &already_played);
    trace = getenv("MM3_TREE_TRACE");
    if (trace && *trace && strcmp(trace, "0") != 0)
        MM3_TREE_AUDIO_LOG("[TREE_IMPACT_AUDIO] id=%08X metadata=%08X converted=%d speed=%.3f sound=tree native_success=%d fallback_success=%d position=(%.3f,%.3f,%.3f)\n",
            entry->id, entry->metadata, converted, speed, already_played, played, position[0], position[1], position[2]);
}
