"""F10's Stop the time limit holds the scripts' race_timer (src/mm3_stop_timer.c)."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    if not shutil.which("cl"):
        raise SystemExit("Run scripts/enter-env.ps1 first.")
    with tempfile.TemporaryDirectory(prefix="mm3-stop-timer-") as temporary:
        folder = Path(temporary)
        shutil.copyfile(ROOT / "src/mm3_recomp_types.h", folder / "recomp_types.h")
        shutil.copyfile(ROOT / "src/mm3_cheat_keys.h", folder / "mm3_cheat_keys.h")
        executable = folder / "contract.exe"
        subprocess.run(["cl", "/nologo", "/W4", "/WX", "/wd4100", "/Od", "/std:c11", "/D_CRT_SECURE_NO_WARNINGS",
                        "/I" + str(folder), "/I" + str(ROOT / "tools/xboxrecomp/include"),
                        str(ROOT / "tests/mm3_stop_timer_contract.c"), str(ROOT / "src/mm3_stop_timer.c"),
                        "/Fo:" + str(folder) + "\\", "/Fe:" + str(executable)],
                       check=True, cwd=folder, stdout=subprocess.DEVNULL)
        subprocess.run([str(executable)], check=True, cwd=folder)


if __name__ == "__main__":
    main()
