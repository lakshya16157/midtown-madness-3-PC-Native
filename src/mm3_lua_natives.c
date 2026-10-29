#include <stdlib.h>
#include <string.h>
#include "recomp_types.h"
#include "mm3_mod_flags.h"
#include "mm3_lua_natives.h"

/* Lua functions this port adds for its own race scripts.
 *
 * sub_0020646F registers every native the scripts can call, with the script
 * wrapper in ECX (kept in EBX and at 0x3C0320): for each one it builds an
 * std::string of the name (sub_000125EA), calls sub_002150BC(&name, handler)
 * with the wrapper in EBX, and frees the name (sub_000127A9(1)). Handlers are
 * called through the dispatch table with the wrapper in ECX and return the
 * number of results in EAX; sub_00214D2F(double) pushes a number result.
 *
 * The added handlers live at guest addresses inside sub_0020646F's own body,
 * which nothing calls (mm3_runtime_entries.c maps them):
 *
 *   mm3AiAllCars()  1 when circuit opponents may drive the DLC and extra cars
 *                   (launcher Mods, MM3_AI_ALL_CARS, default 1, or
 *                   MM3_SAME_CAR: they drive the player's car, whichever it
 *                   is), else 0 (tools/races/logic_circuit.lua). */
extern void sub_0020646F_original(void);
extern void sub_000125EA(void);
extern void sub_002150BC(void);
extern void sub_000127A9(void);
extern void sub_00214D2F(void);

#define LUA_WRAPPER 0x003C0320u
#define RETURN_SITE 0x00208931u   /* sub_0020646F's own epilogue, for stack traces */

static int option(const char *name, int fallback)
{
    const char *v = getenv(name);
    if (v && !strcmp(v, "0")) return 0;
    if (v && !strcmp(v, "1")) return 1;
    return fallback;
}

/* Hand one number back to the script. */
static void lua_return_number(double value)
{
    g_eax = MEM32(LUA_WRAPPER);
    g_esp -= 8;
    MEMD(g_esp) = value;
    PUSH32(g_esp, RETURN_SITE);
    sub_00214D2F();
    g_eax = 1;
}

void mm3_lua_ai_all_cars(void)
{
    uint32_t ecx = g_ecx, edx = g_edx;
    lua_return_number(option("MM3_AI_ALL_CARS", 1) || mm3_mod_same_car());
    g_ecx = ecx; g_edx = edx;
    g_esp += 4;   /* ret */
}

static void register_native(const char *name, uint32_t handler)
{
    uint32_t eax = g_eax, ecx = g_ecx, edx = g_edx, ebx = g_ebx;
    uint32_t esi = g_esi, edi = g_edi, esp = g_esp, ebp = g_ebp;
    uint32_t base = (esp - 0x200u) & ~0xFu, text = base + 0x100u, string = base + 0x80u;
    size_t i, length = strlen(name);
    for (i = 0; i <= length; ++i) MEM8(text + (uint32_t)i) = (uint8_t)name[i];
    g_esp = base;
    g_ecx = string;
    PUSH32(g_esp, text);
    PUSH32(g_esp, RETURN_SITE);
    sub_000125EA();
    g_ebx = MEM32(LUA_WRAPPER);
    PUSH32(g_esp, handler);
    PUSH32(g_esp, string);
    PUSH32(g_esp, RETURN_SITE);
    sub_002150BC();
    g_ecx = string;
    PUSH32(g_esp, 1);
    PUSH32(g_esp, RETURN_SITE);
    sub_000127A9();
    g_eax = eax; g_ecx = ecx; g_edx = edx; g_ebx = ebx;
    g_esi = esi; g_edi = edi; g_esp = esp; g_ebp = ebp;
}

void sub_0020646F(void)
{
    sub_0020646F_original();
    if (MEM32(LUA_WRAPPER)) register_native("mm3AiAllCars", MM3_LUA_AI_ALL_CARS_VA);
}
