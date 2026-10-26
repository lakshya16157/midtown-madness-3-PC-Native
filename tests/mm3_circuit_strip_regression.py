"""Circuit pictures stay out of the Checkpoint race strip (src/mm3_circuit_races.c)."""
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
        subprocess.run([str(executable)], check=True, cwd=folder)


if __name__ == "__main__":
    main()
