"""Exercise the real crash handler against mapped and damaged guest memory."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    if not shutil.which("cl"):
        raise SystemExit("Run scripts/enter-env.ps1 first so cl.exe is available.")
    source = (ROOT / "src/main.c").read_text()
    begin = source.index("static LONG WINAPI mm3_crash_report(")
    end = source.index("\nstatic int load_file(", begin)
    with tempfile.TemporaryDirectory(prefix="mm3-crash-diagnostics-") as temporary:
        folder = Path(temporary)
        (folder / "production_crash_report.inc").write_text(source[begin:end])
        executable = folder / "contract.exe"
        subprocess.run(["cl", "/nologo", "/W3", "/WX", "/O2", "/std:c11",
                        "/I" + str(ROOT / "src"), "/I" + str(folder),
                        str(ROOT / "tests/mm3_crash_diagnostics_contract.c"),
                        "/Fo:" + str(folder / "contract.obj"),
                        "/Fe:" + str(executable)], cwd=folder, check=True)
        log_path = folder / "crash.log"
        subprocess.run([str(executable), str(log_path)], cwd=folder, check=True)
        log = log_path.read_text()
        for access in ("read", "write", "execute", "unavailable"):
            assert "access=" + access in log, access
        assert re.search(r"\[CRASH\] utc=\d{4}-\d\d-\d\dT.*thread=\d+", log)
        assert "return=0xABCDEF01" in log and "return=0xABCDEF02" in log
        assert "stop=nonascending_parent" in log and "stop=limit count=16" in log
        assert "stop=invalid_or_unreadable" in log
        assert "address=0x100000004 unreadable" in log
        assert "seh_object+18 address=0x10218 value=0x10218102" in log
        assert "pause_active=1" in log and "race=0x00010800" in log
        assert "primary_player address=0x108D8 value=0x00010900" in log
        assert "unavailable: parent unreadable" in log
        assert "race=absent: player_manager null" in log and "race=absent\n" in log
        assert "primary_player unavailable: parent null" in log
        assert log.count("[CRASH] code=") == 10
        assert log.count("[CRASH_GUEST] stack+") == 10 * 12
    print("PASS: crash log includes reliable context, safe pointer chains and bounded stack evidence")


if __name__ == "__main__":
    main()
