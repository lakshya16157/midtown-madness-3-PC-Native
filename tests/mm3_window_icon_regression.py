"""Verify embedded large/small icons on the real Windows framebuffer window."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    if not shutil.which("cl") or not shutil.which("rc"):
        raise SystemExit("Run scripts/enter-env.ps1 first so cl.exe and rc.exe are available.")
    with tempfile.TemporaryDirectory(prefix="mm3-window-icon-") as temporary:
        folder = Path(temporary)
        resource = folder / "mm3.res"
        subprocess.run(["rc", "/nologo", "/I", str(ROOT / "src"),
                        "/fo", str(resource), str(ROOT / "src/mm3.rc")],
                       cwd=folder, check=True)
        command = ["cl", "/nologo", "/W3", "/WX", "/O2", "/std:c11",
                   "/D_CRT_SECURE_NO_WARNINGS",
                   "/I" + str(ROOT / "tools/xboxrecomp/src"),
                   str(ROOT / "tests/mm3_window_icon_contract.c"),
                   "/Fo:" + str(folder / "contract.obj"),
                   "user32.lib", "gdi32.lib"]
        for embedded in (True, False):
            executable = folder / ("with-icon.exe" if embedded else "without-icon.exe")
            subprocess.run([*command, "/Fe:" + str(executable),
                            *([str(resource)] if embedded else [])],
                           cwd=folder, check=True)
            subprocess.run([str(executable), *([] if embedded else ["without-resource"])],
                           cwd=folder, check=True, timeout=15)


if __name__ == "__main__":
    main()
