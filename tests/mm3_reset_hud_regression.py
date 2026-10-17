"""Exercise production HUD placement through the Backspace car-reset fade.

Run after scripts/enter-env.ps1. An optional --source path permits checking a
clean renderer reconstructed from the committed runtime patch as well.
"""
import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
RENDERER = ROOT / "tools/xboxrecomp/src/video/nv2a_d3d11.c"


def function(source, name):
    """Extract the real definition, avoiding calls and forward declarations."""
    match = re.search(r"^(?:static )?(?:int|void) " + re.escape(name)
                      + r"\([^;]*?\)\n\{", source, re.M)
    if not match:
        raise AssertionError("Missing renderer function: " + name)
    opening = source.index("{", match.start())
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


def harness(source, include):
    state_start = source.index("static volatile LONG s_edge_hud;")
    state_end = source.index("/* nv2a_d3d_set_depth_view_remap:", state_start)
    draw = function(source, "nv2a_d3d_draw_ub")
    start = draw.find("    /* The authored frontend (including turntable geometry)")
    if start < 0:
        start = draw.index("    if (pos && s &&")
    end = draw.find("\n    if (suppress_map_base)", start)
    if end < 0:
        end = draw.index("\n    /* Vertex data", start)
    placement = draw[start:end]
    flip = function(source, "nv2a_d3d_flip")
    assert "s_frame_bg_tex = 0;" in flip and "s_frame_tag_window = 1;" in flip
    assert "((px * ub_map.x - 1.0) * ub_map.w + ub_place.x) * w" in source
    helpers = ""
    for name in ("map_route_vertex", "world_name_vertex", "map_camera_window", "map_mask_window", "hud_split"):
        if re.search(r"^static int " + name + r"\(", source, re.M):
            helpers += function(source, name) + "\n"
    return r"""
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
""" + include + r"""
typedef long LONG;
static LONG InterlockedExchange(volatile LONG *p, LONG v) {
    LONG old = *p; *p = v; return old;
}
static LONG InterlockedCompareExchange(volatile LONG *p, LONG v, LONG expected) {
    LONG old = *p; if (old == expected) *p = v; return old;
}
enum { NV_D3D_TRIANGLES, NV_D3D_LINES, NV_D3D_POINTS };
typedef struct {
    uint32_t enabled, type, size, stride;
    const uint8_t *data;
} NvD3DAttrib;
#define HUD_GAP 24.0f
#define GUEST_W 640u
#define GUEST_H 480u
""" + source[state_start:state_end] + "\n" + function(source, "nv2a_d3d_hud_active") + "\n" + helpers + r"""
static int s_draw_textured, s_draw_reads_wide;
static int s_draw_minimap_improved, s_draw_map_base, s_draw_map_masked;
static uint32_t s_draw_tex_addr;
typedef struct { float sx, sy; } Surface;
typedef struct { float x[16], y[16]; } Result;
static const char *scenario;
static float checked_aspect;
static int checks;

static void near(float actual, float expected) {
    if (!isfinite(actual) || fabsf(actual - expected) > .0003f) {
        fprintf(stderr, "%s at aspect %.6f: got %.6f, expected %.6f\n",
                scenario, checked_aspect, actual, expected);
        exit(2);
    }
    ++checks;
}
static void require(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "%s at aspect %.6f: %s\n", scenario, checked_aspect, message);
        exit(2);
    }
    ++checks;
}
static Result draw(const float (*pos)[4], uint32_t count, const uint32_t *idx,
                   uint32_t ni, uint32_t lo, float aspect, float scale,
                   int textured, int reads_wide, uint32_t texture) {
    Surface surface = {scale * aspect / (4.0f / 3.0f), scale}, *s = &surface;
    uint32_t gw = GUEST_W, gh = GUEST_H, i;
    int topology = NV_D3D_TRIANGLES, use_dx = 0, use_dy = 0;
    int suppress_map_base = 0;
    float squeeze = 1, shift = 0, yscale = 1;
    static float *dx;
    static uint32_t dx_cap;
    float *dy = NULL;
    NvD3DAttrib attr[16] = {{0}};
    Result result = {0};
    s_draw_textured = textured; s_draw_reads_wide = reads_wide;
    s_draw_tex_addr = texture;
    s_draw_minimap_improved = s_draw_map_base = s_draw_map_masked = 0;
""" + placement + r"""
    require(!suppress_map_base, "reset fade incorrectly treated as a minimap");
    require(count <= 16, "test output capacity exceeded");
    for (i = 0; i < count; ++i) {
        float px = pos[i][0] + (use_dx ? dx[i] : 0);
        result.x[i] = ((px * (2.0f / gw) - 1.0f) * squeeze + shift + 1.0f) * gw * .5f;
        result.y[i] = gh * .5f + (pos[i][1] + (use_dy ? dy[i] : 0) - gh * .5f) * yscale;
    }
    return result;
}
static void quad(float p[4][4], float left, float right, float top, float bottom) {
    float q[4][4] = {{left,top,0,1}, {right,top,0,1},
                     {right,bottom,0,1}, {left,bottom,0,1}};
    memcpy(p, q, sizeof q);
}
static Result box(float left, float right, float top, float bottom,
                  float aspect, float scale, uint32_t base, uint32_t texture) {
    float p[4][4];
    uint32_t idx[6] = {base,base+1,base+2,base,base+2,base+3};
    quad(p,left,right,top,bottom);
    return draw(p,4,idx,6,base,aspect,scale,1,0,texture);
}
static void frame(void) {
    /* These are the frame-local resets checked against nv2a_d3d_flip above. */
    s_frame_bg_tex = 0;
    s_frame_tag_window = 1;
}
static void race(void) {
    nv2a_d3d_set_edge_hud(0);
    nv2a_d3d_set_edge_hud(1);
    nv2a_d3d_set_hud_start(17,380,137,470);
    /* Already learned while driving; the fade shares the gauge's atlas.
     * Keep the tested atlas away from the first entry to check lookup too. */
    s_hud_tex[0] = 500; s_hud_tex[1] = 501; s_hud_tex[2] = 502; s_hud_tex_n = 3;
    frame();
}
static void same(Result actual, int first, Result expected) {
    int i;
    for (i = 0; i < 4; ++i) {
        near(actual.x[first+i],expected.x[i]);
        near(actual.y[first+i],expected.y[i]);
    }
}
static void full_screen(Result fade, int first) {
    near(fade.x[first],0); near(fade.x[first+1],640);
    near(fade.x[first+2],640); near(fade.x[first+3],0);
    near(fade.y[first],0); near(fade.y[first+2],480);
}
static void stable_atlas(void) {
    require(!s_frame_bg_tex, "HUD-atlas reset fade poisoned the frame as a menu background");
    require(nv2a_d3d_hud_active(), "reset fade disabled race HUD placement");
    require(s_hud_tex_n == 3 && s_hud_tex[0] == 500 && s_hud_tex[1] == 501 && s_hud_tex[2] == 502,
            "reset fade changed learned HUD atlas ownership");
}
static void reset_sequence(float aspect, float scale, uint32_t base) {
    Result gauge, needle, damage, fade, actual;
    int pass, order;
    float p[12][4];
    uint32_t idx[18];
    checked_aspect = aspect;
    scenario = "normal driving";
    race();
    gauge = box(500,628,330,470,aspect,scale,base,501);
    needle = box(544,582,362,406,aspect,scale,base,501);
    damage = box(526,608,438,449,aspect,scale,base,501);
    /* Independent expected anchoring prevents a wrong baseline from hiding
     * a regression shared by the before/after draws. */
    near(gauge.x[1],640-(640-628)*fminf(1,(4.0f/3.0f)/aspect));
    near(gauge.y[2],480-(480-470)*fminf(1,aspect/(4.0f/3.0f)));
    for (pass = 0; pass < 5; ++pass) {
        scenario = "separate reset fade, needle and damage bar";
        frame();
        fade = box(0,640,0,480,aspect,scale,base,501);
        stable_atlas(); full_screen(fade,0);
        actual = box(544,582,362,406,aspect,scale,base,501); same(actual,0,needle);
        actual = box(526,608,438,449,aspect,scale,base,501); same(actual,0,damage);
        actual = box(500,628,330,470,aspect,scale,base,501); same(actual,0,gauge);
        for (order = 0; order < 3; ++order) {
            uint32_t i;
            int f = order, n = (order+1)%3, d = (order+2)%3;
            scenario = "mixed reset fade, needle and damage bar batch";
            frame();
            quad(p+4*f,0,640,0,480);
            quad(p+4*n,544,582,362,406);
            quad(p+4*d,526,608,438,449);
            for (i = 0; i < 3; ++i) {
                uint32_t v = base + 4*i, at = 6*i;
                idx[at]=v; idx[at+1]=v+1; idx[at+2]=v+2;
                idx[at+3]=v; idx[at+4]=v+2; idx[at+5]=v+3;
            }
            actual = draw(p,12,idx,18,base,aspect,scale,1,0,501);
            stable_atlas(); full_screen(actual,4*f);
            same(actual,4*n,needle); same(actual,4*d,damage);
        }
        scenario = "driving again after reset";
        frame();
        actual = box(544,582,362,406,aspect,scale,base,501); same(actual,0,needle);
        actual = box(526,608,438,449,aspect,scale,base,501); same(actual,0,damage);
        stable_atlas();
    }
    scenario = "genuine menu background during a race";
    frame();
    (void)box(0,640,0,480,aspect,scale,base,901);
    if (fabsf(aspect/(4.0f/3.0f)-1) > .001f) {
        require(s_frame_bg_tex == 901, "real menu artwork lost background classification");
        require(!nv2a_d3d_hud_active(), "real menu retained race HUD edge placement");
        (void)box(526,608,438,449,aspect,scale,base,501);
        require(s_frame_bg_tex == 901, "later HUD draw cleared the menu background");
    }
    scenario = "next race frame after menu background";
    frame();
    actual = box(526,608,438,449,aspect,scale,base,501); same(actual,0,damage);
    stable_atlas();
    scenario = "widened gameplay surface copy";
    frame();
    quad(p,0,640,0,480);
    idx[0]=base;idx[1]=base+1;idx[2]=base+2;idx[3]=base;idx[4]=base+2;idx[5]=base+3;
    actual = draw(p,4,idx,6,base,aspect,scale,1,1,901);
    full_screen(actual,0); stable_atlas();
}
int main(void) {
    const float aspects[] = {4.0f/3.0f,16.0f/10.0f,16.0f/9.0f,21.0f/9.0f,1,.75f};
    const float scales[] = {.5f,1,2};
    int a, s, b;
    for (a = 0; a < 6; ++a)
        for (s = 0; s < 3; ++s)
            for (b = 0; b < 2; ++b)
                reset_sequence(aspects[a],scales[s],b ? 37u : 0u);
    printf("PASS Backspace reset HUD: %d checks; separate/mixed fade batches, atlas/menu ownership, "
           "6 aspects, 3 scales, nonzero index bases and repeated frames\n",checks);
    return 0;
}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=RENDERER)
    args = parser.parse_args()
    if not shutil.which("cl"):
        raise SystemExit("Run scripts/enter-env.ps1 first.")
    source = args.source.read_text(encoding="utf-8")
    header = args.source.parent / "nv2a_minimap_projection.h"
    if not header.is_file():
        header = RENDERER.parent / header.name
    include = '#include "' + header.as_posix() + '"\n' if "nv2a_minimap_" in source else ""
    with tempfile.TemporaryDirectory(prefix="mm3-reset-hud-") as temporary:
        folder = Path(temporary)
        contract = folder / "reset.c"
        contract.write_text(harness(source, include), encoding="utf-8")
        executable = folder / "reset.exe"
        subprocess.run(["cl", "/nologo", "/W3", "/std:c11", str(contract),
                        "/Fo:" + str(folder / "reset.obj"), "/Fe:" + str(executable)],
                       check=True, cwd=folder)
        subprocess.run([str(executable)], check=True, cwd=folder)


if __name__ == "__main__":
    main()
