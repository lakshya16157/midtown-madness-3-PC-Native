"""Compile the production vehicle-menu ordering hook and verify its guest ABI."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    # Installer and native classification must agree about retail bonus IDs.
    native = (ROOT / "src/mm3_car_order.c").read_text()
    installer = (ROOT / "tools/car-mods/prepare.py").read_text()
    c_ids = re.search(r"original_car_ids\[\]\s*=\s*\{([^}]+)\}", native)[1]
    py_ids = re.search(r"ORIGINAL_CAR_IDS\s*=\s*\(([^)]+)\)", installer)[1]
    assert re.findall(r"\d+", c_ids) == re.findall(r"\d+", py_ids)
    if not shutil.which("cl"):
        raise SystemExit("Run scripts/enter-env.ps1 first so cl.exe is available.")
    with tempfile.TemporaryDirectory(prefix="mm3-car-order-") as temporary:
        folder = Path(temporary)
        shutil.copyfile(ROOT / "src/mm3_recomp_types.h", folder / "recomp_types.h")
        executable = folder / "contract.exe"
        subprocess.run([
            "cl", "/nologo", "/W4", "/WX", "/Od", "/std:c11",
            "/I" + str(folder), "/I" + str(ROOT / "tools/xboxrecomp/include"),
            str(ROOT / "tests/mm3_car_order_contract.c"),
            "/Fo:" + str(folder / "contract.obj"), "/Fe:" + str(executable),
        ], check=True, cwd=folder)
        environment = {k: v for k, v in os.environ.items() if k not in ("MM3_SAME_CAR", "MM3_EXTRA_CARS")}
        subprocess.run([str(executable)], check=True, cwd=folder, timeout=20, env=environment)
        # The "Opponents drive your car" mod (MM3_SAME_CAR), read once per thread.
        subprocess.run([str(executable), "same"], check=True, cwd=folder, timeout=20,
                       env=dict(environment, MM3_SAME_CAR="1"))
        # The installer's cars left out (MM3_EXTRA_CARS=0), also read once per thread.
        subprocess.run([str(executable), "hidden"], check=True, cwd=folder, timeout=20,
                       env=dict(environment, MM3_EXTRA_CARS="0"))


if __name__ == "__main__":
    main()
