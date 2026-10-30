/* Keyboard cheats (src/mm3_cheat_keys.c), the F8 free camera
 * (src/mm3_free_camera.c) and howhardcanitbe0..9 (src/mm3_ai_difficulty.c),
 * against the game's own view matrix builder and a race AI's update. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <xinput.h>
#include "recomp_types.h"
#include "../src/mm3_cheat_keys.c"

ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi;
static unsigned char memory[0x410000];
static const uint32_t k_manager = 0x31000, k_camera = 0x32000, k_out = 0x33000, k_stack = 0xFF00,
                      k_ai = 0x40000, k_personality = 0x41000, k_vehicle = 0x42000, k_car = 0x43000, k_state = 0x44000,
                      k_player = 0x45000, k_player_vehicle = 0x46000;
static int held[256], blocked;
static float seen[5];

int xbox_FramebufferKeyDown(int vk) { return vk >= 0 && vk < 256 && held[vk]; }
void xbox_InputSetBlocked(int value) { blocked = value; }
DWORD WINAPI test_XInputGetState(DWORD port, XINPUT_STATE *s) { (void)port; (void)s; return ERROR_DEVICE_NOT_CONNECTED; }

/* sub_001EA16C as the game runs it: [R^T 0; -P R^T 1] from camera+0x10. */
void sub_001EA16C_original(void)
{
    uint32_t c = MEM32(g_esp + 4);
    const float *r = (const float *)XBOX_PTR(c + 0x10), *p = r + 9;
    float *m = (float *)XBOX_PTR(g_ebx);
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) m[i * 4 + j] = r[j * 3 + i];
        m[i * 4 + 3] = 0.0f;
        m[12 + i] = -(p[0] * r[i * 3] + p[1] * r[i * 3 + 1] + p[2] * r[i * 3 + 2]);
    }
    m[15] = 1.0f;
    g_esp += 8;
}

void sub_0014194C_original(void)
{
    static const uint32_t fields[5] = {0xD4, 0xD0, 0x78, 0xB4, 0xB8};
    for (int i = 0; i < 5; ++i) seen[i] = MEMF(k_personality + fields[i]);
    g_esp += 8;
}

#include "../src/mm3_free_camera.c"
#include "../src/mm3_ai_difficulty.c"

static void draw(float m[16])
{
    g_esp = k_stack;
    MEM32(g_esp + 4) = k_camera;
    g_ebx = k_out;
    sub_001EA16C();
    assert(g_esp == k_stack + 8 && g_ebx == k_out);
    for (int i = 0; i < 16; ++i) m[i] = MEMF(k_out + 4u * (uint32_t)i);
}

static void eye(const float m[16], float p[3])
{
    for (int j = 0; j < 3; ++j) p[j] = -(m[12] * m[j * 4] + m[13] * m[j * 4 + 1] + m[14] * m[j * 4 + 2]);
}

static int same(const float a[16], const float b[16])
{
    for (int i = 0; i < 16; ++i) if (fabsf(a[i] - b[i]) > 1e-4f) return 0;
    return 1;
}

/* A race camera at (100, 5, -40) looking along yaw/pitch, without roll; a
 * mirrored world swaps its right axis. */
static void pose(float yaw, float pitch, float hand)
{
    float *r = (float *)XBOX_PTR(k_camera + 0x10);
    float f[3] = {cosf(pitch) * sinf(yaw), sinf(pitch), cosf(pitch) * cosf(yaw)};
    float x[3] = {hand * cosf(yaw), 0.0f, -hand * sinf(yaw)};
    float u[3] = {hand * (f[1] * x[2] - f[2] * x[1]), hand * (f[2] * x[0] - f[0] * x[2]), hand * (f[0] * x[1] - f[1] * x[0])};
    for (int i = 0; i < 3; ++i) { r[i] = x[i]; r[3 + i] = u[i]; r[6 + i] = f[i]; }
    r[9] = 100.0f; r[10] = 5.0f; r[11] = -40.0f;
}

static void press_f8(void) { InterlockedIncrement(&s_camera_presses); }

static void camera_contract(float hand)
{
    float game[16], m[16], p0[3], p[3], d[3], length;
    const float *f = (const float *)XBOX_PTR(k_camera + 0x10 + 24), *r = (const float *)XBOX_PTR(k_camera + 0x10);
    pose(0.7f, -0.2f, hand);
    draw(game);                                  /* the game's view, F8 off */
    press_f8(); draw(m);
    assert(InterlockedCompareExchange(&s_active, 0, 0) && blocked);
    assert(same(m, game));                       /* starts exactly at the game's view */
    eye(m, p0);
    assert(fabsf(p0[0] - 100.0f) < 1e-3f && fabsf(p0[1] - 5.0f) < 1e-3f && fabsf(p0[2] + 40.0f) < 1e-3f);
    held['W'] = 1; Sleep(50); draw(m); held['W'] = 0;
    eye(m, p);
    for (int i = 0; i < 3; ++i) d[i] = p[i] - p0[i];
    length = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    assert(length > 0.5f && (d[0] * f[0] + d[1] * f[1] + d[2] * f[2]) / length > 0.999f);   /* W: forward */
    held[VK_RIGHT] = 1; Sleep(50); draw(m); held[VK_RIGHT] = 0;
    assert(m[2] * r[0] + m[6] * r[1] + m[10] * r[2] > 0.02f);  /* turned toward the right */
    held['W'] = held[VK_SHIFT] = 1;
    for (int i = 0; i < 12; ++i) { Sleep(100); draw(m); }      /* 100 m/s for over a second */
    held['W'] = held[VK_SHIFT] = 0;
    eye(m, p);
    length = sqrtf((p[0] - 100.0f) * (p[0] - 100.0f) + (p[2] + 40.0f) * (p[2] + 40.0f));
    assert(length > 49.0f && length <= 50.0f + 1e-2f);       /* MM3_FREE_CAMERA_RANGE=50 */
    press_f8(); draw(m);
    assert(!InterlockedCompareExchange(&s_active, 0, 0) && !blocked && same(m, game));
    printf("PASS free camera (%s world): F8 starts at the game's view, W flies forward, Right turns right, "
           "flight stays within range, F8 again is the game's view and input\n", hand > 0 ? "left-handed" : "mirrored");
}

static void watchdog_contract(void)
{
    float m[16];
    press_f8(); draw(m);
    assert(InterlockedCompareExchange(&s_active, 0, 0) && blocked);
    mm3_free_camera_watchdog();
    assert(blocked);                             /* still drawing */
    Sleep(1600);
    mm3_free_camera_watchdog();
    assert(!InterlockedCompareExchange(&s_active, 0, 0) && !blocked);
    press_f8(); Sleep(1600); draw(m);            /* a press away from the race is not taken */
    assert(!InterlockedCompareExchange(&s_active, 0, 0) && !blocked);
    puts("PASS free camera: no race view for 1.5 s gives the input back; presses away from a race are dropped");
}

static void type(const char *text)
{
    for (; *text; ++text) typed(*text >= 'a' && *text <= 'z' ? (char)(*text - 32) : *text);
}

static void step_ai(void)
{
    g_esp = k_stack; g_ecx = k_state;
    sub_0014194C();
    assert(g_esp == k_stack + 8);
}

static void ai_contract(void)
{
    MEM32(k_state + 4) = k_ai; MEM32(k_ai) = 0x37FAB0; MEM32(k_ai + 0x94) = k_personality; MEM32(k_ai + 0x28) = k_vehicle;
    MEM32(k_vehicle) = 0x383768; MEM32(k_vehicle + 8) = k_car;
    MEMF(k_car + 0x90) = MEMF(k_car + 0x94) = 1.0f;
    MEMF(k_personality + 0xD4) = 22.0f; MEMF(k_personality + 0xD0) = 22.0f; MEMF(k_personality + 0x78) = 40.0f;
    MEMF(k_personality + 0xB4) = 0.5f; MEMF(k_personality + 0xB8) = 0.8f;
    MEM32(k_manager + 0x50) = 1;                   /* the human: player 1's car */
    MEM32(k_camera + 0xD8) = k_player; MEM32(k_player + 0x19C) = k_player_vehicle;
    MEM32(k_player_vehicle) = 0x383768; MEM32(k_player_vehicle + 8) = 0x47000;
    MEMF(0x47000 + 0x90) = 1.0f;

    assert(mm3_cheat_ai_level() == 5);
    type("howhardcanitbe"); type("x9"); assert(mm3_cheat_ai_level() == 5);
    type("xxhowhardcanitbe9"); assert(mm3_cheat_ai_level() == 9);
    step_ai();
    assert(fabsf(seen[0] - 22.0f * 1.2f) < 1e-4f && fabsf(seen[1] - 22.0f * 1.1f * 1.1f) < 1e-3f);
    assert(fabsf(seen[2] - 44.0f) < 1e-3f && fabsf(seen[3] - 1.0f) < 1e-6f && fabsf(seen[4] - 1.0f) < 1e-6f);
    assert(MEMF(k_personality + 0xD4) == 22.0f && MEMF(k_personality + 0xB4) == 0.5f);   /* put back */
    assert(fabsf(MEMF(k_car + 0x90) - 1.4f) < 1e-5f && fabsf(MEMF(k_car + 0x94) - 1.6f) < 1e-5f);
    step_ai();
    assert(fabsf(MEMF(k_car + 0x90) - 1.4f) < 1e-5f);                               /* not compounded */
    MEMF(k_car + 0x90) = 1.25f;                    /* the race script boosts this opponent */
    step_ai();
    assert(fabsf(MEMF(k_car + 0x90) - 1.25f * 1.4f) < 1e-5f);
    type("howhardcanitbe5"); step_ai();
    assert(fabsf(MEMF(k_car + 0x90) - 1.25f) < 1e-5f && fabsf(MEMF(k_car + 0x94) - 1.0f) < 1e-5f);
    assert(seen[0] == 22.0f && seen[3] == 0.5f);
    type("howhardcanitbe0"); step_ai();
    assert(fabsf(seen[0] - 22.0f * 0.7f) < 1e-4f && fabsf(seen[2] - 32.0f) < 1e-3f && seen[3] < 0.5f);
    assert(fabsf(MEMF(k_car + 0x90) - 1.25f * 0.6f) < 1e-5f);
    MEM32(k_vehicle + 8) = 0x47000;                /* an AI driving the player's car */
    type("howhardcanitbe9"); step_ai();
    assert(MEMF(0x47000 + 0x90) == 1.0f);
    MEM32(k_ai) = 0x37F000;                        /* not a race car's AI: police, traffic */
    step_ai();
    assert(seen[0] == 22.0f && MEMF(0x47000 + 0x90) == 1.0f);
    puts("PASS howhardcanitbeN: typed digit sets the level; above 5 later braking, faster corners, no holding back, "
         "engine and brakes on top of the scripts' own factors; 5 restores; 0 slower; police and your car untouched");
}

int main(void)
{
    g_xbox_mem_offset = (ptrdiff_t)memory;
    _putenv_s("MM3_FREE_CAMERA_RANGE", "50");
    MEM32(0x3C5CDC) = k_manager; MEM32(k_manager + 0x54) = k_camera;
    camera_contract(1.0f);
    camera_contract(-1.0f);
    watchdog_contract();
    ai_contract();
    return 0;
}
