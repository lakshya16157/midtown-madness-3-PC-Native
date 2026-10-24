"""Screen-widget link guard on the widget draw (src/mm3_ui_guard.c)."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    if not shutil.which("cl"):
        raise SystemExit("Run scripts/enter-env.ps1 first.")
    with tempfile.TemporaryDirectory(prefix="mm3-ui-guard-") as temporary:
        folder = Path(temporary)
        shutil.copyfile(ROOT / "src/mm3_recomp_types.h", folder / "recomp_types.h")
        executable = folder / "contract.exe"
        subprocess.run(["cl", "/nologo", "/W4", "/WX", "/Od", "/std:c11", "/D_CRT_SECURE_NO_WARNINGS",
                        "/I" + str(folder), "/I" + str(ROOT / "tools/xboxrecomp/include"),
                        str(ROOT / "tests/mm3_ui_guard_contract.c"), str(ROOT / "src/mm3_ui_guard.c"),
                        str(ROOT / "src/mm3_heap_guard.c"), str(ROOT / "src/mm3_unresolved_observe.c"),
                        "/Fo:" + str(folder) + "\\", "/Fe:" + str(executable)],
                       check=True, cwd=folder, stdout=subprocess.DEVNULL)
        done = subprocess.run([str(executable)], check=True, cwd=folder, capture_output=True, text=True)
        print(done.stdout.strip())
        # The first overwritten link (0x00100004, cut) is watched in every
        # thread. After that the contract writes there seven times (making its
        # widgets) and the guard six times (cutting links): exactly the
        # contract's seven writes report themselves.
        log = done.stderr
        assert "[MM3_UI_WATCH] watching 00100004 for writes in " in log, log
        assert " for writes in 0 threads" not in log, log
        hits = [line for line in log.splitlines() if line.startswith("[MM3_UI_WATCH] 00100004 = ")]
        assert all("written at rva " in line for line in hits), log
        values = [line.split()[3] for line in hits]
        assert values == ["00100140", "00100140", "00100140", "00000000", "00100100", "00100100", "00100100"], values
        print(f"PASS UI watch: the overwritten link is watched; {len(hits)} later writes reported, "
              "none of the guard's own")
        # Only the screen deleted behind its menu's back is reported.
        screens = [line for line in log.splitlines() if line.startswith("[MM3_MENU_SCREEN]")]
        assert len(screens) == 1 and "menu id 5 " in screens[0] and "00123456" in screens[0], screens
        frees = [line for line in log.splitlines() if line.startswith("[HEAP_DOUBLE_FREE]")]
        assert len(frees) == 1 and "pointer=00220010" in frees[0], frees
        stubs = [line.split()[1] for line in log.splitlines() if line.startswith("[MM3_UNRESOLVED]")]
        assert stubs == ["0x00241E85", "0x00012EA3"], stubs
        print("PASS unresolved stubs: each reports the first time it runs, once")


if __name__ == "__main__":
    main()
