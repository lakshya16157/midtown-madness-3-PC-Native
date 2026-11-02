"""Run title overlay placement and route upload contracts without a GPU."""
from pathlib import Path
import os
import subprocess
import tempfile

import cc

ROOT = Path(__file__).resolve().parents[1]

D3D11_HOOKS = r'''
#include <assert.h>
#include <stdio.h>
#include "widescreen_hud.c"
static int edge = -1, starts;
void nv2a_d3d_set_edge_hud(int on) { edge = on; }
void nv2a_d3d_set_hud_start(float a, float b, float c, float d) { starts += a == 50 && b == 320 && c == 178 && d == 448; }
int mm3_render_force_depth(const NvD3DState *s, uint32_t w, uint32_t h) { (void)s; (void)w; (void)h; return 0; }
int mm3_render_cubic_filter(const NvD3DState *s, int t, uint32_t l) { (void)s; (void)t; (void)l; return 0; }
void mm3_render_vertex_alignment(uint32_t w, uint32_t h, float *snap, float *offset) { (void)w; (void)h; (void)snap; (void)offset; }
int main(void) {
    assert(!mm3_hud_hooks.place && !mm3_hud_hooks.frame && !mm3_hud_hooks.hold_aspect);
    assert(!mm3_hud_hooks.sanitize_vertex_upload);
    assert(mm3_hud_hooks.force_depth_test == mm3_render_force_depth);
    assert(mm3_hud_hooks.cubic_filter_0 == mm3_render_cubic_filter);
    assert(mm3_hud_hooks.vertex_alignment == mm3_render_vertex_alignment);
    mm3_hud_set_edge(1); assert(edge == 1);
    mm3_hud_set_edge(0); assert(edge == 0);
    mm3_hud_set_start(50, 320, 178, 448); assert(starts == 1);
    puts("PASS overlay: D3D11 keeps the renderer's HUD placement, forwards the latch");
    return 0;
}
'''


def main():
    cc.require_compiler()
    with tempfile.TemporaryDirectory(prefix="mm3-overlay-") as temporary:
        folder = Path(temporary)
        executable = folder / f"contract{cc.exe_suffix()}"
        # POSIX has no Interlocked* in libc: the runtime compat layer provides them.
        sources = [ROOT / "tests/mm3_overlay_contract.c", ROOT / "src/geometry/piece_cluster.c"]
        if os.name != "nt":
            sources.append(ROOT / "tools/xboxrecomp/src/platform/win32_compat.c")
        subprocess.run(cc.compile_command(
            sources, executable,
            includes=[ROOT / "src/mods", ROOT / "tools/xboxrecomp/src/video", ROOT / "tools/xboxrecomp/src", ROOT / "tools/xboxrecomp/src/kernel"],
            # The SDL3 GPU renderer is the one that takes the title's placement hook.
            defines=["_CRT_SECURE_NO_WARNINGS", "XBOXRECOMP_SDL3=1"], std="c11",
            optimize="speed", warnings="normal", werror=True),
            check=True, cwd=folder)
        subprocess.run([str(executable)], check=True, cwd=folder)
        # The D3D11 renderer has its own placement: a place hook would replace it,
        # so only the HUD latch is forwarded and the draw-policy hooks installed.
        (folder / "d3d11.c").write_text(D3D11_HOOKS, encoding="utf-8")
        d3d11 = folder / f"d3d11{cc.exe_suffix()}"
        subprocess.run(cc.compile_command(
            [folder / "d3d11.c"], d3d11,
            includes=[ROOT / "src/mods", ROOT / "tools/xboxrecomp/src/video", ROOT / "tools/xboxrecomp/src", ROOT / "tools/xboxrecomp/src/kernel"],
            defines=["_CRT_SECURE_NO_WARNINGS"], std="c11",
            optimize="speed", warnings="normal", werror=True),
            check=True, cwd=folder)
        subprocess.run([str(d3d11)], check=True, cwd=folder)


if __name__ == "__main__":
    main()
