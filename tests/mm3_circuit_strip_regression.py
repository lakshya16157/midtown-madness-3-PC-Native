"""Circuit pictures stay out of the Checkpoint race strip, and switched-off circuits out of the
profile box's Checkpoint line (src/mm3_circuit_races.c)."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    if not shutil.which("cl"):
        raise SystemExit("Run scripts/enter-env.ps1 first.")
    with tempfile.TemporaryDirectory(prefix="mm3-circuit-strip-") as temporary:
        folder = Path(temporary)
        shutil.copyfile(ROOT / "src/mm3_recomp_types.h", folder / "recomp_types.h")
        executable = folder / "contract.exe"
        subprocess.run(["cl", "/nologo", "/W4", "/WX", "/Od", "/std:c11", "/D_CRT_SECURE_NO_WARNINGS",
                        "/I" + str(folder), "/I" + str(ROOT / "src"), "/I" + str(ROOT / "tools/xboxrecomp/include"),
                        str(ROOT / "tests/mm3_circuit_strip_contract.c"), str(ROOT / "src/mm3_circuit_races.c"),
                        "/Fo:" + str(folder) + "\\", "/Fe:" + str(executable)],
                       check=True, cwd=folder, stdout=subprocess.DEVNULL)
        # Ten Paris circuits installed, switched off (Xbox gameplay).
        data = folder / "game_files" / "Data"
        data.mkdir(parents=True)
        (data / "mm3_circuits.ini").write_text("".join(f"1 {race} Circuit {race}\n" for race in range(20, 30)))
        environment = dict(os.environ, MM3_EXTRA_CIRCUITS="0")
        subprocess.run([str(executable)], check=True, cwd=folder, env=environment)


if __name__ == "__main__":
    main()
