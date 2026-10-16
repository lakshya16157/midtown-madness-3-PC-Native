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
/* Keep daylight authored and night fill independent of local lights.
 * The optional readability floor turns
 * even unlit surfaces into 75% of their texture color after LightApply's x4
 * multiplier, overwhelming headlights and street lamps at night. */
static unsigned s_ambient_lift = 0;
static unsigned s_even_lighting = 0;
static unsigned s_night_world_light = 25;

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
            fprintf(stderr, "[MM3_LIGHTING] invalid MM3_AMBIENT_LIFT; expected integer 0..16, using 0\n");
    }
    setting = getenv("MM3_EVEN_LIGHTING");
    if (setting && *setting) {
        if (setting[0] == '0' && !setting[1]) s_even_lighting = 0;
        else if (setting[0] == '1' && !setting[1]) s_even_lighting = 1;
        else
            fprintf(stderr, "[MM3_LIGHTING] invalid MM3_EVEN_LIGHTING; expected 0 or 1, using 0\n");
    }
    setting = getenv("MM3_NIGHT_WORLD_LIGHT");
    if (setting && *setting) {
        char *end;
        long value;
        errno = 0;
        value = strtol(setting, &end, 10);
        if (!errno && end != setting && !*end && value >= 0 && value <= 100)
            s_night_world_light = (unsigned)value;
        else
            fprintf(stderr, "[MM3_LIGHTING] invalid MM3_NIGHT_WORLD_LIGHT; expected integer 0..100, using 25\n");
    }
    fprintf(stderr, "[MM3_LIGHTING] world ambient lift=%u/255 even_lighting=%u world_rgb_floor=%u/255 night_world_light=%u%% local_light_accumulation=%s (MM3_EVEN_LIGHTING=1 enables the readability boost)\n",
            s_ambient_lift, s_even_lighting,
            s_even_lighting ? MM3_WORLD_LIGHT_FLOOR : 0,
            s_night_world_light,
            "retail");
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
    /* Native raceGetTimeOfDay (2125A0), the enum constructor (2501CD), and
     * the scene renderer (22F2DA) agree: 0 morning, 1 noon, 2 evening, 3 night.
     * Scale only the three world colors; the separate local-light buffer and
     * material-emission branch retain their authored strength. */
    if (MEM32(0x00407C30u) == 3u)
        color = mm3_world_light_scale(color, s_night_world_light);
    if (ambient) color = mm3_world_ambient_lift(color, s_ambient_lift);
    return s_even_lighting ? mm3_world_light_floor(color, MM3_WORLD_LIGHT_FLOOR) : color;
}

void sub_000FF19A(void)
{
    uint32_t caller = MEM32(g_esp);
    sub_000FF19A_original();
    /* Optional readability adjustments are scoped to the full-scene draw.
     * Night world fill is reduced independently of local illumination.
     * Daylight and material alpha pass through unchanged by default. */
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
    world_lighting_configure();
    /* Keep the native light-buffer/composite mode. Forcing mode 1 discarded
     * headlights and street-light illumination even when their effect nodes
     * were present. Local sources must remain independent of ambient light. */
    sub_00230DEE_original();
}
