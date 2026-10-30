#include <windows.h>
#include <xinput.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "recomp_types.h"
#include "mm3_cheat_keys.h"

/* Free camera: its key in a race (F8 unless the launcher's Gameplay mods tab
 * chose another; Free camera off there: never) flies the view away from the car.
 *
 * sub_001EA16C(camera) turns a race camera's pose into the world-to-camera
 * matrix the frame is drawn with: the pose is a Matrix34 at camera+0x10,
 * rows right, up, forward (world axes, Y up) and the position at +0x34; the
 * 4x4 result at EBX is [R^T 0; -P R^T 1] (row vectors). The draw (sub_001EC8E6)
 * calls it once per camera a frame, then hands the matrix to the view
 * (sub_000F4BB1 -> +0x1F24) whose eye, frustum planes, lights, fog and LOD
 * follow from it. Replacing only that result moves everything drawn and
 * nothing the game keeps: the chase camera, the race arrow and the camera
 * state carry on underneath, so the key again is straight back to the game's view.
 * The draw runs while paused too.
 *
 * The city streams around the player's car (sub_001E960A gives the streaming
 * the car's position), cells of 400 m with their eight neighbours, so the
 * camera stays within MM3_FREE_CAMERA_RANGE metres (default 400, 0 = no limit)
 * of the game's own camera, across the ground.
 *
 * While it flies the game gets no controller or keyboard input (the F10
 * dialog's xbox_InputSetBlocked): the car rolls on with nothing pressed.
 * Controls: W/S/A/D or the left stick move, Q/E or the triggers go down and
 * up, the arrow keys, the right stick or the mouse with its right button held
 * look around; Shift or RB is five times faster, Ctrl or LB five times slower.
 * The key thread (src/mm3_cheat_keys.c) gives the input back if races stop
 * drawing (results, menus) while it is on. */
extern void sub_001EA16C_original(void);
extern int xbox_FramebufferKeyDown(int vk);
extern void xbox_InputSetBlocked(int blocked);

#define SPEED 20.0f          /* metres a second */
#define TURN 1.6f            /* radians a second, keys and full stick */
#define MOUSE 0.003f         /* radians a pixel */

static volatile LONG s_active;
static volatile LONG s_drawn;          /* GetTickCount of the last race draw */
static float s_pos[3], s_yaw, s_pitch, s_hand;
static LARGE_INTEGER s_last;
static POINT s_cursor;
static int s_mouse_held;

static float range(void)
{
    static float metres = -1.0f;
    if (metres < 0.0f) {
        const char *v = getenv("MM3_FREE_CAMERA_RANGE");
        metres = v && *v ? (float)atof(v) : 400.0f;
        if (!(metres >= 0.0f)) metres = 400.0f;
    }
    return metres;
}

static int key(int vk) { return xbox_FramebufferKeyDown(vk) != 0; }

static float stick(SHORT value, SHORT dead)
{
    float v = (float)value;
    if (value > -dead && value < dead) return 0.0f;
    v = (v + (value > 0 ? -dead : dead)) / (32767.0f - dead);
    return v > 1.0f ? 1.0f : v < -1.0f ? -1.0f : v;
}

/* The first connected pad, looked for once a second: asking an empty port
 * every frame is slow. */
static int pad(XINPUT_GAMEPAD *out)
{
    static DWORD port = 4, looked;
    XINPUT_STATE state;
    if (port < 4 && XInputGetState(port, &state) == ERROR_SUCCESS) {
        *out = state.Gamepad;
        return 1;
    }
    port = 4;
    if (GetTickCount() - looked < 1000) return 0;
    looked = GetTickCount();
    for (DWORD i = 0; i < 4; ++i)
        if (XInputGetState(i, &state) == ERROR_SUCCESS) { port = i; *out = state.Gamepad; return 1; }
    return 0;
}

static void start(uint32_t camera)
{
    const float *r = (const float *)XBOX_PTR(camera + 0x10u);
    float cross[3];
    for (int i = 0; i < 3; ++i) s_pos[i] = r[9 + i];
    s_yaw = atan2f(r[6], r[8]);
    s_pitch = asinf(fmaxf(-1.0f, fminf(1.0f, r[7])));
    /* right = up x forward in the game's (left-handed) world; follow
     * whichever the pose shows so looking left and right never swap. */
    cross[0] = r[4] * r[8] - r[5] * r[7];
    cross[1] = r[5] * r[6] - r[3] * r[8];
    cross[2] = r[3] * r[7] - r[4] * r[6];
    s_hand = cross[0] * r[0] + cross[1] * r[1] + cross[2] * r[2] < 0.0f ? -1.0f : 1.0f;
    if (!isfinite(s_pos[0] + s_pos[1] + s_pos[2] + s_yaw + s_pitch)) {
        s_pos[0] = s_pos[1] = s_pos[2] = s_yaw = s_pitch = 0.0f;
    }
    QueryPerformanceCounter(&s_last);
    s_mouse_held = 0;
    InterlockedExchange(&s_active, 1);
    xbox_InputSetBlocked(1);
    fprintf(stderr, "[MM3_FREE_CAMERA] on at %.1f %.1f %.1f\n", s_pos[0], s_pos[1], s_pos[2]);
    fflush(stderr);
}

static void stop(const char *why)
{
    if (!InterlockedExchange(&s_active, 0)) return;
    xbox_InputSetBlocked(0);
    fprintf(stderr, "[MM3_FREE_CAMERA] off (%s)\n", why);
    fflush(stderr);
}

/* The key thread: no race drawn for 1.5 s with the camera on (results,
 * menus, loading) gives the input back. */
void mm3_free_camera_watchdog(void)
{
    if (InterlockedCompareExchange(&s_active, 0, 0) &&
        GetTickCount() - (DWORD)InterlockedCompareExchange(&s_drawn, 0, 0) > 1500u)
        stop("no race view");
}

static void fly(uint32_t camera, float out[16])
{
    LARGE_INTEGER now, frequency;
    XINPUT_GAMEPAD g;
    float dt, speed = SPEED, move[3] = {0}, f[3], r[3], u[3], length, limit;
    const float *game = (const float *)XBOX_PTR(camera + 0x34u);
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    dt = (float)(now.QuadPart - s_last.QuadPart) / (float)frequency.QuadPart;
    s_last = now;
    if (!(dt >= 0.0f) || dt > 0.1f) dt = 0.1f;

    /* Look. */
    if (key(VK_LEFT)) s_yaw -= TURN * dt * s_hand;
    if (key(VK_RIGHT)) s_yaw += TURN * dt * s_hand;
    if (key(VK_UP)) s_pitch += TURN * dt;
    if (key(VK_DOWN)) s_pitch -= TURN * dt;
    if (key(VK_RBUTTON)) {
        POINT p;
        if (GetCursorPos(&p)) {
            /* The pointer goes back where the button was pressed each frame,
             * so turning never stops at the screen's edge. */
            if (s_mouse_held) {
                s_yaw += (float)(p.x - s_cursor.x) * MOUSE * s_hand;
                s_pitch -= (float)(p.y - s_cursor.y) * MOUSE;
                SetCursorPos(s_cursor.x, s_cursor.y);
            } else {
                s_cursor = p;
            }
            s_mouse_held = 1;
        }
    } else {
        s_mouse_held = 0;
    }
    if (key(VK_SHIFT)) speed *= 5.0f;
    if (key(VK_CONTROL)) speed *= 0.2f;
    if (key('W')) move[2] += 1.0f;
    if (key('S')) move[2] -= 1.0f;
    if (key('D')) move[0] += 1.0f;
    if (key('A')) move[0] -= 1.0f;
    if (key('E')) move[1] += 1.0f;
    if (key('Q')) move[1] -= 1.0f;
    if (pad(&g)) {
        s_yaw += stick(g.sThumbRX, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE) * TURN * dt * s_hand;
        s_pitch += stick(g.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE) * TURN * dt;
        move[0] += stick(g.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
        move[2] += stick(g.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
        move[1] += (g.bRightTrigger - g.bLeftTrigger) / 255.0f;
        if (g.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) speed *= 5.0f;
        if (g.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) speed *= 0.2f;
    }
    s_pitch = fmaxf(-1.55f, fminf(1.55f, s_pitch));
    s_yaw = remainderf(s_yaw, 6.2831853f);

    /* The pose: forward from yaw and pitch, right across it level with the
     * ground, up completing the frame. */
    f[0] = cosf(s_pitch) * sinf(s_yaw);
    f[1] = sinf(s_pitch);
    f[2] = cosf(s_pitch) * cosf(s_yaw);
    r[0] = s_hand * cosf(s_yaw);
    r[1] = 0.0f;
    r[2] = -s_hand * sinf(s_yaw);
    u[0] = s_hand * (f[1] * r[2] - f[2] * r[1]);
    u[1] = s_hand * (f[2] * r[0] - f[0] * r[2]);
    u[2] = s_hand * (f[0] * r[1] - f[1] * r[0]);
    for (int i = 0; i < 3; ++i)
        s_pos[i] += (f[i] * move[2] + r[i] * move[0] + (i == 1) * move[1]) * speed * dt;

    /* Stay where the city is loaded: across the ground from the game's own
     * camera, which follows the car. */
    limit = range();
    if (limit > 0.0f && isfinite(game[0] + game[2])) {
        float dx = s_pos[0] - game[0], dz = s_pos[2] - game[2];
        length = sqrtf(dx * dx + dz * dz);
        if (length > limit) {
            s_pos[0] = game[0] + dx * limit / length;
            s_pos[2] = game[2] + dz * limit / length;
        }
    }

    /* [R^T 0; -P R^T 1], as sub_001EA16C builds it. */
    for (int i = 0; i < 3; ++i) {
        out[i * 4 + 0] = r[i];
        out[i * 4 + 1] = u[i];
        out[i * 4 + 2] = f[i];
        out[i * 4 + 3] = 0.0f;
    }
    out[12] = -(s_pos[0] * r[0] + s_pos[1] * r[1] + s_pos[2] * r[2]);
    out[13] = -(s_pos[0] * u[0] + s_pos[1] * u[1] + s_pos[2] * u[2]);
    out[14] = -(s_pos[0] * f[0] + s_pos[1] * f[1] + s_pos[2] * f[2]);
    out[15] = 1.0f;
}

void sub_001EA16C(void)
{
    uint32_t camera = MEM32(g_esp + 4u), out = g_ebx, manager = MEM32(0x003C5CDCu);
    int presses;
    sub_001EA16C_original();
    if (manager < 0x10000u || camera != MEM32(manager + 0x54u)) return;   /* player 1's view only */
    /* Presses made away from a race (menus, loading) are not for this one. */
    presses = mm3_cheat_camera_presses();
    if (GetTickCount() - (DWORD)InterlockedExchange(&s_drawn, (LONG)GetTickCount()) > 1500u) presses = 0;
    if (presses & 1) {
        if (InterlockedCompareExchange(&s_active, 0, 0)) stop("key");
        else start(camera);
    }
    if (!InterlockedCompareExchange(&s_active, 0, 0)) return;
    xbox_InputSetBlocked(1);      /* again: closing the F10 dialog gives it back */
    /* The key thread's watchdog may have stopped it in between: then its
     * release came first and this block must not outlive it. */
    if (!InterlockedCompareExchange(&s_active, 0, 0)) { xbox_InputSetBlocked(0); return; }
    fly(camera, (float *)XBOX_PTR(out));
}
