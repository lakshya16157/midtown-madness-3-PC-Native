"""Verify the facade stream use counters without a game process."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    if not shutil.which("cl"):
        raise SystemExit("Run scripts/enter-env.ps1 first so cl.exe is available.")
    with tempfile.TemporaryDirectory(prefix="mm3-facade-streams-") as folder:
        folder = Path(folder)
        executable = folder / "contract.exe"
        subprocess.run(["cl", "/nologo", "/W3", "/wd4102", "/Od", "/std:c11", "/D_CRT_SECURE_NO_WARNINGS",
                        "/I" + str(ROOT / "src"), "/I" + str(ROOT / "src/recomp/gen"),
                        "/I" + str(ROOT / "tools/xboxrecomp/include"),
                        str(ROOT / "tests/mm3_facade_streams_contract.c"),
                        "/Fo:" + str(folder / "contract.obj"), "/Fe:" + str(executable)],
                       check=True, cwd=folder)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
