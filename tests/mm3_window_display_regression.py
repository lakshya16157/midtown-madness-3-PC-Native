"""Compile and run host display controls against real hidden Win32 windows."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    if not shutil.which("cl"):
        raise SystemExit("Run scripts/enter-env.ps1 first so cl.exe is available.")
    with tempfile.TemporaryDirectory(prefix="mm3-window-display-") as temporary:
        folder = Path(temporary)
        executable = folder / "contract.exe"
        subprocess.run(["cl", "/nologo", "/W3", "/WX", "/O2", "/std:c11",
                        "/D_CRT_SECURE_NO_WARNINGS",
                        "/I" + str(ROOT / "tools/xboxrecomp/src"),
                        str(ROOT / "tests/mm3_window_display_contract.c"),
                        "/Fo:" + str(folder / "contract.obj"),
                        "/Fe:" + str(executable), "user32.lib", "xinput.lib"],
                       cwd=folder, check=True)
        for arguments in ([], ["preconfigured"]):
            subprocess.run([str(executable), *arguments], cwd=folder,
                           check=True, timeout=20)


if __name__ == "__main__":
    main()
