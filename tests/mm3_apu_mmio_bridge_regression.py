"""Exercise the native bridge and production Windows exception decoder."""
import json
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    if not shutil.which("cl"):
        raise SystemExit("Run scripts/enter-env.ps1 first.")
    results = []
    with tempfile.TemporaryDirectory(prefix="mm3-apu-bridge-") as temporary:
        directory = Path(temporary)
        executable = directory / "bridge.exe"
        subprocess.run(["cl", "/nologo", "/O2", "/Ob1", "/MD", "/std:c11",
                        "/I" + str(ROOT / "src"),
                        "/I" + str(ROOT / "tools/xboxrecomp/src/apu"),
                        "/Fo:" + str(directory) + "/", "/Fe:" + str(executable),
                        str(ROOT / "tests/mm3_apu_mmio_contract.c"),
                        str(ROOT / "src/mm3_apu_mmio.c"),
                        str(ROOT / "tools/xboxrecomp/src/apu/apu_mmio_hook.c")],
                       cwd=directory, check=True)
        for mode in ("native", "disabled", "missing", "trace"):
            result = subprocess.run([str(executable), mode], check=True, text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            print(mode, result.stdout, end="")
            results.append({"mode": mode, "exit_code": result.returncode,
                            "stdout": result.stdout, "stderr": result.stderr})
    target = ROOT / "logs/dc-smoothness-pass2-20261019/apu-native-bridge-validation.json"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps({"production_bridge": "src/mm3_apu_mmio.c",
                                 "production_decoder": "tools/xboxrecomp/src/apu/apu_mmio_hook.c",
                                 "results": results,
                                 "scope": "Controlled register backend; per-access microbenchmark, not whole-game FPS."}, indent=2) + "\n")


if __name__ == "__main__":
    main()
