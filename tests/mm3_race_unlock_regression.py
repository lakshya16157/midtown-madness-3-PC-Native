"""Build and run the unlock-all-races contract, once with the setting off and once on (no game, GPU or save needed).

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
    with tempfile.TemporaryDirectory(prefix="mm3-race-unlock-") as temporary:
        folder = Path(temporary)
        shutil.copyfile(ROOT / "src/mm3_recomp_types.h", folder / "recomp_types.h")
        shutil.copyfile(ROOT / "tools/xboxrecomp/templates/runtime/recomp_types.h", folder / "recomp_base.h")
        executable = folder / f"contract{cc.exe_suffix()}"
        subprocess.run(cc.compile_command(
            [ROOT / "tests/mm3_race_unlock_contract.c"], executable,
            includes=[folder, ROOT / "tools/xboxrecomp/include", ROOT / "tools/xboxrecomp/src/kernel",
                      ROOT / "tools/xboxrecomp/src"], std="c11", optimize="none", warnings="normal",
            werror=True, obj=folder / "contract.obj"), check=True, cwd=folder)
        for setting in ("off", "on"):
            print(subprocess.run([str(executable), setting], check=True, capture_output=True, text=True).stdout.strip())


if __name__ == "__main__":
    main()
