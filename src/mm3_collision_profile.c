#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <errno.h>
#include <fenv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mm3_collision_profile.h"

#ifndef MM3_COLLISION_QPC
#define MM3_COLLISION_QPC QueryPerformanceCounter
#endif
#ifndef MM3_COLLISION_LOG
#define MM3_COLLISION_LOG(...) fprintf(stderr, __VA_ARGS__)
#endif

int mm3_collision_profile_on;
static int64_t s_frequency;
static volatile LONG64 s_calls, s_contacts, s_points, s_effects;
static __declspec(thread) struct {
    int64_t started, ticks, worst;
    uint64_t calls, contacts, points;
    unsigned slow_lines;
} s_window;

void mm3_collision_profile_init(void)
{
    int saved_errno = errno;
    DWORD saved_error = GetLastError();
    LARGE_INTEGER frequency;
    const char *setting = getenv("RECOMP_COLLISION_PROFILE");
    if (setting && !strcmp(setting, "1") &&
        QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0) {
        s_frequency = frequency.QuadPart;
        mm3_collision_profile_on = 1;
    }
    errno = saved_errno;
    SetLastError(saved_error);
}

MM3CollisionSample mm3_collision_profile_begin(uint32_t car, uint32_t contacts)
{
    MM3CollisionSample sample = {0, car, contacts};
    int saved_errno;
    DWORD saved_error;
    LARGE_INTEGER now;
    if (!mm3_collision_profile_on) return sample;
    saved_errno = errno;
    saved_error = GetLastError();
    if (MM3_COLLISION_QPC(&now) && now.QuadPart > 0) sample.started = now.QuadPart;
    errno = saved_errno;
    SetLastError(saved_error);
    return sample;
}

void mm3_collision_profile_end(MM3CollisionSample sample, uint32_t contacts)
{
    int saved_errno, report;
    DWORD saved_error;
    LARGE_INTEGER now;
    int64_t duration;
    uint32_t points;
    fenv_t fp;
    int fp_valid;
    if (!mm3_collision_profile_on) return;
    saved_errno = errno;
    saved_error = GetLastError();
    InterlockedIncrement64(&s_calls);
    points = contacts >= sample.contacts_before ? contacts - sample.contacts_before : 0;
    if (points) {
        InterlockedIncrement64(&s_contacts);
        InterlockedAdd64(&s_points, points);
    }
    if (!sample.started || !MM3_COLLISION_QPC(&now) || now.QuadPart < sample.started)
        goto done;
    duration = now.QuadPart - sample.started;
    if (!s_window.started || now.QuadPart < s_window.started) s_window.started = sample.started;
    ++s_window.calls;
    s_window.contacts += points != 0;
    s_window.points += points;
    s_window.ticks += duration;
    if (duration > s_window.worst) s_window.worst = duration;
    report = now.QuadPart - s_window.started >= s_frequency;
    if (!report && !(duration >= s_frequency / 500 && s_window.slow_lines < 8)) goto done;
    fp_valid = feholdexcept(&fp) == 0;
    if (duration >= s_frequency / 500 && s_window.slow_lines++ < 8) {
        MM3_COLLISION_LOG("[HITCH] phase=collision_contact qpc_ms=%.3f start_qpc_ms=%.3f "
            "duration_ms=%.3f car=%08X solid_points=%u\n",
            now.QuadPart * 1000.0 / s_frequency, sample.started * 1000.0 / s_frequency,
            duration * 1000.0 / s_frequency, sample.car, points);
    }
    if (report) {
        MM3_COLLISION_LOG("[COLLISION_PROFILE] qpc_ms=%.3f interval_ms=%.3f calls=%llu "
            "contact_events=%llu solid_points=%llu contact_ms=%.3f max_contact_ms=%.3f "
            "total_contact_events=%llu effects=%llu\n",
            now.QuadPart * 1000.0 / s_frequency,
            (now.QuadPart - s_window.started) * 1000.0 / s_frequency,
            (unsigned long long)s_window.calls, (unsigned long long)s_window.contacts,
            (unsigned long long)s_window.points, s_window.ticks * 1000.0 / s_frequency,
            s_window.worst * 1000.0 / s_frequency,
            (unsigned long long)InterlockedCompareExchange64(&s_contacts, 0, 0),
            (unsigned long long)InterlockedCompareExchange64(&s_effects, 0, 0));
        memset(&s_window, 0, sizeof s_window);
        s_window.started = now.QuadPart;
    }
    if (fp_valid) fesetenv(&fp);
done:
    errno = saved_errno;
    SetLastError(saved_error);
}

void mm3_collision_profile_effect(void)
{
    if (mm3_collision_profile_on) InterlockedIncrement64(&s_effects);
}

void mm3_collision_profile_snapshot(MM3CollisionProfileSnapshot *snapshot)
{
    if (!snapshot) return;
    snapshot->enabled = mm3_collision_profile_on;
    snapshot->calls = (uint64_t)InterlockedCompareExchange64(&s_calls, 0, 0);
    snapshot->contact_events = (uint64_t)InterlockedCompareExchange64(&s_contacts, 0, 0);
    snapshot->solid_points = (uint64_t)InterlockedCompareExchange64(&s_points, 0, 0);
    snapshot->effects = (uint64_t)InterlockedCompareExchange64(&s_effects, 0, 0);
}
