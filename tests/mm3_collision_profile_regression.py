"""Exercise production collision counters, clock failures and host-state safety.

Run after scripts/enter-env.ps1. Compiles a small native contract with fake
clocks/logging; no game, game data, graphics or audio devices are used.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = r'''
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <assert.h>
#include <errno.h>
#include <fenv.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int64_t clock_now = 1000000, frequency_value = 1000000;
static int clock_ok = 1, frequency_ok = 1;
static unsigned clock_calls, frequency_calls;
static char logs[32768];
static size_t log_size;
static BOOL fake_qpc(LARGE_INTEGER *out)
{
    ++clock_calls;
    out->QuadPart = clock_now;
    errno = ERANGE;
    SetLastError(0xBAD1);
    return clock_ok;
}
static BOOL fake_frequency(LARGE_INTEGER *out)
{
    ++frequency_calls;
    out->QuadPart = frequency_value;
    errno = ERANGE;
    SetLastError(0xBAD2);
    return frequency_ok;
}
static void capture_log(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int length = vsnprintf(logs + log_size, sizeof logs - log_size, format, args);
    va_end(args);
    assert(length >= 0 && (size_t)length < sizeof logs - log_size);
    log_size += (size_t)length;
    /* Formatting/observation must not leak errors or floating-point state. */
    assert(fesetround(FE_UPWARD) == 0);
    assert(feclearexcept(FE_ALL_EXCEPT) == 0);
    assert(feraiseexcept(FE_DIVBYZERO) == 0);
    errno = ERANGE;
    SetLastError(0xBAD3);
}
#define MM3_COLLISION_QPC fake_qpc
#define QueryPerformanceFrequency fake_frequency
#define MM3_COLLISION_LOG capture_log
#include "mm3_collision_profile.c"
#undef QueryPerformanceFrequency

static void host_state(void)
{
    assert(fesetround(FE_DOWNWARD) == 0);
    assert(feclearexcept(FE_ALL_EXCEPT) == 0);
    assert(feraiseexcept(FE_INEXACT) == 0);
    errno = EDOM;
    SetLastError(0x12345678);
}
static void check_host_state(void)
{
    assert(errno == EDOM && GetLastError() == 0x12345678);
    assert(fegetround() == FE_DOWNWARD);
    assert(fetestexcept(FE_ALL_EXCEPT) == FE_INEXACT);
}
static unsigned occurrences(const char *needle)
{
    unsigned count = 0;
    const char *p = logs;
    while ((p = strstr(p, needle)) != NULL) { ++count; p += strlen(needle); }
    return count;
}
static void reset_fixture(void)
{
    s_calls = s_contacts = s_points = s_effects = 0;
    memset(&s_window, 0, sizeof s_window);
    logs[0] = 0;
    log_size = 0;
    clock_calls = 0;
    clock_ok = 1;
}
static void contact(int64_t begin, int64_t end, uint32_t before, uint32_t after)
{
    clock_now = begin;
    host_state();
    MM3CollisionSample sample = mm3_collision_profile_begin(0x1234ABCD, before);
    check_host_state();
    clock_now = end;
    host_state();
    mm3_collision_profile_end(sample, after);
    check_host_state();
}
static MM3CollisionProfileSnapshot snapshot(void)
{
    MM3CollisionProfileSnapshot result;
    host_state();
    mm3_collision_profile_snapshot(&result);
    check_host_state();
    mm3_collision_profile_snapshot(NULL);
    return result;
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    if (!strcmp(argv[1], "off")) {
        const char *settings[] = {"", "0", "true", "2", "01"};
        for (unsigned i = 0; i < sizeof settings / sizeof settings[0]; ++i) {
            assert(!_putenv_s("RECOMP_COLLISION_PROFILE", settings[i]));
            host_state();
            mm3_collision_profile_init();
            check_host_state();
            contact(1000, 5000, 0, 4);
            host_state();
            mm3_collision_profile_effect();
            check_host_state();
        }
        MM3CollisionProfileSnapshot p = snapshot();
        assert(!p.enabled && !p.calls && !p.contact_events && !p.solid_points && !p.effects);
        assert(!clock_calls && !frequency_calls && !log_size);
        puts("PASS: default off has no clocks, logs or counter updates");
        return 0;
    }
    assert(!_putenv_s("RECOMP_COLLISION_PROFILE", "1"));
    if (!strcmp(argv[1], "frequency_failure")) frequency_ok = 0;
    if (!strcmp(argv[1], "frequency_zero")) frequency_value = 0;
    host_state();
    mm3_collision_profile_init();
    check_host_state();
    assert(frequency_calls == 1);
    if (!frequency_ok || frequency_value <= 0) {
        contact(1000, 5000, 0, 4);
        assert(!snapshot().enabled && !clock_calls && !log_size);
        puts("PASS: unusable frequency leaves instrumentation disabled");
        return 0;
    }
    assert(snapshot().enabled);
    contact(1000000, 1000010, 3, 7);
    contact(1000100, 1000110, 7, 2); /* Title resets its static contact counter. */
    contact(1000200, 1000210, 2, 5);
    for (unsigned i = 0; i < 3; ++i) {
        host_state(); mm3_collision_profile_effect(); check_host_state();
    }
    MM3CollisionProfileSnapshot p = snapshot();
    assert(p.calls == 3 && p.contact_events == 2 && p.solid_points == 7 && p.effects == 3);
    assert(clock_calls == 6 && !log_size);

    reset_fixture();
    clock_ok = 0;
    host_state();
    MM3CollisionSample failed = mm3_collision_profile_begin(1, 0);
    check_host_state();
    assert(!failed.started && clock_calls == 1);
    clock_ok = 1;
    host_state(); mm3_collision_profile_end(failed, 2); check_host_state();
    assert(clock_calls == 1); /* Failed begin must not read another clock. */
    clock_now = 1000;
    MM3CollisionSample sample = mm3_collision_profile_begin(1, 2);
    clock_ok = 0;
    host_state(); mm3_collision_profile_end(sample, 5); check_host_state();
    clock_ok = 1;
    contact(2000, 1999, 5, 9); /* Reversed sample: count events, reject timing. */
    contact(0, 100, 9, 9); /* Zero timestamps are also rejected. */
    p = snapshot();
    assert(p.calls == 4 && p.contact_events == 3 && p.solid_points == 9);
    assert(!s_window.calls && !log_size);

    reset_fixture();
    for (unsigned i = 0; i < 20; ++i)
        contact(1000000 + i * 10000, 1003000 + i * 10000, i, i + 1);
    assert(occurrences("phase=collision_contact") == 8);
    assert(!occurrences("[COLLISION_PROFILE]"));
    assert(strstr(logs, "car=1234ABCD solid_points=1"));
    contact(1999999, 2000000, 20, 20); /* One-second summary renews the cap. */
    assert(occurrences("[COLLISION_PROFILE]") == 1);
    assert(strstr(logs, "calls=21 contact_events=20 solid_points=20"));
    assert(!s_window.calls && !s_window.contacts && !s_window.points && !s_window.slow_lines);
    for (unsigned i = 0; i < 20; ++i)
        contact(2010000 + i * 10000, 2013000 + i * 10000, 0, 1);
    assert(occurrences("phase=collision_contact") == 16);
    assert(occurrences("[COLLISION_PROFILE]") == 1);
    p = snapshot();
    assert(p.calls == 41 && p.contact_events == 40 && p.solid_points == 40);
    puts("PASS: collision event deltas/resets, failed/reversed clocks, renewable 8-line budget and host-state preservation");
    return 0;
}
'''


def main():
    if not shutil.which("cl"):
        raise SystemExit("Run scripts/enter-env.ps1 first.")
    with tempfile.TemporaryDirectory(prefix="mm3-collision-profile-") as temporary:
        folder = Path(temporary)
        assert folder.resolve().is_relative_to(Path(tempfile.gettempdir()).resolve())
        source, executable = folder / "contract.c", folder / "contract.exe"
        source.write_text(SOURCE)
        subprocess.run(
            ["cl", "/nologo", "/W4", "/WX", "/Od", "/fp:strict", "/std:c11",
             "/D_CRT_SECURE_NO_WARNINGS", "/I" + str(ROOT / "src"),
             "/Fo:" + str(folder / "contract.obj"), "/Fe:" + str(executable), str(source)],
            cwd=folder, check=True,
        )
        for mode in ("off", "frequency_failure", "frequency_zero", "on"):
            subprocess.run([str(executable), mode], cwd=folder, check=True)


if __name__ == "__main__":
    main()
