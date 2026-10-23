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
static uint64_t s_prof_program_uploads, s_prof_program_reused;
static uint64_t s_prof_pipeline_issued, s_prof_pipeline_avoided;
static uint64_t s_prof_color_writes, s_prof_color_readonly;
static uint64_t s_prof_index_maps, s_prof_index_skipped;
static uint64_t s_prof_idle_calls, s_prof_idle_gpu, s_prof_idle_timer, s_prof_idle_failures;
static int64_t s_prof_idle_max, s_prof_idle_overshoot;
static uint64_t s_prof_sync_checks, s_prof_sync_bytes, s_prof_sync_changed, s_prof_sync_raw_skips;
static uint32_t s_prof_upload_last[7];
static uint64_t s_prof_upload_pixels;
static uint64_t s_prof_tex_hashes, s_prof_tex_hash_bytes;
/* The texture write-watch segment prints only with a watch; none here. */
typedef struct { void (*stats)(uint64_t *faults, uint64_t *armed_pages); } GwwOps;
static const GwwOps *s_watch;
static uint64_t s_prof_watch_clean, s_prof_watch_armed;
static uint64_t s_prof_watch_faults_last, s_prof_watch_pages_last, s_watch_missed;
static int watch_verify(void) { return 0; }
static uint64_t s_prof_vb_bytes, s_prof_vb_epoch_repeat, s_prof_vb_frame_repeat, s_prof_vb_prev_frame;
static uint64_t s_prof_vb_reused, s_prof_vconst_uploads, s_prof_epochs;
static int64_t s_prof_draw_setup, s_prof_draw_upload;
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
        s_prof_program_uploads = s_prof_pipeline_issued = s_prof_color_writes = s_prof_index_maps = 1;
        s_prof_idle_calls = s_prof_idle_gpu = 1;
        s_prof_sync_checks = s_prof_sync_bytes = s_prof_tex_hashes = s_prof_vb_bytes = 1;
        s_prof_vb_epoch_repeat = s_prof_vconst_uploads = s_prof_draw_setup = s_prof_draw_upload = 1;
        s_prof_vb_reused = s_prof_epochs = 1;
        s_prof_sync_changed = s_prof_upload_pixels = 1;
        s_prof_upload_last[0] = 0x00123000u;
        clock_ticks += 1000;
        prof_report();
        CHECK(clock_calls == 1001 && frequency_calls == 1 && output_calls > 0);
        for (i = 0; i < PROF_COUNT; i++) CHECK(!s_prof[i]);
        CHECK(!s_prof_verts && !s_prof_draws && !s_prof_ps_uploads && !s_prof_ps_reused);
        CHECK(!s_prof_light_draws && !s_prof_depth_views && !s_prof_local_depth_views && !s_prof_light_depth_aliases);
        CHECK(!s_prof_program_uploads && !s_prof_pipeline_issued && !s_prof_color_writes && !s_prof_index_maps);
        CHECK(!s_prof_idle_calls && !s_prof_idle_gpu);
        CHECK(!s_prof_sync_checks && !s_prof_sync_bytes && !s_prof_tex_hashes && !s_prof_vb_bytes);
        CHECK(!s_prof_vb_epoch_repeat && !s_prof_vconst_uploads && !s_prof_draw_setup && !s_prof_draw_upload);
        CHECK(!s_prof_vb_reused && !s_prof_epochs);
        CHECK(!s_prof_sync_changed && !s_prof_upload_pixels);
    }
    printf("PASS: renderer profiling '%s': one configuration read; %s\n", argv[1],
           enabled ? "enabled timings/output/reset preserved" : "zero timing calls and output for 1000 frames");
    return 0;
}
