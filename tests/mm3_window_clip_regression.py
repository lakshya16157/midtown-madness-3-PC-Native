"""Exercise production window-method decoding and export for all eight regions."""
from pathlib import Path
import re
import subprocess
import tempfile
import mm3_volume_texture_regression as texture

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "tools/xboxrecomp/src"


def main():
    original = (RUNTIME / "kernel/nv2a_pb_exec.c").read_text()
    start = original.index("typedef struct {\n    uint32_t offset;")
    end = original.index("\n/* Unhandled methods", start)
    definitions = original[start:end]
    definitions = re.sub(r"#define NV_MAX_INDICES[^\n]*", "#define NV_MAX_INDICES 1", definitions)
    definitions = re.sub(r"#define NV_MAX_INLINE[^\n]*", "#define NV_MAX_INLINE 1", definitions)
    decoder_start = original.index("    case 0x02B4:", original.index("void nv2a_pb_exec_method("))
    decoder_end = original.index("    case NV097_SET_SURFACE_FORMAT:", decoder_start)
    # Compile the exact switch arms from the production executor, not a second
    # decoder. The large unrelated GPU-method handlers are outside this fixture.
    decoder = original[decoder_start:decoder_end]
    source = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "video/nv2a_d3d11.h"
#include "d3d/d3d8_swizzle.h"
static uint32_t dma_resolve(uint32_t offset) { return offset; }
'''
    source += definitions + "\n" + texture.function(original, "d3d_fill_state")
    source += "\nstatic void method(uint32_t method, uint32_t param) { switch (method) {\n"
    source += decoder + '\n default: assert(!"unexpected window method"); } }\n'
    source += r'''
int main(void)
{
    NvD3DState state;
    unsigned i, mask = 0;
    memset(&s_gpu, 0, sizeof s_gpu);
    d3d_fill_state(&state);
    assert(!state.wclip_mask && !state.wclip_type);
    for (i = 0; i < 8; ++i) {
        uint32_t h = ((i * 31u + 19u) << 16) | (i * 31u + 2u);
        uint32_t v = ((i * 23u + 17u) << 16) | (i * 23u + 3u);
        method(0x02C0u + 4u * i, h);
        mask |= 1u << i;
        d3d_fill_state(&state);
        assert(state.wclip_mask == mask && state.wclip_h[i] == h);
        assert(!state.wclip_v[i]);
        method(0x02E0u + 4u * i, v);
        d3d_fill_state(&state);
        assert(state.wclip_mask == mask && state.wclip_v[i] == v);
        assert(state.wclip_h[i] == h);
    }
    for (i = 0; i < 8; ++i) {
        assert(state.wclip_h[i] == ((i * 31u + 19u) << 16 | (i * 31u + 2u)));
        assert(state.wclip_v[i] == ((i * 23u + 17u) << 16 | (i * 23u + 3u)));
    }
    method(0x02B4, 1); d3d_fill_state(&state);
    assert(state.wclip_type == 1 && state.wclip_mask == 255);
    method(0x02DC, 0x0FFF0000u); method(0x02FC, 0x03FF0200u);
    method(0x02B4, 0); d3d_fill_state(&state);
    assert(!state.wclip_type && state.wclip_mask == 255);
    assert(state.wclip_h[7] == 0x0FFF0000u && state.wclip_v[7] == 0x03FF0200u);
    assert(state.wclip_h[0] == 0x00130002u && state.wclip_v[0] == 0x00110003u);
    puts("PASS all eight inclusive/exclusive window clip method indices, written masks, partial updates and production state export");
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix="mm3-window-method-") as temporary:
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
