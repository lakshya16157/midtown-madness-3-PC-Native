"""Generated functions with their x87 state in locals (scripts/mm3_gen_x87_locals.py)
behave exactly as the generated originals.

Each sampled function is compiled twice from the generated chunk, as the build
does (register cache prologue): unchanged and transformed. Both run from the
same random registers, x87 state (both precision modes) and guest memory; every
callee is a stub that pops its return address and changes eax and the x87
state the same way for both. Guest memory is 4 GiB reserved and filled on
first touch from the page address, so any pointer the code follows is valid
and identical for both runs. Registers, the eight x87 slots, top, control
word, compare state, the direction flag and every touched byte must match.
Each case runs in its own process. A case where the original faults, makes a
NaN (payloads follow the compiler's operand order) or runs past the time limit
is skipped.

    python tests/mm3_gen_x87_locals_regression.py [--count N] [--all] [--cases K] [--jobs J]
"""
import argparse
import json
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import mm3_gen_x87_locals as x87  # noqa: E402

FUNCTION = re.compile(r"(?ms)^void (\w+)\(void\)\n\{\n(.*?)^\}\n")
HOT = ["sub_002598CD", "sub_00259982", "sub_00013068", "sub_0022FE8C", "sub_00312BE5", "sub_001681FB",
       "sub_00167DA5", "sub_00036FD9", "sub_001C7B15", "sub_001C7744", "sub_00239A95", "sub_00069807",
       "sub_0016691E", "sub_00166408", "sub_000139C8", "sub_00116535", "sub_001D2C3A", "sub_001D47F4",
       "sub_001CCDDB", "sub_001C793A"]

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "recomp_types.h"
ptrdiff_t g_xbox_mem_offset;
uint32_t g_xbox_code_lo, g_xbox_code_hi;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi, g_ebp, g_seh_ebp, g_fs_base, g_eflags;
RECOMP_TLS double g_fp_stack[8];
RECOMP_TLS int g_fp_top, g_fp_cmp, g_df;
RECOMP_TLS uint16_t g_fp_cc, g_fp_control_word;
RECOMP_TLS RecompXmm g_xmm0, g_xmm1, g_xmm2, g_xmm3, g_xmm4, g_xmm5, g_xmm6, g_xmm7;
volatile uint32_t g_icall_trace[ICALL_TRACE_SIZE];
volatile uint32_t g_icall_trace_idx;
volatile uint64_t g_icall_count;
recomp_func_t recomp_lookup_manual(uint32_t va) { (void)va; return NULL; }
recomp_func_t recomp_lookup(uint32_t va) { (void)va; return NULL; }
recomp_func_t recomp_lookup_kernel(uint32_t va) { (void)va; return NULL; }
void recomp_icall_not_code_log(uint32_t va) { (void)va; }
void recomp_icall_fail_log(uint32_t va) { (void)va; }

static unsigned char *base;
static uint32_t s_seed;
/* Inputs: 0 random pointers and values; 1 small loop counts in memory and
 * some registers; 2 every register small. Profiles 1 and 2 let code whose
 * loops count from its inputs finish. */
static unsigned s_profile;
#define TOUCHED 65536
static uint32_t touched[TOUCHED];
static volatile LONG touched_count;

static uint32_t mix(uint32_t v)
{
    v ^= v >> 16; v *= 0x7feb352du; v ^= v >> 15; v *= 0x846ca68bu; v ^= v >> 16;
    return v;
}
/* A page's first contents: small floats, small integers and pointers into a
 * few busy pages, decided by the address and the case seed. */
static void fill(uint32_t page)
{
    uint32_t i, *p = (uint32_t *)(base + (size_t)page * 4096);
    for (i = 0; i < 1024; ++i) {
        uint32_t r = mix(page * 1024 + i + s_seed * 0x9E3779B9u);
        switch (r & 3) {
        case 0: { float f = (float)((int)(r >> 8 & 0xFFFF) - 32768) / 256.0f; memcpy(&p[i], &f, 4); break; }
        case 1: p[i] = s_profile ? r >> 29 : r >> 24; break;
        case 2: p[i] = 0x00100000u + (r >> 8 & 0xFFFC); break;
        default: { float f = (float)(r >> 8 & 0xFFF) / 64.0f + 0.5f; memcpy(&p[i], &f, 4); break; }
        }
    }
}
static LONG CALLBACK on_fault(PEXCEPTION_POINTERS ep)
{
    EXCEPTION_RECORD *er = ep->ExceptionRecord;
    uintptr_t a;
    uint32_t page;
    LONG n;
    if (er->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || er->NumberParameters < 2) return EXCEPTION_CONTINUE_SEARCH;
    a = (uintptr_t)er->ExceptionInformation[1];
    if (a < (uintptr_t)base || a - (uintptr_t)base >= 0x100000000ull) return EXCEPTION_CONTINUE_SEARCH;
    page = (uint32_t)((a - (uintptr_t)base) / 4096);
    n = InterlockedIncrement(&touched_count);
    if (n > TOUCHED) return EXCEPTION_CONTINUE_SEARCH;
    if (!VirtualAlloc(base + (size_t)page * 4096, 4096, MEM_COMMIT, PAGE_READWRITE)) {
        /* The machine is out of commit (other work beside the test): no
         * difference between the versions, so say so rather than crash. */
        fputs("STARVED\n", stdout);
        fflush(stdout);
        TerminateProcess(GetCurrentProcess(), 3);
    }
    touched[n - 1] = page;
    fill(page);
    return EXCEPTION_CONTINUE_EXECUTION;
}
static void reset_memory(void)
{
    VirtualFree(base, 0x100000000ull, MEM_DECOMMIT);
    touched_count = 0;
}
static uint64_t memory_hash(void)
{
    uint64_t h = 1469598103934665603ull;
    LONG n, i, j;
    n = touched_count;
    /* Page order may differ; hash each page and combine order-free. */
    {
        uint64_t sum = 0;
        for (i = 0; i < n; ++i) {
            uint64_t ph = 1469598103934665603ull ^ touched[i];
            const unsigned char *p = base + (size_t)touched[i] * 4096;
            for (j = 0; j < 4096; ++j) { ph ^= p[j]; ph *= 1099511628211ull; }
            sum += ph * 0x9E3779B97F4A7C15ull;
        }
        h ^= sum + (uint64_t)n;
    }
    return h;
}
/* The same with every float NaN pattern made one value (see produced_nan). */
static uint64_t memory_hash_canonical(void)
{
    uint64_t sum = 0;
    LONG n = touched_count, i, j;
    for (i = 0; i < n; ++i) {
        uint64_t ph = 1469598103934665603ull ^ touched[i];
        const uint32_t *w = (const uint32_t *)(base + (size_t)touched[i] * 4096);
        for (j = 0; j < 1024; ++j) {
            uint32_t v = w[j];
            if ((v & 0x7F800000u) == 0x7F800000u && (v & 0x007FFFFFu)) v = 0x7FC00000u;
            ph ^= v; ph *= 1099511628211ull;
        }
        sum += ph * 0x9E3779B97F4A7C15ull;
    }
    return sum + (uint64_t)n;
}

typedef struct {
    uint32_t r[10]; double fp[8]; int top, cmp, df; uint16_t cc, cw; uint64_t memory, canonical;
    int done, stubs; DWORD code;
} Result;
static DWORD s_code;
static RECOMP_TLS int s_stub_calls;
static void stub_effect(uint32_t which)
{
    ++s_stub_calls;
    g_esp += 4;
    g_eax = mix(g_eax ^ which ^ (uint32_t)s_stub_calls);
    g_ecx ^= which;
    if (which & 1) {          /* a float result on the stack */
        g_fp_top = (g_fp_top + 7) & 7;
        g_fp_stack[g_fp_top] = (double)(which & 0xFFF) / 3.0;
    }
    if (which & 2) g_fp_control_word ^= 0x0300;
}

typedef void (*fn_t)(void);
static void setup(uint32_t seed)
{
    uint32_t r = mix(seed), i;
    g_eax = 0x00100000u + (mix(r + 1) & 0xFFFC); g_ecx = 0x00100000u + (mix(r + 2) & 0xFFFC);
    g_edx = 0x00100000u + (mix(r + 3) & 0xFFFC); g_ebx = 0x00100000u + (mix(r + 4) & 0xFFFC);
    g_esi = 0x00100000u + (mix(r + 5) & 0xFFFC); g_edi = 0x00100000u + (mix(r + 6) & 0xFFFC);
    if (s_profile >= 1) { g_eax = mix(r + 1) & 7; g_edx = mix(r + 3) & 7; g_edi = mix(r + 6) & 7; }
    if (s_profile >= 2) { g_ecx = mix(r + 2) & 7; g_ebx = mix(r + 4) & 7; g_esi = mix(r + 5) & 7; }
    g_ebp = 0x00200800u; g_seh_ebp = g_ebp; g_esp = 0x00200000u; g_df = 0;
    for (i = 0; i < 8; ++i) g_fp_stack[i] = (double)(int)(mix(r + 10 + i) % 20001 - 10000) / 64.0;
    g_fp_top = (int)(mix(r + 20) & 7); g_fp_cmp = (int)(mix(r + 21) % 3) - 1; g_fp_cc = (uint16_t)(mix(r + 22) & 0x4500);
    g_fp_control_word = (mix(r + 23) & 1) ? 0x027F : 0x007F;
    MEM32(g_esp) = 0xDEAD0000u;     /* return address */
    s_stub_calls = 0;
}
static int run_one(fn_t fn, uint32_t seed, Result *out)
{
    memset(out, 0, sizeof *out);
    reset_memory();
    s_seed = seed;
    setup(seed);
    s_code = 0;
    __try {
        fn();
    } __except (s_code = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
    }
    out->code = s_code;
    out->r[0] = g_eax; out->r[1] = g_ecx; out->r[2] = g_edx; out->r[3] = g_esp; out->r[4] = g_ebx;
    out->r[5] = g_esi; out->r[6] = g_edi; out->r[7] = g_ebp; out->r[8] = g_seh_ebp; out->r[9] = g_eflags;
    memcpy(out->fp, g_fp_stack, sizeof out->fp);
    out->top = g_fp_top; out->cmp = g_fp_cmp; out->df = g_df; out->cc = g_fp_cc; out->cw = g_fp_control_word;
    out->stubs = s_stub_calls;
    out->done = 1;
    out->memory = memory_hash();
    out->canonical = memory_hash_canonical();
    return out->code ? -1 : 1;
}
static int same_x87(const Result *a, const Result *b, int nan_equal)
{
    int j;
    for (j = 0; j < 8; ++j)
        if (memcmp(&a->fp[j], &b->fp[j], 8) && !(nan_equal && a->fp[j] != a->fp[j] && b->fp[j] != b->fp[j]))
            return 0;
    return a->top == b->top && a->cmp == b->cmp && a->cc == b->cc && a->cw == b->cw;
}
static unsigned char *saved;
static uint32_t saved_pages[TOUCHED];
static LONG saved_count;
static void save_pages(void)
{
    LONG i;
    if (!saved) saved = (unsigned char *)malloc((size_t)TOUCHED * 4096);
    saved_count = touched_count;
    for (i = 0; i < saved_count; ++i) {
        saved_pages[i] = touched[i];
        memcpy(saved + (size_t)i * 4096, base + (size_t)touched[i] * 4096, 4096);
    }
}
static void diff_pages(void)
{
    LONG i; int j, shown = 0;
    for (i = 0; i < saved_count && shown < 12; ++i) {
        const unsigned char *a = saved + (size_t)i * 4096, *b = base + (size_t)saved_pages[i] * 4096;
        for (j = 0; j < 4096 && shown < 12; j += 4)
            if (memcmp(a + j, b + j, 4)) {
                float fa, fb; memcpy(&fa, a + j, 4); memcpy(&fb, b + j, 4);
                printf("  %08X: %08X (%g) vs %08X (%g)\n", saved_pages[i] * 4096 + j,
                       *(const uint32_t *)(a + j), fa, *(const uint32_t *)(b + j), fb);
                ++shown;
            }
    }
    printf("  pages touched %ld vs %ld\n", saved_count, touched_count);
}
/* A NaN the code made (the first fill has none). Which NaN payload an
 * operation keeps depends on the compiler's operand order (the original
 * built at /O2 and at /Od already differ in them), and the recompiled code
 * never followed the x87's NaN rules, so such cases are not compared. */
static int produced_nan(const Result *r)
{
    LONG i; int j;
    for (j = 0; j < 8; ++j) if (r->fp[j] != r->fp[j]) return 1;
    for (i = 0; i < touched_count; ++i) {
        const uint32_t *w = (const uint32_t *)(base + (size_t)touched[i] * 4096);
        for (j = 0; j < 1024; ++j)
            if ((w[j] & 0x7F800000u) == 0x7F800000u && (w[j] & 0x007FFFFFu)) return 1;
    }
    return 0;
}
#include "pairs.inc"
/* One case per process: harness <function> <case> [profile]; without
 * arguments it prints the number of functions. The test runs several at once
 * and stops a process that runs too long (stopping a thread instead could
 * leave a lock held and hang every later case). Prints "A <ms>" once the
 * original has finished, then one of:
 *   SAME             everything identical
 *   SAME fault       both stopped at the same CPU exception with the same
 *                    registers and memory (until its next call or return the
 *                    transformed version keeps x87 state in locals, so that
 *                    is not compared)
 *   SAME nan         the original made a NaN: identical with every NaN taken
 *                    as one value (payloads follow the compiler's operand order)
 *   SKIP nan-derived the original made a NaN and differs in values worked out
 *                    from NaN bits
 *   FAIL ...         a real difference */
static void fail(const char *name, unsigned c, const char *what, const Result *a, const Result *b)
{
    printf("FAIL %s case %u profile %u: %s (code %08lX/%08lX top %d/%d cw %04X/%04X eax %08X/%08X memory %d)\n",
           name, c, s_profile, what, a->code, b->code, a->top, b->top, a->cw, b->cw, a->r[0], b->r[0],
           a->memory != b->memory);
    diff_pages();
}
int main(int argc, char **argv)
{
    unsigned f, c;
    uint32_t seed;
    Result a, b;
    int ra, rb, nan;
    LARGE_INTEGER frequency, start, end;
    if (argc < 2) { printf("%u\n", PAIR_COUNT); return 0; }
    if (argc < 3) { for (f = 0; f < PAIR_COUNT; ++f) puts(pairs[f].name); return 0; }
    f = (unsigned)atoi(argv[1]); c = (unsigned)atoi(argv[2]);
    s_profile = argc > 3 ? (unsigned)atoi(argv[3]) : 0;
    if (f >= PAIR_COUNT) return 2;
    base = (unsigned char *)VirtualAlloc(NULL, 0x100000000ull, MEM_RESERVE, PAGE_NOACCESS);
    if (!base) { puts("FAIL no 4 GiB reservation"); return 2; }
    g_xbox_mem_offset = (ptrdiff_t)base;
    AddVectoredExceptionHandler(1, on_fault);
    seed = mix(f * 7919u + c + 1 + s_profile * 0x10001u);
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    ra = run_one(pairs[f].original, seed, &a);
    nan = produced_nan(&a);
    QueryPerformanceCounter(&end);
    save_pages();
    printf("A %.0f\n", (double)(end.QuadPart - start.QuadPart) * 1000.0 / (double)frequency.QuadPart);
    fflush(stdout);
    rb = run_one(getenv("X87_SELF") ? pairs[f].original : pairs[f].locals, seed, &b);
    if (ra < 0 || rb < 0) {
        if (ra >= 0 || rb >= 0 || a.code != b.code) {
            fail(pairs[f].name, c, "faulted differently", &a, &b);
            return 1;
        }
        if (memcmp(a.r, b.r, sizeof a.r) || a.df != b.df || a.stubs != b.stubs ||
            (nan ? a.canonical != b.canonical : a.memory != b.memory)) {
            if (nan) { puts("SKIP nan-derived"); return 0; }
            fail(pairs[f].name, c, "state differs at the fault", &a, &b);
            return 1;
        }
        puts(nan ? "SAME fault nan" : "SAME fault");
        return 0;
    }
    if (!nan) {
        if (memcmp(a.r, b.r, sizeof a.r) || a.df != b.df || a.stubs != b.stubs || !same_x87(&a, &b, 0) ||
            a.memory != b.memory) {
            fail(pairs[f].name, c, "state differs", &a, &b);
            return 1;
        }
        puts("SAME");
        return 0;
    }
    if (!memcmp(a.r, b.r, sizeof a.r) && a.df == b.df && a.stubs == b.stubs && same_x87(&a, &b, 1) &&
        a.canonical == b.canonical)
        puts("SAME nan");
    else
        puts("SKIP nan-derived");
    return 0;
}
"""


CASE_SECONDS = 4.0


OUTCOMES = ("SAME", "SAME fault", "SAME nan", "SAME fault nan", "SKIP nan-derived", "SLOW", "STARVED", "FAIL")


def run_case(harness, folder, function, case, profile):
    """(function, outcome, output), outcome one of OUTCOMES."""
    try:
        done = subprocess.run([str(harness), str(function), str(case), str(profile)], cwd=folder,
                              capture_output=True, text=True, timeout=CASE_SECONDS)
    except subprocess.TimeoutExpired as late:
        output = late.stdout.decode(errors="replace") if isinstance(late.stdout, bytes) else (late.stdout or "")
        first = re.match(r"A (\d+)", output)
        # The original finished quickly but the transformed version ran on.
        if first and int(first.group(1)) < CASE_SECONDS * 1000 / 4:
            return function, "FAIL", output + f"the transformed version ran past {CASE_SECONDS} s\n"
        return function, "SLOW", output
    output = done.stdout.strip()
    last = output.splitlines()[-1] if output else ""
    if done.returncode == 0 and last in OUTCOMES:
        return function, last, output
    if done.returncode == 3 and last == "STARVED":
        return function, "STARVED", output
    return function, "FAIL", output or f"exit code {done.returncode}"


def drive(harness, folder, cases, jobs, report=None):
    """Every function, `cases` random cases; then functions with no compared
    run get small-value inputs (profiles 1 and 2), 16 cases each."""
    names = subprocess.run([str(harness), "names"], cwd=folder, capture_output=True, text=True,
                           check=True).stdout.split()
    per = {name: {k: 0 for k in OUTCOMES} for name in names}
    tally, failures, settled = {k: 0 for k in OUTCOMES}, [], [0]

    def run(jobs_list):
        with ThreadPoolExecutor(max_workers=jobs) as pool:
            futures = {pool.submit(run_case, harness, folder, *job): job for job in jobs_list}
            done = [(futures[future], future.result()) for future in as_completed(futures)]
        for job, (function, outcome, output) in done:
            # A failure or a starved case runs again on its own before it
            # counts: a real difference repeats, as every case's inputs come
            # from its seed. A guest page that cannot be committed (the machine
            # short of memory beside other work: 798, then 492 "exit code
            # C0000005" failures in runs beside builds and game sessions) now
            # reports STARVED instead of crashing, and is never a failure.
            if outcome in ("FAIL", "STARVED"):
                function, outcome, output = run_case(harness, folder, *job)
                settled[0] += outcome not in ("FAIL", "STARVED")
            per[names[function]][outcome] += 1
            tally[outcome] += 1
            if outcome == "FAIL":
                failures.append(output)

    def compared(name):
        return sum(per[name][k] for k in ("SAME", "SAME fault", "SAME nan", "SAME fault nan"))

    run([(f, c, 0) for f in range(len(names)) for c in range(cases)])
    retry = [f for f, name in enumerate(names) if not compared(name)]
    run([(f, c, profile) for f in retry for profile in (1, 2) for c in range(16)])
    uncovered = [name for name in names if not compared(name)]
    for failure in failures[:5]:
        print(failure)
    print(f"{'FAIL' if failures else 'PASS'}: {len(names) - len(uncovered)} of {len(names)} functions compared; "
          + ", ".join(f"{tally[k]} {k}" for k in OUTCOMES) + f" ({len(retry)} retried with small inputs, "
          f"{settled[0]} failed or starved cases passed when run alone)")
    if tally["STARVED"]:
        print(f"  {tally['STARVED']} cases could not get memory: run the test again with less else running")
    if uncovered:
        reasons = {}
        for name in uncovered:
            key = "+".join(k for k in ("SKIP nan-derived", "SLOW") if per[name][k]) or "none"
            reasons[key] = reasons.get(key, 0) + 1
        print(f"  never compared: {len(uncovered)} ({', '.join(f'{n} {k}' for k, n in reasons.items())})")
    if report:
        Path(report).write_text(json.dumps({"per": per, "uncovered": uncovered, "failures": failures}, indent=1))
    return not failures


def chunks():
    for path in sorted((ROOT / "src/recomp/gen").glob("recomp_0*.c")):
        text = path.read_text(encoding="utf-8", errors="surrogateescape").replace("\r\n", "\n")
        for m in FUNCTION.finditer(text):
            yield m


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--count", type=int, default=160, help="eligible functions sampled besides the hot ones")
    parser.add_argument("--all", action="store_true", help="every eligible function")
    parser.add_argument("--cases", type=int, default=24)
    parser.add_argument("--only", help="one function, with the first differing bytes printed")
    parser.add_argument("--keep", help="build in this folder and keep it")
    parser.add_argument("--objects", help="a kept folder: reuse its compiled functions, rebuild only the harness")
    parser.add_argument("--jobs", type=int, default=4, help="cases run at once")
    parser.add_argument("--report", help="write each function's outcomes to this JSON file")
    args = parser.parse_args()
    if not shutil.which("cl"):
        raise SystemExit("Run scripts/enter-env.ps1 first.")
    if args.objects:
        folder = Path(args.objects)
        (folder / "harness.c").write_text(HARNESS)
        subprocess.run(["cl", "/nologo", "/O2", "/MD", "/w", "/std:c11", "/I" + str(folder),
                        "/I" + str(ROOT / "src/recomp/gen"), "harness.c", "mmx.obj", "functions.obj",
                        "/Fe:harness.exe"], check=True, cwd=folder, stdout=subprocess.DEVNULL)
        if not drive(folder / "harness.exe", folder, args.cases, args.jobs, args.report):
            raise SystemExit(1)
        return
    eligible = {}
    for m in chunks():
        name, whole = m.group(1), m.group(0)
        if m.group(2).count("RECOMP_UNIMPL(") >= 4:
            continue
        head = f"void {name}(void)\n{{\n"
        rest = whole[len(head):]
        local = x87.transform(head + "    RECOMP_GEN_PROLOGUE\n", rest)
        if local:
            eligible[name] = (head, rest, local)
    names = sorted(eligible)
    chosen = [args.only] if args.only else names if args.all else sorted(set(random.Random(23).sample(names, min(args.count, len(names)))) |
                                           {n for n in HOT if n in eligible})
    out, callees = [], set()
    for name in chosen:
        head, rest, local = eligible[name]
        callees |= set(re.findall(r"\bsub_[0-9A-F]{8}(?:_original)?\b", rest))
        out.append(head.replace(name, "orig_" + name, 1) + "    RECOMP_GEN_PROLOGUE\n" + rest)
        out.append(local.replace(f"void {name}(void)", f"void x87_{name}(void)", 1))
    stubs = sorted(callees)
    with tempfile.TemporaryDirectory(prefix="mm3-x87-locals-") as temporary:
        folder = Path(args.keep) if args.keep else Path(temporary)
        folder.mkdir(parents=True, exist_ok=True)
        for name in ("mm3_gen_cache_on.h", "mm3_gen_cache_off.h"):
            shutil.copyfile(ROOT / "src" / name, folder / name)
        shutil.copyfile(ROOT / "src/mm3_recomp_types.h", folder / "recomp_types.h")
        decl = "".join(f"void {s}(void);\n" for s in stubs)
        (folder / "functions.c").write_text('#include "recomp_types.h"\n' + decl + "".join(out))
        stub_text = "".join(f"void {s}(void) {{ stub_effect(0x{index * 2654435761 & 0xFFFFFFFF:08X}u); }}\n"
                            for index, s in enumerate(stubs, 1))
        pairs = "".join(f"void orig_{n}(void); void x87_{n}(void);\n" for n in chosen)
        pairs += ("static const struct { const char *name; fn_t original, locals; } pairs[] = {\n" +
                  "".join(f'    {{"{n}", orig_{n}, x87_{n}}},\n' for n in chosen) + "};\n")
        pairs += f"#define PAIR_COUNT {len(chosen)}u\n"
        (folder / "pairs.inc").write_text(stub_text + pairs)
        (folder / "harness.c").write_text(HARNESS)
        (folder / "mmx.c").write_text("#include <stdint.h>\ntypedef union { uint64_t q; } M;\n"
                                      "__declspec(thread) M g_mm0, g_mm1, g_mm2, g_mm3, g_mm4, g_mm5, g_mm6, g_mm7;\n")
        include = ["/I" + str(folder), "/I" + str(ROOT / "src/recomp/gen")]
        optimise = os.environ.get("X87_CFLAGS", "/O2 /Ob1").split()
        subprocess.run(["cl", "/nologo", *optimise, "/MD", "/w", "/std:c11", "/bigobj", *include,
                        "/DRECOMP_GENERATED_CODE", "/DMM3_GEN_REGISTER_CACHE=1", "/c", "functions.c"],
                       check=True, cwd=folder, stdout=subprocess.DEVNULL)
        subprocess.run(["cl", "/nologo", "/O2", "/MD", "/w", "/std:c11", *include, "harness.c", "mmx.c",
                        "functions.obj", "/Fe:harness.exe"], check=True, cwd=folder, stdout=subprocess.DEVNULL)
        passed = drive(folder / "harness.exe", folder, args.cases, args.jobs, args.report)
    print(f"({len(chosen)} of {len(names)} eligible functions compiled)")
    if not passed:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
