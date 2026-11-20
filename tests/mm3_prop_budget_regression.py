"""Build and run the street-prop model queue budget contract (no game, GPU or save needed).

Run inside `python scripts/devenv.py --shell`.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

import cc

ROOT = Path(__file__).resolve().parents[1]


def main():
    cc.require_compiler()
    with tempfile.TemporaryDirectory(prefix="mm3-prop-budget-") as folder:
        folder = Path(folder)
        shutil.copyfile(ROOT / "src/mm3_recomp_types.h", folder / "recomp_types.h")
        shutil.copyfile(ROOT / "tools/xboxrecomp/templates/runtime/recomp_types.h", folder / "recomp_base.h")
        exe = folder / f"contract{cc.exe_suffix()}"
        subprocess.run(cc.compile_command([ROOT / "tests/mm3_prop_budget_contract.c"], exe,
                                          includes=[folder, ROOT / "tools/xboxrecomp/include",
                                                    ROOT / "tools/xboxrecomp/src/video", ROOT / "tools/xboxrecomp/src"],
                                          defines=["_CRT_SECURE_NO_WARNINGS"], warnings="normal"),
                       check=True, capture_output=True, text=True, cwd=folder)
        print(subprocess.run([str(exe)], check=True, capture_output=True, text=True).stdout.strip())


if __name__ == "__main__":
    main()
