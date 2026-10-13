"""Verify guest menu registration, live labels and saved draft behavior."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    if not shutil.which("cl"):
        raise SystemExit("Run scripts/enter-env.ps1 first so cl.exe is available.")
    with tempfile.TemporaryDirectory(prefix="mm3-graphics-menu-") as temporary:
        folder = Path(temporary)
        executable = folder / "contract.exe"
        shutil.copyfile(ROOT / "src/mm3_recomp_types.h", folder / "recomp_types.h")
        subprocess.run([
            "cl", "/nologo", "/W4", "/WX", "/Od", "/std:c11", "/D_CRT_SECURE_NO_WARNINGS",
            "/I" + str(folder), "/I" + str(ROOT / "src"),
            "/I" + str(ROOT / "tools/xboxrecomp/include"),
            str(ROOT / "tests/mm3_graphics_menu_contract.c"),
            "/Fe:" + str(executable),
        ], check=True, cwd=folder)
        subprocess.run([str(executable)], check=True, cwd=folder, timeout=20)


if __name__ == "__main__":
    main()
