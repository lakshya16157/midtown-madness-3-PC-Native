"""Verify real renderer profiler control flow without a game process or GPU."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    if not shutil.which("cl"):
        raise SystemExit("Run scripts/enter-env.ps1 first so cl.exe is available.")
    source = (ROOT / "tools/xboxrecomp/src/video/nv2a_d3d11.c").read_text()
    bodies = []
    for name in ("nv2a_d3d_prof_enabled", "prof_report"):
        match = re.search(r"^(?:static )?(?:int|void) " + name + r"\([^\n]*\)\n\{.*?^\}", source, re.M | re.S)
        if match is None:
            raise ValueError(f"Missing production {name} body")
        bodies.append(match.group())
    with tempfile.TemporaryDirectory(prefix="mm3-renderer-profile-") as folder:
        folder = Path(folder)
        (folder / "production_profile.inc").write_text("\n\n".join(bodies))
        executable = folder / "contract.exe"
        subprocess.run(["cl", "/nologo", "/W4", "/WX", "/Od", "/std:c11",
                        "/D_CRT_SECURE_NO_WARNINGS", "/I" + str(folder),
                        str(ROOT / "tests/mm3_renderer_profile_contract.c"),
                        "/Fo:" + str(folder / "contract.obj"), "/Fe:" + str(executable)],
                       check=True, cwd=folder)
        for setting in ("unset", "", "0", "1", "yes"):
            subprocess.run([str(executable), setting], check=True)


if __name__ == "__main__":
    main()
