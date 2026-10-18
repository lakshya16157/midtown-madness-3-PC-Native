"""Build production launcher/config and exercise isolated preferences and native UI."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    if not shutil.which("cl"):
        raise SystemExit("Run scripts/enter-env.ps1 first so cl.exe is available.")
    with tempfile.TemporaryDirectory(prefix="mm3-launcher-") as temporary:
        folder = Path(temporary)
        common = ["cl", "/nologo", "/W4", "/WX", "/Od", "/std:c11", "/utf-8",
                  "/D_CRT_SECURE_NO_WARNINGS", "/I" + str(ROOT / "src")]
        config = folder / "config.exe"
        subprocess.run(common + [str(ROOT / "tests/mm3_launcher_config_contract.c"),
            str(ROOT / "src/mm3_launcher_config.c"), str(ROOT / "src/mm3_display_config.c"),
            "/Fe:" + str(config)], check=True, cwd=folder)
        environment = {key: value for key, value in os.environ.items()
                       if not key.startswith(("MM3_", "RECOMP_")) or key == "MM3_LAUNCHER_CAPTURE_DIR"}
        subprocess.run([str(config), str(folder / "launcher.ini")], check=True,
                       cwd=folder, env=environment)
        ui = folder / "launcher.exe"
        subprocess.run(common + ["/DMM3_LAUNCHER_AUDIO=1", "/DMM3_LAUNCHER_NETWORK=1",
            str(ROOT / "tests/mm3_launcher_ui_contract.c"),
            str(ROOT / "src/mm3_launcher_config.c"), str(ROOT / "src/mm3_display_config.c"),
            "/Fe:" + str(ui), "/link", "user32.lib", "gdi32.lib", "comctl32.lib", "xinput.lib"],
            check=True, cwd=folder)
        subprocess.run([str(ui), str(folder)], check=True, cwd=folder,
                       env=environment, timeout=40)


if __name__ == "__main__":
    main()
