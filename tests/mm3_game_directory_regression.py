"""A Steam shortcut starts mm3_recomp.exe in its own folder; the game must still find game_files."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def run(executable, cwd):
    out = subprocess.run([str(executable)], cwd=cwd, check=True, capture_output=True).stdout
    lines = dict(line.split("=", 1) for line in out.decode("utf-8").splitlines())
    return lines["entered"], Path(lines["cwd"])


def same(a, b):
    return os.path.normcase(os.path.realpath(a)) == os.path.normcase(os.path.realpath(b))


def make_game(folder):
    (folder / "game_files").mkdir(parents=True)
    (folder / "game_files" / "default.xbe").write_bytes(b"XBEH")


def main():
    if not shutil.which("cl"):
        raise SystemExit("Run scripts/enter-env.ps1 first so cl.exe is available.")
    source = (ROOT / "src/main.c").read_text()
    begin = source.index("static int mm3_is_file(")
    end = source.index("\nint main(void)", begin)
    # Non-ASCII and a space, like a real install under a user's Desktop.
    with tempfile.TemporaryDirectory(prefix="mm3-game-directory-") as temporary:
        folder = Path(temporary)
        (folder / "production_game_directory.inc").write_text(source[begin:end])
        built = folder / "contract.exe"
        subprocess.run(["cl", "/nologo", "/W3", "/WX", "/O2", "/std:c11",
                        "/I" + str(ROOT / "src"), "/I" + str(folder),
                        str(ROOT / "tests/mm3_game_directory_contract.c"),
                        "/Fo:" + str(folder / "contract.obj"),
                        "/Fe:" + str(built)], cwd=folder, check=True,
                       capture_output=True)

        # Repository layout: exe in build\RelWithDebInfo, Steam's default Start In.
        repo = folder / "Mídtown Madness 3" / "native"
        make_game(repo)
        build = repo / "build" / "RelWithDebInfo"
        build.mkdir(parents=True)
        shutil.copy(built, build / "mm3_recomp.exe")
        entered, cwd = run(build / "mm3_recomp.exe", build)
        assert entered != "-" and same(cwd, repo), (entered, cwd)

        # A working directory that already holds the game is kept.
        entered, cwd = run(build / "mm3_recomp.exe", repo)
        assert entered == "-" and same(cwd, repo), (entered, cwd)

        # Installer layout: exe beside game_files, launched from elsewhere.
        installed = folder / "Installed Game"
        make_game(installed)
        shutil.copy(built, installed / "mm3_recomp.exe")
        elsewhere = folder / "elsewhere"
        elsewhere.mkdir()
        entered, cwd = run(installed / "mm3_recomp.exe", elsewhere)
        assert entered != "-" and same(cwd, installed), (entered, cwd)

        # No game anywhere: leave the working directory alone so the existing
        # "cannot load game_files/default.xbe" report is unchanged.
        lonely = folder / "lonely" / "bin"
        lonely.mkdir(parents=True)
        shutil.copy(built, lonely / "mm3_recomp.exe")
        entered, cwd = run(lonely / "mm3_recomp.exe", lonely)
        assert entered == "-" and same(cwd, lonely), (entered, cwd)
    print("game directory regression passed")


if __name__ == "__main__":
    main()
