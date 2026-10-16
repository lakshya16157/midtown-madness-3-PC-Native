"""Exercise the actual pushbuffer texture decoder and D3D state exporter.

Run after scripts/enter-env.ps1. No game process or assets are modified.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "tools/xboxrecomp/src"


def function(source, name):
    match = re.search(r"^static [^\n]+\b" + name + r"\([^\n]*\)\n\{.*?^\}",
                      source, re.M | re.S)
    if not match:
        raise ValueError(name)
    return match.group()


CONTRACT = r'''
static void set_format(unsigned stage, uint32_t format) {
    tex_stage_method(NV_TEX_FIRST + stage * 0x40, 0x00100000u + stage * 0x10000);
    tex_stage_method(NV097_SET_TEXTURE_FORMAT + stage * 0x40, format);
}

int main(void) {
    NvD3DState state;
    unsigned stage;
    for (stage = 0; stage < 4; ++stage) {
        /* 32x32x32 DXT5, four mips: the native projected light fields. */
        set_format(stage, 0x55540F32u);
        d3d_fill_state(&state);
        assert(state.tex[stage].width == 32);
        assert(state.tex[stage].height == 32);
        assert(state.tex[stage].depth == 32);
        assert(state.tex[stage].levels == 4);
        assert(state.tex[stage].color == 0x0F);
        assert(!state.tex[stage].cube);
        assert(state.tex[stage].format == 0x55540F32u);

        /* Non-cubic dimensions test the independent U/V/P bit fields. */
        set_format(stage, 0x23410632u);
        d3d_fill_state(&state);
        assert(state.tex[stage].width == 16);
        assert(state.tex[stage].height == 8);
        assert(state.tex[stage].depth == 4);

        /* A one-slice volume retains its raw dimension even at depth one. */
        set_format(stage, 0x03410632u);
        d3d_fill_state(&state);
        assert(state.tex[stage].depth == 1);
        assert(((state.tex[stage].format >> 4) & 15) == 3);

        /* Reusing a stage for 2D resets depth despite stale P bits. */
        set_format(stage, 0xF5510622u);
        d3d_fill_state(&state);
        assert(state.tex[stage].width == 32);
        assert(state.tex[stage].height == 32);
        assert(state.tex[stage].depth == 1);
        assert(!state.tex[stage].cube);

        /* Cube faces are never interpreted as a volume. */
        set_format(stage, 0xF5510626u);
        d3d_fill_state(&state);
        assert(state.tex[stage].depth == 1);
        assert(state.tex[stage].cube);

        /* Linear 2D dimensions still come from IMAGE_RECT and pitch. */
        set_format(stage, 0xF0011222u);
        tex_stage_method(NV097_SET_TEXTURE_CONTROL1 + stage * 0x40, 320u << 16);
        tex_stage_method(NV097_SET_TEXTURE_IMAGE_RECT + stage * 0x40, (80u << 16) | 60u);
        d3d_fill_state(&state);
        assert(state.tex[stage].width == 80);
        assert(state.tex[stage].height == 60);
        assert(state.tex[stage].depth == 1);
        assert(state.tex[stage].pitch == 320);

        /* Legacy zero-initialized 2D callers export one slice. */
        s_gpu.texs[stage].depth = 0;
        d3d_fill_state(&state);
        assert(state.tex[stage].depth == 1);
    }
    puts("PASS: native light volume dimensions reach all four backend stages; non-cubic/one-slice volumes, 2D reuse, cube faces, linear sizes and zero-depth fallback preserved");
    return 0;
}
'''


def main():
    if not shutil.which("cl"):
        raise SystemExit("Run after scripts/enter-env.ps1 so cl is available")
    original = (RUNTIME / "kernel/nv2a_pb_exec.c").read_text()
    start = original.index("typedef struct {\n    uint32_t offset;")
    end = original.index("\n/* Unhandled methods", start)
    # Retain the production structures while shrinking unrelated draw buffers.
    definitions = original[start:end]
    definitions = re.sub(r"#define NV_MAX_INDICES[^\n]*", "#define NV_MAX_INDICES 1", definitions)
    definitions = re.sub(r"#define NV_MAX_INLINE[^\n]*", "#define NV_MAX_INLINE 1", definitions)
    macros = "\n".join(re.findall(r"^#define NV097_SET_TEXTURE_[^\n]+", original, re.M))
    source = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "video/nv2a_d3d11.h"
#include "d3d/d3d8_swizzle.h"
#define NV_TEX_FIRST 0x1B00
#define NV_TEX_LAST 0x1BFC
static uint32_t s_tex_reg[(NV_TEX_LAST - NV_TEX_FIRST) / 4 + 1];
static uint8_t s_tex_set[(NV_TEX_LAST - NV_TEX_FIRST) / 4 + 1];
static uint32_t dma_resolve(uint32_t offset) { return offset; }
'''
    source += macros + "\n" + definitions + "\n"
    source += "\n".join(function(original, name) for name in
                        ("tex_size_from_format", "tex_update_valid", "record_tex_reg",
                         "tex_stage_method", "d3d_fill_state"))
    source += CONTRACT
    with tempfile.TemporaryDirectory(prefix="mm3-volume-parser-") as temporary:
        folder = Path(temporary)
        test = folder / "contract.c"
        test.write_text(source)
        executable = folder / "contract.exe"
        subprocess.run(["cl", "/nologo", "/W4", "/WX", "/Od", "/std:c11",
                        "/I" + str(RUNTIME), "/Fo:" + str(folder / "contract.obj"),
                        "/Fe:" + str(executable), str(test)], check=True, cwd=folder)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
