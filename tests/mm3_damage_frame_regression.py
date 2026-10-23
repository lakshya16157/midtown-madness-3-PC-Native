"""Wreck-job car-to-car damage follows the frame time (src/mm3_damage_diagnostics.c)."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    if not shutil.which("cl"):
        raise SystemExit("Run scripts/enter-env.ps1 first.")
    with tempfile.TemporaryDirectory(prefix="mm3-damage-frame-") as temporary:
        folder = Path(temporary)
        shutil.copyfile(ROOT / "src/mm3_recomp_types.h", folder / "recomp_types.h")
        executable = folder / "contract.exe"
        subprocess.run(["cl", "/nologo", "/W4", "/WX", "/wd4100", "/Od", "/std:c11", "/D_CRT_SECURE_NO_WARNINGS",
                        "/I" + str(folder), "/I" + str(ROOT / "tools/xboxrecomp/include"),
                        str(ROOT / "tests/mm3_damage_frame_contract.c"), str(ROOT / "src/mm3_damage_diagnostics.c"),
                        "/Fo:" + str(folder) + "\\", "/Fe:" + str(executable)],
                       check=True, cwd=folder, stdout=subprocess.DEVNULL)
        for mode in ("on", "off"):
            subprocess.run([str(executable), mode], check=True, cwd=folder)


if __name__ == "__main__":
    main()
