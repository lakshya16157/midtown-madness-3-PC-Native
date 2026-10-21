"""CPU-only production stress adapter contract; never starts the game."""
from pathlib import Path
import json
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    # Assert the inspected retail owner layout before trusting diagnostic reads.
    source=(ROOT / "src/recomp/gen/recomp_0014.c").read_text()
    owner=lambda name: re.search(r"void "+name+r"\(void\)\n\{.*?\n\}",source,re.S).group()
    factory=owner("sub_0017BB4B");constructor=owner("sub_0017BC0C");release=owner("sub_0017BAFD")
    assert "eax = MEM32(ebx + 4);" in factory and "edi = ebx + 0xC;" in factory
    assert "PUSH32(esp, 0x40);" in constructor and "POP32(esp, esi);" in constructor
    assert "eax = esi + 0xC;" in release and "MEM32(edi + 4) = MEM32(edi + 4) - 1;" in release
    # std::list insert/erase own count+8; deque count is+10 relative to+0C.
    all_sources="".join(path.read_text() for path in (ROOT / "src/recomp/gen").glob("recomp_*.c"))
    def body(name): return re.search(r"void "+name+r"\(void\)\n\{.*?\n\}",all_sources,re.S).group()
    assert "RECOMP_ABI_CALL(0x0001AF52u, sub_0001AF52)" in body("sub_0003F85B")
    assert "eax = MEM32(ecx + 8);" in body("sub_0001AF52") and "MEM32(ecx + 8) = eax;" in body("sub_0001AF52")
    assert "MEM32(esi + 0x10) = MEM32(esi + 0x10) + 1;" in body("sub_00079645")
    if not shutil.which("cl"):
        raise SystemExit("Run scripts/enter-env.ps1 first.")
    with tempfile.TemporaryDirectory(prefix="mm3-stress-contract-") as temporary:
        folder = Path(temporary)
        shutil.copyfile(ROOT / "src/mm3_recomp_types.h", folder / "recomp_types.h")
        executable = folder / "contract.exe"
        subprocess.run([
            "cl", "/nologo", "/W3", "/Od", "/std:c11", "/I" + str(folder),
            "/I" + str(ROOT / "tools/xboxrecomp/include"),
            str(ROOT / "tests/mm3_stress_contract.c"),
            "/Fo:" + str(folder / "contract.obj"), "/Fe:" + str(executable),
        ], check=True, cwd=folder)
        result = subprocess.run([str(executable)], check=True, text=True, capture_output=True)
        lines = result.stdout.splitlines()
        first, maximum = (json.loads(line) for line in lines[:2])
        pool=first["audio_pool"]
        assert pool["available"] and pool["active_wrappers"]==64 and pool["free_resources"]==0
        assert pool["zero_free_samples"]==1 and pool["factory_empty_rejections"]==0
        assert pool["counter_scope"]=="process_since_first_stress_tick"
        police = first["police"]
        assert police["active_count"] == 2 and police["body_count"] == 2
        assert police["nearby_count"] == 1 and len(set(police["nearby_ids"])) == 1
        actors = {actor["id"]: actor for actor in police["actors"]}
        nearby = actors[police["nearby_ids"][0]]
        assert nearby["mode"] == 2 and nearby["body"] and abs(nearby["distance"] - (205 ** 0.5)) < 0.0001
        assert (nearby["x"], nearby["y"], nearby["z"]) == (3, 10, -14)
        assert police["placement_attempted"] == 2 and police["placement_accepted"] == police["placement_verified"] == 1
        assert len(maximum["police"]["actors"]) == 32 and maximum["police"]["nearby_count"] == 0
        print(lines[-1])
        print(f"PASS: JSON proof fields and maximum32-actor snapshot ({len(lines[1])} bytes <8192)")


if __name__ == "__main__":
    main()
