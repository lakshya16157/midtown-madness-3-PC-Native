"""Compile the actual cache-load hook against adversarial collision cells."""
from pathlib import Path
import shutil
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="mm3-collision-") as temp:
    directory = Path(temp)
    shutil.copyfile(ROOT / "src/mm3_recomp_types.h", directory / "recomp_types.h")
    executable = directory / "contract.exe"
    subprocess.run(["cl", "/nologo", "/W4", "/WX", "/O2", "/std:c11",
                    "/I" + str(directory), "/I" + str(ROOT / "tools/xboxrecomp/include"),
                    str(ROOT / "tests/mm3_collision_cells_contract.c"),
                    "/Fo:" + str(directory / "contract.obj"), "/Fe:" + str(executable)],
                   cwd=directory, check=True)
    subprocess.run([str(executable)], cwd=directory, check=True, timeout=10)
