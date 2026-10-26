"""Exhaust smoke (src/mm3_exhaust.c), on and off."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    if not shutil.which("cl"):
        raise SystemExit("Run scripts/enter-env.ps1 first.")
    with tempfile.TemporaryDirectory(prefix="mm3-exhaust-") as temporary:
        folder = Path(temporary)
        shutil.copyfile(ROOT / "src/mm3_recomp_types.h", folder / "recomp_types.h")
        executable = folder / "contract.exe"
        subprocess.run(["cl", "/nologo", "/W4", "/WX", "/Od", "/std:c11", "/D_CRT_SECURE_NO_WARNINGS",
                        "/I" + str(folder), "/I" + str(ROOT / "src"), "/I" + str(ROOT / "tools/xboxrecomp/src"),
                        "/I" + str(ROOT / "tools/xboxrecomp/include"),
                        str(ROOT / "tests/mm3_exhaust_contract.c"), str(ROOT / "src/mm3_exhaust.c"),
                        "/Fo:" + str(folder) + "\\", "/Fe:" + str(executable)],
                       check=True, cwd=folder, stdout=subprocess.DEVNULL)
        environment = {key: value for key, value in os.environ.items() if not key.startswith("MM3_")}
        subprocess.run([str(executable), "off"], check=True, cwd=folder, env=environment)
        environment["MM3_EXHAUST_TRIANGLES"] = "1"
        subprocess.run([str(executable)], check=True, cwd=folder, env=environment)


if __name__ == "__main__":
    main()
