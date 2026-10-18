"""Compile the actual title fog/horizon hooks against sparse synthetic guest RAM."""
from pathlib import Path
import shutil
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="mm3-native-fog-") as temporary:
    folder = Path(temporary)
    shutil.copyfile(ROOT / "src/mm3_recomp_types.h", folder / "recomp_types.h")
    executable = folder / "contract.exe"
    subprocess.run(["cl", "/nologo", "/W4", "/WX", "/Od", "/std:c11", "/D_CRT_SECURE_NO_WARNINGS",
                    "/I" + str(folder), "/I" + str(ROOT / "tools/xboxrecomp/include"),
                    str(ROOT / "tests/mm3_native_fog_contract.c"),
                    "/Fo:" + str(folder / "contract.obj"), "/Fe:" + str(executable)],
                   check=True, cwd=folder)
    for enabled in ("0", "1"):
        subprocess.run([str(executable), enabled], check=True, cwd=folder, timeout=20)
