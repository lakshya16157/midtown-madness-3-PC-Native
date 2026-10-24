"""The language chosen in Game Setup survives a restart (src/mm3_language.c)."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    if not shutil.which("cl"):
        raise SystemExit("Run scripts/enter-env.ps1 first.")
    with tempfile.TemporaryDirectory(prefix="mm3-language-") as temporary:
        folder = Path(temporary)
        shutil.copyfile(ROOT / "src/mm3_recomp_types.h", folder / "recomp_types.h")
        executable = folder / "contract.exe"
        subprocess.run(["cl", "/nologo", "/W4", "/WX", "/wd4100", "/Od", "/std:c11", "/D_CRT_SECURE_NO_WARNINGS",
                        "/I" + str(folder), "/I" + str(ROOT / "src"), "/I" + str(ROOT / "tools/xboxrecomp/include"),
                        str(ROOT / "tests/mm3_language_contract.c"), str(ROOT / "src/mm3_language.c"),
                        str(ROOT / "src/mm3_display_config.c"),
                        "/Fo:" + str(folder) + "\\", "/Fe:" + str(executable), "user32.lib"],
                       check=True, cwd=folder, stdout=subprocess.DEVNULL)
        subprocess.run([str(executable), str(folder)], check=True, cwd=folder)


if __name__ == "__main__":
    main()
