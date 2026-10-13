"""Exercise persistent production settings without starting the game or touching user config."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    if not shutil.which("cl"):
        raise SystemExit("Run scripts/enter-env.ps1 first so cl.exe is available.")
    with tempfile.TemporaryDirectory(prefix="mm3-display-config-") as temporary:
        folder = Path(temporary)
        executable = folder / "contract.exe"
        subprocess.run([
            "cl", "/nologo", "/W4", "/WX", "/Od", "/std:c11", "/utf-8",
            "/D_CRT_SECURE_NO_WARNINGS", "/I" + str(ROOT / "src"),
            str(ROOT / "tests/mm3_display_config_contract.c"),
            str(ROOT / "src/mm3_display_config.c"),
            "/Fo:" + str(folder) + "/", "/Fe:" + str(executable),
        ], check=True, cwd=folder)
        subprocess.run([str(executable), str(folder)], check=True, cwd=folder)


if __name__ == "__main__":
    main()
