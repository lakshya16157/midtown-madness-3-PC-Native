"""Build production launcher/config and exercise isolated preferences and native UI."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from mm3_update_regression import serve   # a local stand-in for GitHub's releases


def main():
    if not shutil.which("cl"):
        raise SystemExit("Run scripts/enter-env.ps1 first so cl.exe is available.")
    server = serve()
    try:
        run(server.server_port)
    finally:
        server.shutdown()


def run(port):
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
        # The launcher's header logo is generated from the installed game (an empty one without it).
        subprocess.run([sys.executable, str(ROOT / "tools/branding/generate_logo.py"),
                        "--archive", str(ROOT / "game_files/Data/Data_dvd.zip"),
                        "--header", str(folder / "branding/mm3_logo.h")], check=True, cwd=folder)
        subprocess.run(common + ["/I" + str(folder / "branding"), "/DMM3_LAUNCHER_AUDIO=1", "/DMM3_LAUNCHER_NETWORK=1",
            str(ROOT / "tests/mm3_launcher_ui_contract.c"),
            str(ROOT / "src/mm3_launcher_config.c"), str(ROOT / "src/mm3_display_config.c"),
            str(ROOT / "src/mm3_update.c"),
            "/Fe:" + str(ui), "/link", "user32.lib", "gdi32.lib", "comctl32.lib", "xinput.lib",
            "shell32.lib", "winhttp.lib", "bcrypt.lib"],
            check=True, cwd=folder)
        environment = {key: value for key, value in environment.items() if not key.lower().endswith("_proxy")}
        subprocess.run([str(ui), str(folder), str(port)], check=True, cwd=folder,
                       env=environment, timeout=90)


if __name__ == "__main__":
    main()
