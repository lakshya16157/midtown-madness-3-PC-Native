"""Launches without play.ps1 (Steam shortcuts) get a log; captured launches are unchanged."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

CONTRACT = r'''
#include <stdio.h>
#include <stdlib.h>
#include "mm3_direct_log.h"
static const char *value(const char *key) { const char *v = getenv(key); return v ? v : "-"; }
int main(void)
{
    mm3_direct_launch_log();
    fprintf(stderr, "contract stderr line\n");
    fflush(stderr);
    printf("profile=%s\nhitch=%s\ncsv=%s\n", value("RECOMP_D3D_PROFILE"),
           value("RECOMP_HITCH_TRACE"), value("RECOMP_FRAME_CSV"));
    return 0;
}
'''


def run(exe, cwd, stderr, **env):
    environment = {k: v for k, v in os.environ.items()
                   if k not in ("RECOMP_D3D_PROFILE", "RECOMP_HITCH_TRACE",
                                "RECOMP_FRAME_CSV", "MM3_DIRECT_LOG")}
    environment.update(env)
    done = subprocess.run([str(exe)], cwd=cwd, stdout=subprocess.PIPE, stderr=stderr,
                          env=environment, check=True)
    values = dict(line.split("=", 1) for line in done.stdout.decode().splitlines())
    return values, done.stderr


def logs(folder):
    directory = folder / "logs"
    return sorted(p.name for p in directory.iterdir()) if directory.exists() else []


def main():
    if not shutil.which("cl"):
        raise SystemExit("Run scripts/enter-env.ps1 first so cl.exe is available.")
    with tempfile.TemporaryDirectory(prefix="mm3-direct-log-") as temporary:
        folder = Path(temporary)
        (folder / "contract.c").write_text(CONTRACT)
        exe = folder / "contract.exe"
        subprocess.run(["cl", "/nologo", "/W4", "/WX", "/O2", "/std:c11", "/I" + str(ROOT / "src"),
                        str(folder / "contract.c"), str(ROOT / "src/mm3_direct_log.c"),
                        "/Fe:" + str(exe), "user32.lib"], cwd=folder, check=True,
                       capture_output=True)

        # play.ps1 and the benchmark harness pipe stderr: nothing changes.
        game = folder / "piped"
        game.mkdir()
        values, captured = run(exe, game, subprocess.PIPE)
        assert b"contract stderr line" in captured and not logs(game), (captured, logs(game))
        assert values == {"profile": "-", "hitch": "-", "csv": "-"}, values

        # A Steam shortcut leaves stderr on a console (a character device, as NUL is).
        game = folder / "steam"
        game.mkdir()
        values, _ = run(exe, game, subprocess.DEVNULL)
        names = logs(game)
        assert len(names) == 1 and names[0].startswith("direct-") and names[0].endswith(".log"), names
        text = (game / "logs" / names[0]).read_text(encoding="utf-8")
        assert "[MM3_DIRECT] launched without play.ps1" in text and "contract stderr line" in text, text
        assert values["profile"] == "1" and values["hitch"] == "1", values
        csv = Path(values["csv"])
        assert csv.is_absolute() and csv.name == names[0][:-4] + "-frames.csv", values

        # Explicit choices win, and MM3_DIRECT_LOG=0 keeps the console.
        values, _ = run(exe, game, subprocess.DEVNULL, RECOMP_D3D_PROFILE="0")
        assert values["profile"] == "0", values
        before = logs(game)
        values, _ = run(exe, game, subprocess.DEVNULL, MM3_DIRECT_LOG="0")
        assert logs(game) == before and values["profile"] == "-", (before, logs(game), values)

        # Only the newest five sessions (log plus frame CSV) are kept.
        game = folder / "pruned"
        (game / "logs").mkdir(parents=True)
        for second in range(7):
            stem = f"direct-20200101-00000{second}"
            (game / "logs" / (stem + ".log")).write_text("old")
            (game / "logs" / (stem + "-frames.csv")).write_text("old")
        (game / "logs" / "play-20200101-000000.log").write_text("play.ps1 log")
        run(exe, game, subprocess.DEVNULL)
        names = logs(game)
        kept = [n for n in names if n.startswith("direct-") and n.endswith(".log")]
        assert len(kept) == 5 and "direct-20200101-000003.log" in kept, names
        assert "direct-20200101-000002.log" not in names, names
        assert "direct-20200101-000002-frames.csv" not in names, names
        assert "direct-20200101-000003-frames.csv" in names, names
        assert "play-20200101-000000.log" in names, names
    print("PASS: piped launches unchanged; console launches log to logs\\direct-*.log with frame "
          "timing; explicit settings and MM3_DIRECT_LOG=0 respected; newest five sessions kept")


if __name__ == "__main__":
    main()
