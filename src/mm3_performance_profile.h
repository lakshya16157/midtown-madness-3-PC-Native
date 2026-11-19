#ifndef MM3_PERFORMANCE_PROFILE_H
#define MM3_PERFORMANCE_PROFILE_H

#include <ctype.h>
#include <stdlib.h>
#include "platform/win32_compat.h" /* _putenv_s: the CRT on Windows, compat layer elsewhere */
#ifndef _WIN32
#include <unistd.h>                /* sysconf */
#endif

typedef struct MM3PerformanceProfile {
    const char *name;
    const char *render_scale;
    const char *msaa;
    const char *draw_distance;
    const char *aniso;
    const char *fps_limit;
    const char *apu_dsp;    /* RECOMP_APU_DSP: the accurate sound processor costs ~1/3 of a core */
    const char *apu_hrtf;   /* RECOMP_APU_HRTF: the voice processor's 3D headphone filter */
    /* 1: don't wait each frame for the GPU's read-back pictures (MM3_NO_GPU_WAIT,
     * RECOMP_READBACK_HOLD=0). On a slow integrated GPU the wait adds the GPU's
     * frame to the CPU's; lamp glows can flash on windows. 0: the platform default. */
    int no_gpu_wait;
} MM3PerformanceProfile;

static inline int mm3_profile_name_equal(const char *value, const char *name)
{
    if (!value) return 0;
    while (*value && *name) {
        if (tolower((unsigned char)*value) != tolower((unsigned char)*name))
            return 0;
        ++value;
        ++name;
    }
    return *value == *name;
}

/* With no profile chosen (a direct or Steam launch before any is saved), a PC
 * with 4 or fewer logical processors starts on Lowest: a 2-core laptop has to
 * carry the game thread, the GPU executor and the sound chip's work. */
static inline const char *mm3_default_profile_name(void)
{
#ifdef _WIN32
    const char *count = getenv("NUMBER_OF_PROCESSORS");
    long n = count ? atol(count) : 0;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
#endif
    return n > 0 && n <= 4 ? "Lowest" : "Balanced";
}

static inline const MM3PerformanceProfile *mm3_performance_profile(const char *name)
{
    if (!name || !*name) name = mm3_default_profile_name();
#ifdef _WIN32
    static const MM3PerformanceProfile balanced = {"Balanced", "2.0", "4", "2", "16", "60", "1", "1"};
#else
    /* The SDL3 GPU renderer has no multisampling, and the same 2x scale costs
     * a handheld (Steam Deck) much more than the D3D11 renderer; 1.5x fits its
     * 1280x800 panel. */
    static const MM3PerformanceProfile balanced = {"Balanced", "1.5", "4", "2", "16", "60", "1", "1"};
#endif
    static const MM3PerformanceProfile quality = {"Quality", "3.0", "8", "2", "16", "60", "1", "1"};
    static const MM3PerformanceProfile low_power = {"LowPower", "1.5", "1", "1", "16", "30", "1", "1"};
    /* Slow PCs (issue #36): the Xbox's own 480 lines, no filtering extras, and
     * the voice-processor mix instead of the interpreted sound DSP, and 3D
     * sounds panned in stereo without the HRTF filter, and no per-frame wait for
     * the GPU's read-back pictures. */
    static const MM3PerformanceProfile lowest = {"Lowest", "1.0", "1", "1", "1", "30", "0", "0", 1};
    /* What the Xbox did: its 480 lines at 30 FPS, the original distances, and
     * the full sound chip (DSP effects and the HRTF filter). */
    static const MM3PerformanceProfile xbox = {"Xbox", "1.0", "1", "1", "1", "30", "1", "1", 0};
    if (mm3_profile_name_equal(name, quality.name)) return &quality;
    if (mm3_profile_name_equal(name, xbox.name)) return &xbox;
    if (mm3_profile_name_equal(name, low_power.name)) return &low_power;
    if (mm3_profile_name_equal(name, lowest.name)) return &lowest;
    return &balanced;
}

static inline int mm3_performance_default(const char *key, const char *value)
{
    const char *current = getenv(key);
    if (current && *current) return 0;
    return _putenv_s(key, value);
}

/* Each independently configured setting takes priority over the profile.
 * Return the failed key so startup can report an environment allocation error. */
static inline const char *mm3_apply_performance_defaults(const char *name)
{
    const MM3PerformanceProfile *profile = mm3_performance_profile(name);
    if (mm3_performance_default("RECOMP_RENDER_SCALE", profile->render_scale))
        return "RECOMP_RENDER_SCALE";
    if (mm3_performance_default("RECOMP_MSAA", profile->msaa))
        return "RECOMP_MSAA";
    if (mm3_performance_default("RECOMP_DRAW_DISTANCE", profile->draw_distance))
        return "RECOMP_DRAW_DISTANCE";
    if (mm3_performance_default("RECOMP_VIEW_DISTANCE", getenv("RECOMP_DRAW_DISTANCE")))
        return "RECOMP_VIEW_DISTANCE";
    if (mm3_performance_default("RECOMP_ANISO", profile->aniso))
        return "RECOMP_ANISO";
    if (mm3_performance_default("RECOMP_FPS_LIMIT", profile->fps_limit))
        return "RECOMP_FPS_LIMIT";
    if (mm3_performance_default("RECOMP_APU_DSP", profile->apu_dsp))
        return "RECOMP_APU_DSP";
    if (mm3_performance_default("RECOMP_APU_HRTF", profile->apu_hrtf))
        return "RECOMP_APU_HRTF";
    if (profile->no_gpu_wait && mm3_performance_default("RECOMP_READBACK_HOLD", "0"))
        return "RECOMP_READBACK_HOLD";
    if (mm3_performance_default("RECOMP_READBACK_FENCE", "1"))
        return "RECOMP_READBACK_FENCE";
    if (mm3_performance_default("RECOMP_AUDIO_HIRES_WAIT", "1"))
        return "RECOMP_AUDIO_HIRES_WAIT";
    if (mm3_performance_default("RECOMP_AUDIO_DSP_HANDOFF", "1"))
        return "RECOMP_AUDIO_DSP_HANDOFF";
    if (mm3_performance_default("RECOMP_BUFFERED_ASSET_READS", "1"))
        return "RECOMP_BUFFERED_ASSET_READS";
    /* RECOMP_READBACK_HOLD comes from the launcher's "Don't wait for the GPU"
     * (MM3_NO_GPU_WAIT, default off): without the hold the title reads lamp
     * glow visibility a frame late and window lights flash. */
#if defined(_WIN32) && defined(MM3_RENDERER_SDL3)
    /* On Windows SDL would pick Direct3D 12; this build is the Vulkan choice. */
    if (mm3_performance_default("RECOMP_SDL_GPU_DRIVER", "vulkan"))
        return "RECOMP_SDL_GPU_DRIVER";
#endif
    return NULL;
}

#endif
