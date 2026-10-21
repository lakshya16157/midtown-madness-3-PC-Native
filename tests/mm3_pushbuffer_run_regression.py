"""Bulk pushbuffer runs must equal one production method call per word.

Compiles the production nv2a_pb_exec_run body against the production
ARRAY_ELEMENT16/32 and INLINE_ARRAY cases (with small buffers so the full
and nearly-full limits are reached) and the production TRANSFORM_PROGRAM /
TRANSFORM_CONSTANT range checks, then compares both paths on random runs.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "tools/xboxrecomp/src/kernel/nv2a_pb_exec.c"

HARNESS = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define NV_MAX_INDICES 64u
#define NV_MAX_INLINE 40u
#define NV097_ARRAY_ELEMENT16 0x1800
#define NV097_INLINE_ARRAY 0x1818
static struct { uint32_t prim, idx[NV_MAX_INDICES], idx_count, inline_buf[NV_MAX_INLINE], inline_count; } s_gpu;
static int s_ftrace;
static unsigned dispatched, logged;
static uint32_t log_words[256];
static void nv2a_vsh_program_word(uint32_t word) { log_words[logged++ & 255] = 0x10000000u ^ word; }
static void nv2a_vsh_constant_word(uint32_t word) { log_words[logged++ & 255] = 0x20000000u ^ word; }
void nv2a_pb_exec_method(uint32_t subch, uint32_t method, uint32_t param)
{
    (void)subch;
    dispatched++;
    switch (method) {
CASES
    default:
RANGES
        break;
    }
}
RUN
static uint32_t rng = 12345;
static uint32_t next(void) { rng = rng * 1664525u + 1013904223u; return rng; }

static int index_runs(void)
{
    static const uint32_t methods[3] = { 0x1800, 0x1808, 0x1818 };
    uint32_t words[80], trial, i;
    for (trial = 0; trial < 200000; ++trial) {
        uint32_t method = methods[next() % 3], n = next() % 80;
        uint32_t prim = next() % 4 ? 5 : 0, idx = next() % (NV_MAX_INDICES + 1);
        uint32_t inl = next() % (NV_MAX_INLINE + 1);
        uint32_t per_idx[NV_MAX_INDICES], per_inl[NV_MAX_INLINE], per_ic, per_nc;
        for (i = 0; i < n; ++i) words[i] = next();
        memset(&s_gpu, 0xA5, sizeof s_gpu);
        s_gpu.prim = prim; s_gpu.idx_count = idx; s_gpu.inline_count = inl;
        for (i = 0; i < n; ++i) nv2a_pb_exec_method(0, method, words[i]);
        memcpy(per_idx, s_gpu.idx, sizeof per_idx); memcpy(per_inl, s_gpu.inline_buf, sizeof per_inl);
        per_ic = s_gpu.idx_count; per_nc = s_gpu.inline_count;
        memset(&s_gpu, 0xA5, sizeof s_gpu);
        s_gpu.prim = prim; s_gpu.idx_count = idx; s_gpu.inline_count = inl;
        dispatched = 0;
        nv2a_pb_exec_run(method, words, n);
        if (s_gpu.idx_count != per_ic || s_gpu.inline_count != per_nc ||
            memcmp(per_idx, s_gpu.idx, sizeof per_idx) || memcmp(per_inl, s_gpu.inline_buf, sizeof per_inl) ||
            dispatched != (n ? 1u : 0u)) {
            fprintf(stderr, "FAIL index trial %u method %X n %u prim %u idx %u inline %u\n",
                    trial, method, n, prim, idx, inl);
            return 1;
        }
    }
    return 0;
}

/* pb_walk keeps an increasing run inside one range, as here. */
static int upload_runs(void)
{
    uint32_t words[32], trial, i, per[32];
    for (trial = 0; trial < 100000; ++trial) {
        uint32_t base = next() & 1 ? 0x0B00u : 0x0B80u;
        uint32_t method = base + 4 * (next() % 32), n = next() % ((base + 0x80 - method) / 4 + 1);
        unsigned per_logged, per_dispatched;
        s_ftrace = next() % 3 == 0 ? 2 : 0;
        for (i = 0; i < n; ++i) words[i] = next();
        logged = dispatched = 0;
        for (i = 0; i < n; ++i) nv2a_pb_exec_method(0, method + 4 * i, words[i]);
        memcpy(per, log_words, sizeof per); per_logged = logged; per_dispatched = dispatched;
        logged = dispatched = 0;
        nv2a_pb_exec_run(method, words, n);
        if (logged != per_logged || memcmp(per, log_words, logged * 4) ||
            dispatched != (s_ftrace == 2 ? per_dispatched : n ? 1u : 0u)) {
            fprintf(stderr, "FAIL upload trial %u method %X n %u ftrace %d\n", trial, method, n, s_ftrace);
            return 1;
        }
    }
    s_ftrace = 0;
    return 0;
}

int main(void)
{
    if (index_runs() || upload_runs()) return 1;
    puts("PASS: 200000 index/inline and 100000 program/constant runs equal per-word execution, including full buffers and frame tracing");
    return 0;
}
'''


def case_block(source, label):
    start = source.index(label, source.index("void nv2a_pb_exec_method("))
    end = source.index("break;", start) + len("break;")
    return source[start:end]


def range_block(source, condition):
    start = source.index(condition, source.index("void nv2a_pb_exec_method("))
    end = source.index("}", source.index("break;", start)) + 1
    return source[start:end]


def main():
    if not shutil.which("cl"):
        raise SystemExit("Run scripts/enter-env.ps1 first so cl.exe is available.")
    source = SOURCE.read_text()
    cases = "\n".join(case_block(source, label) for label in (
        "case NV097_INLINE_ARRAY:", "case NV097_ARRAY_ELEMENT16:", "case 0x1808:"))
    ranges = "\n".join(range_block(source, condition) for condition in (
        "if (method >= 0x0B00 && method < 0x0B80) {", "if (method >= 0x0B80 && method < 0x0C00) {"))
    run = re.search(r"^void nv2a_pb_exec_run\(.*?^\}", source, re.M | re.S)
    if run is None:
        raise ValueError("Missing production nv2a_pb_exec_run")
    program = HARNESS.replace("CASES", cases).replace("RANGES", ranges).replace("RUN", run.group())
    with tempfile.TemporaryDirectory(prefix="mm3-pb-run-") as folder:
        folder = Path(folder)
        (folder / "run.c").write_text(program)
        subprocess.run(["cl", "/nologo", "/W4", "/WX", "/O2", "/std:c11", "run.c", "/Fe:run.exe"],
                       check=True, cwd=folder, stdout=subprocess.DEVNULL)
        subprocess.run([str(folder / "run.exe")], check=True)


if __name__ == "__main__":
    main()
