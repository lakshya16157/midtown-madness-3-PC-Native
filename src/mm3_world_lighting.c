#if defined(_WIN32)
#include <windows.h>
#else
/* The runtime's POSIX host layer supplies the same once primitive, backed
 * by pthreads and already linked transitively through xboxrecomp. */
#include "platform/win32_compat.h"
#endif
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include "recomp_types.h"
#include "mm3_world_lighting.h"

extern void sub_000FF19A_original(void);
extern void sub_000FF324_original(void);
extern void sub_000FF15C_original(void);
extern void sub_00230DEE_original(void);

static INIT_ONCE s_ambient_once = INIT_ONCE_STATIC_INIT;
static unsigned s_ambient_lift = 3;
static unsigned s_even_lighting = 1;

static BOOL CALLBACK ambient_configure(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    const char *setting = getenv("MM3_AMBIENT_LIFT");
    (void)once; (void)parameter; (void)context;
    if (setting && *setting) {
        char *end;
        long value;
        errno = 0;
        value = strtol(setting, &end, 10);
        if (!errno && end != setting && !*end && value >= 0 && value <= 16)
            s_ambient_lift = (unsigned)value;
        else
            fprintf(stderr, "[MM3_LIGHTING] invalid MM3_AMBIENT_LIFT; expected integer 0..16, using 3\n");
    }
    setting = getenv("MM3_EVEN_LIGHTING");
    if (setting && *setting) {
        if (setting[0] == '0' && !setting[1]) s_even_lighting = 0;
        else if (setting[0] != '1' || setting[1])
            fprintf(stderr, "[MM3_LIGHTING] invalid MM3_EVEN_LIGHTING; expected 0 or 1, using 1\n");
    }
    fprintf(stderr, "[MM3_LIGHTING] world ambient lift=%u/255 even_lighting=%u world_rgb_floor=%u/255 local_light_accumulation=%s (MM3_EVEN_LIGHTING=0 restores legacy lighting)\n",
            s_ambient_lift, s_even_lighting,
            s_even_lighting ? MM3_WORLD_LIGHT_FLOOR : 0,
            s_even_lighting ? "off" : "retail");
    return TRUE;
}

static void world_lighting_configure(void)
{
#if defined(_WIN32)
    DWORD saved_error = GetLastError();
#endif
    int saved_errno = errno;
    InitOnceExecuteOnce(&s_ambient_once, ambient_configure, NULL, NULL);
    errno = saved_errno;
#if defined(_WIN32)
    SetLastError(saved_error);
#endif
}

static uint32_t world_light_color(uint32_t color, unsigned ambient)
{
    world_lighting_configure();
    if (ambient) color = mm3_world_ambient_lift(color, s_ambient_lift);
    return s_even_lighting ? mm3_world_light_floor(color, MM3_WORLD_LIGHT_FLOOR) : color;
}

void sub_000FF19A(void)
{
    uint32_t caller = MEM32(g_esp);
    sub_000FF19A_original();
    /* Ambient alone has zero weight on fully diffuse/backlit materials.
     * Scope all three color floors to the native full-scene LightApply draw,
     * preserving alpha, brighter authored colors and unrelated getter calls. */
    if (caller == 0x00230F75u) g_eax = world_light_color(g_eax, 1);
}

void sub_000FF324(void)
{
    uint32_t caller = MEM32(g_esp);
    sub_000FF324_original();
    if (caller == 0x00230F8Du) g_eax = world_light_color(g_eax, 0);
}

void sub_000FF15C(void)
{
    uint32_t caller = MEM32(g_esp);
    sub_000FF15C_original();
    if (caller == 0x00230FA6u) g_eax = world_light_color(g_eax, 0);
}

void sub_00230DEE(void)
{
    uint32_t renderer = g_ecx, mode = 0;
    unsigned even;
    world_lighting_configure();
    /* The native draw returns before touching its renderer when view is null. */
    even = s_even_lighting && renderer && MEM32(g_esp + 4);
    if (even) {
        mode = MEM32(renderer + 4);
        /* Native mode 1 skips the local light-buffer call before LightApply
         * (230E8B) and after it (2312AD). The draw still resets +5884 and uses
         * its ordinary non-textured-light composite. LightApplyTex's added
         * t1 accumulation would otherwise retain the bright patch ahead of
         * the car even with a global illumination floor. This mode does not
         * change headlights' authored emissive meshes or visible lamp state. */
        MEM32(renderer + 4) = 1;
    }
    sub_00230DEE_original();
    if (even) MEM32(renderer + 4) = mode;
}
