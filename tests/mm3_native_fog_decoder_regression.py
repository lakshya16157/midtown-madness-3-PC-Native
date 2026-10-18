"""Compile the exact production ordered fog decoder without GPU/game assets."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "tools/xboxrecomp"

CONTRACT = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "nv2a_d3d11.h"
#define NV097_NO_OPERATION 0x100u
static unsigned calls, ordinary;
static uint32_t last_method, last_subch, colour, flags, state_marker = 7;
static float captured[8];
static void d3d_fill_state(NvD3DState *state) {
    memset(state,0,sizeof *state); state->color_addr = state_marker;
}
void nv2a_d3d_distance_fog(const NvD3DState *state, float a, float b,
    float c, float d, uint32_t argb, uint32_t mode, const float sky[4]) {
    assert(state->color_addr == state_marker); ++calls;
    captured[0]=a; captured[1]=b; captured[2]=c; captured[3]=d;
    memcpy(captured+4,sky,4*sizeof(float)); colour=argb; flags=mode;
}
static void feed(uint32_t subch,uint32_t method,uint32_t param) {
DECODER
    ++ordinary; last_method=method; last_subch=subch;
}
static uint32_t word(float value) { uint32_t bits; memcpy(&bits,&value,4); return bits; }
static void packet(void) {
    const float f[8]={.0001f,1000,200,800,.6f,.8f,-.1f,6.25f};
    unsigned i;
    feed(0,0x100,NV2A_HOST_FOG_TAG);
    for(i=0;i<4;++i) feed(0,0x100,word(f[i]));
    feed(0,0x100,0xFF123456); feed(0,0x100,7);
    for(i=4;i<8;++i) feed(0,0x100,word(f[i]));
    assert(!memcmp(captured,f,sizeof f) && colour==0xFF123456 && flags==7);
}
int main(void) {
    unsigned n;
    feed(0,0x100,0); assert(!calls && !ordinary);
    feed(0,0x100,NV2A_HOST_FOG_TAG); feed(0,0x100,word(99));
    feed(0,0x2B4,1); assert(ordinary==1 && last_method==0x2B4 && !calls);
    for(n=0;n<10;++n) feed(0,0x100,word(1)); assert(!calls);
    packet(); assert(calls==1);
    /* A new tag replaces an incomplete extension instead of appending to it. */
    feed(0,0x100,NV2A_HOST_FOG_TAG); feed(0,0x100,word(99));
    state_marker=81; packet(); assert(calls==2);
    /* A different subchannel interrupts, but remains an ordinary command. */
    feed(0,0x100,NV2A_HOST_FOG_TAG); feed(3,0x100,word(1));
    assert(ordinary==2 && last_subch==3 && last_method==0x100);
    for(n=0;n<10;++n) feed(0,0x100,word(1)); assert(calls==2);
    /* Every ordinary command survives interruption, including actual clip regs. */
    feed(0,0x100,NV2A_HOST_FOG_TAG); feed(0,0x2FC,0x003F0000);
    assert(ordinary==3 && last_method==0x2FC);
    for(n=0;n<1024;++n) packet();
    assert(calls==1026 && ordinary==3);
    puts("PASS: production ordered fog packet payload/state, incomplete/restarted/interrupted packets, subchannels, ordinary window methods and repeated packet bounds");
    return 0;
}
'''

if __name__ == "__main__":
    if not shutil.which("cl"):
        raise SystemExit("Run scripts/enter-env.ps1 first.")
    source = (RUNTIME / "src/kernel/nv2a_pb_exec.c").read_text()
    decoder = re.search(r"^    static uint32_t fog_words\[.*?^    fog_word = 0;\n",
                        source, re.M | re.S)
    assert decoder, "Production fog decoder is missing"
    with tempfile.TemporaryDirectory(prefix="mm3-fog-decoder-") as temporary:
        folder = Path(temporary)
        code = folder / "contract.c"
        code.write_text(CONTRACT.replace("DECODER", decoder.group()))
        executable = folder / "contract.exe"
        subprocess.run(["cl", "/nologo", "/W4", "/WX", "/O2", "/std:c11",
                        "/D_CRT_SECURE_NO_WARNINGS", "/I" + str(RUNTIME / "src/video"),
                        str(code), "/Fo:" + str(folder / "contract.obj"),
                        "/Fe:" + str(executable)], check=True, cwd=folder)
        subprocess.run([str(executable)], check=True, cwd=folder, timeout=10)
