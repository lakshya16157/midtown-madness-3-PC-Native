/* Off Windows: XInputGetState for the port's own tools (free camera) over the
 * runtime's SDL3 pad layer (xbox_InputHostPad), which sees every SDL gamepad.
 * XINPUT_* types and button bits come from win32_compat.h. */
#pragma once
#include "platform/win32_compat.h"
#include "input/xinput_xbox.h"
#ifndef XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE
#define XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE  7849
#define XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE 8689
#define XINPUT_GAMEPAD_TRIGGER_THRESHOLD    30
#endif
static inline DWORD XInputGetState(DWORD port, XINPUT_STATE *state)
{
    XINPUT_GAMEPAD *g = &state->Gamepad;
    uint16_t buttons;
    uint8_t lt, rt;
    int16_t lx, ly, rx, ry;
    uint32_t r = xbox_InputHostPad(port, &buttons, &lt, &rt, &lx, &ly, &rx, &ry);
    if (r) return r;
    state->dwPacketNumber++;
    g->wButtons = buttons; g->bLeftTrigger = lt; g->bRightTrigger = rt;
    g->sThumbLX = lx; g->sThumbLY = ly; g->sThumbRX = rx; g->sThumbRY = ry;
    return ERROR_SUCCESS;
}
