/* Actual renderer profiler bodies are extracted by the companion Python runner.
 * Timing/environment/output boundaries are mocked to verify the quiet path. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
typedef struct LARGE_INTEGER { int64_t QuadPart; } LARGE_INTEGER;
enum { PROF_VERTEX, PROF_TEXTURE, PROF_DRAW, PROF_SYNC, PROF_WRITEBACK,
       PROF_PRESENT, PROF_PACE, PROF_EXEC, PROF_COUNT };
static const char *const s_prof_name[PROF_COUNT] = {
    "vertex", "texture", "draw", "cpu-sync", "writeback", "present", "pace", "exec(all)" };
static int64_t s_prof[PROF_COUNT];
static uint64_t s_prof_verts, s_prof_draws, s_prof_ps_uploads, s_prof_ps_reused;
static uint64_t s_prof_light_draws, s_prof_depth_views, s_prof_local_depth_views;
static uint64_t s_prof_light_depth_aliases;
static uint32_t s_prof_light_source[3], s_prof_light_target[3];
static int s_prof_on = -1;
static const char *setting;
static unsigned env_calls, clock_calls, frequency_calls, output_calls;
static int64_t clock_ticks = 1000;

static char *test_getenv(const char *key)
{
    CHECK(strcmp(key, "RECOMP_D3D_PROFILE") == 0);
    env_calls++;
    return (char *)setting;
}
static int64_t qpc(void) { clock_calls++; return clock_ticks; }
static int QueryPerformanceFrequency(LARGE_INTEGER *frequency)
{ frequency_calls++; frequency->QuadPart = 1000; return 1; }
static int test_fprintf(FILE *file, const char *format, ...)
{ CHECK(file == stderr); CHECK(format != NULL); output_calls++; return 1; }
static int test_fputc(int character, FILE *file)
{ CHECK(character == '\n' && file == stderr); output_calls++; return character; }
static int test_fflush(FILE *file)
{ CHECK(file == stderr); output_calls++; return 0; }

#define getenv test_getenv
#define fprintf test_fprintf
#define fputc test_fputc
#define fflush test_fflush
#include "production_profile.inc"
#undef getenv
#undef fprintf
#undef fputc
#undef fflush

int main(int argc, char **argv)
{
    unsigned i;
    int enabled;
    CHECK(argc == 2);
    setting = strcmp(argv[1], "unset") ? argv[1] : NULL;
    enabled = setting && *setting && *setting != '0';
    for (i = 0; i < 1000; i++) {
        CHECK(nv2a_d3d_prof_enabled() == enabled);
        prof_report();
    }
    CHECK(env_calls == 1);
    if (!enabled) {
        CHECK(!clock_calls && !frequency_calls && !output_calls);
    } else {
        CHECK(clock_calls == 1000 && frequency_calls == 1 && !output_calls);
        for (i = 0; i < PROF_COUNT; i++) s_prof[i] = 1;
        s_prof_verts = s_prof_draws = s_prof_ps_uploads = s_prof_ps_reused = 1;
        s_prof_light_draws = s_prof_depth_views = s_prof_local_depth_views = s_prof_light_depth_aliases = 1;
        clock_ticks += 1000;
        prof_report();
        CHECK(clock_calls == 1001 && frequency_calls == 1 && output_calls > 0);
        for (i = 0; i < PROF_COUNT; i++) CHECK(!s_prof[i]);
        CHECK(!s_prof_verts && !s_prof_draws && !s_prof_ps_uploads && !s_prof_ps_reused);
        CHECK(!s_prof_light_draws && !s_prof_depth_views && !s_prof_local_depth_views && !s_prof_light_depth_aliases);
    }
    printf("PASS: renderer profiling '%s': one configuration read; %s\n", argv[1],
           enabled ? "enabled timings/output/reset preserved" : "zero timing calls and output for 1000 frames");
    return 0;
}
