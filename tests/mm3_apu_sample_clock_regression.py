"""Run MM3's XACT engine clock (sub_00357B67) on the APU sample counter.

The title reads XGSCNT as 48 kHz samples. The APU core now counts the
samples its voice processor produced (32 per frame); xemu's 10 MHz host clock
made every XACT time run 208 times fast. The contract executes the native
clock and its 64-bit helpers with the guest ABI against both counters."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / "tools/xboxrecomp/src/apu/apu_core.c"
LOWER = re.compile(r"    \{ uint32_t _icall_esp = g_esp;\n    \{ uint32_t _icall_target = MEM32\(0x361FF4\);"
                   r" PUSH32\(esp, 0x00357C0Bu\); RECOMP_ICALL_SAFE_AT\(_icall_target, _icall_esp, 0x00357C05u\); \}"
                   r" /\* indirect call \*/\n    \}\n")


def native_owner(name):
    owners = []
    for path in (ROOT / "src/recomp/gen").glob("recomp_*.c"):
        match = re.search(r"^void " + name + r"\(void\)\n\{.*?^\}", path.read_text(), re.M | re.S)
        if match:
            owners.append(match.group())
    assert len(owners) == 1, (name, len(owners))
    return owners[0]


def main():
    if not shutil.which("cl"):
        raise SystemExit("Run scripts/enter-env.ps1 first.")
    core = CORE.read_text()
    se_frame = core[core.index("static void se_frame(MCPXAPUState *d)"):]
    se_frame = se_frame[:se_frame.index("\n}\n")]
    # The voice processor's frame advances the counter by its 32 samples; the
    # register read returns it and the guest's reset write sets it.
    assert "mcpx_apu_vp_frame(d, mixbins);\n    /* The voices moved on by one frame; so does the sample counter. */\n" \
           "    qatomic_fetch_add(&d->gscnt, NUM_SAMPLES_PER_FRAME);" in se_frame
    assert "            r = qatomic_read(&d->gscnt);" in core
    assert "    case NV_PAPU_XGSCNT:\n        /* DirectSound clears the counter when it starts the APU. */\n" \
           "        qatomic_set(&d->gscnt, (uint32_t)val);" in core
    clock = native_owner("sub_00357B67")
    assert clock.count("eax = MEM32(0xFE80200Cu);") == 1
    clock = clock.replace("eax = MEM32(0xFE80200Cu);", "eax = mm3_apu_mmio_read32(0xFE80200Cu);")
    clock, lowered = LOWER.subn("    PUSH32(esp, 0x00357C0Bu); lower_irql();\n", clock)
    assert lowered == 1
    native = "\n".join((native_owner("sub_00094D70"), native_owner("sub_00095100"), clock)) + "\n"
    with tempfile.TemporaryDirectory(prefix="mm3-apu-clock-") as temporary:
        folder = Path(temporary)
        (folder / "native_clock.inc").write_text(native)
        shutil.copyfile(ROOT / "src/mm3_recomp_types.h", folder / "recomp_types.h")
        executable = folder / "contract.exe"
        subprocess.run(["cl", "/nologo", "/W3", "/WX", "/wd4102", "/Od", "/std:c11",
                        "/I" + str(folder), "/I" + str(ROOT / "tools/xboxrecomp/include"),
                        str(ROOT / "tests/mm3_apu_sample_clock_contract.c"),
                        "/Fo:" + str(folder / "contract.obj"), "/Fe:" + str(executable)],
                       check=True, cwd=folder)
        subprocess.run([str(executable)], check=True, cwd=folder)
    print("PASS APU sample clock: produced 48 kHz samples give XACT real time at any call cadence, "
          "wrap handled; the former 10 MHz clock ran 208x fast")


if __name__ == "__main__":
    main()
