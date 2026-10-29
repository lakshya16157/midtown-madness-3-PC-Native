"""Exercise the actual settings dialog without launching another game."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    if not shutil.which("cl") or not shutil.which("rc"):
        raise SystemExit("Run scripts/enter-env.ps1 first so cl.exe and rc.exe are available.")
    with tempfile.TemporaryDirectory(prefix="mm3-display-ui-") as temporary:
        folder = Path(temporary)
        resource = folder / "display.res"
        executable = folder / "contract.exe"
        subprocess.run(["rc", "/nologo", "/I" + str(ROOT / "src"),
                        "/fo" + str(resource), str(ROOT / "src/mm3.rc")], check=True, cwd=folder)
        subprocess.run([
            "cl", "/nologo", "/W4", "/WX", "/Od", "/std:c11", "/D_CRT_SECURE_NO_WARNINGS",
            "/I" + str(ROOT / "tools/xboxrecomp/src"),
            "/I" + str(ROOT / "tools/xboxrecomp/src/input"),
            str(ROOT / "tests/mm3_display_ui_contract.c"),
            str(ROOT / "src/mm3_display_config.c"), str(resource),
            "/Fe:" + str(executable), "/link", "user32.lib", "gdi32.lib",
        ], check=True, cwd=folder)
        environment = {key: value for key, value in os.environ.items()
                       if not key.startswith(("MM3_", "RECOMP_")) or key == "MM3_UI_CAPTURE"}
        subprocess.run([str(executable), str(folder / "settings.ini")],
                       check=True, cwd=folder, env=environment, timeout=40)
        subprocess.run([str(executable), str(folder / "fixed.ini"), "fixed"],
                       check=True, cwd=folder, env=environment, timeout=40)


if __name__ == "__main__":
    main()
