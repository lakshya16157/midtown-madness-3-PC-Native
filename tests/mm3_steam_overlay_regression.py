"""Build and run the Steam overlay contract (issue #47) against a stand-in overlay DLL (no Steam, game or GPU needed).

Run inside `python scripts/devenv.py --shell`. The stand-in lives in a temporary folder used as SteamPath, so the
real Steam folder is never touched; each scenario runs with every Steam variable removed from the environment.
"""
from pathlib import Path
import os
import subprocess
import tempfile

import cc

ROOT = Path(__file__).resolve().parents[1]
DLL_NAME = "GameOverlayRenderer64.dll"
DLL_SOURCE = """#include <windows.h>
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved)
{
    (void)module; (void)reason; (void)reserved;
    return TRUE;
}
"""


def scrubbed(**extra):
    env = {k: v for k, v in os.environ.items() if not k.lower().startswith("steam") and k != "MM3_STEAM_OVERLAY"}
    env.update(extra)
    return env


def main():
    cc.require_compiler()
    cc.skip_unless_windows("the overlay is a Windows DLL")
    with tempfile.TemporaryDirectory(prefix="mm3-steam-overlay-") as temporary:
        folder = Path(temporary)
        steam = folder / "Steam"
        broken = folder / "BrokenSteam"
        steam.mkdir()
        broken.mkdir()

        dll_source = folder / "overlay.c"
        dll_source.write_text(DLL_SOURCE)
        subprocess.run(cc.compile_command(
            [dll_source], steam / DLL_NAME, std="c11", optimize="none", warnings="normal",
            msvc_extra=("/LD",), gcc_extra=("-shared",), obj=folder / "overlay.obj"), check=True, cwd=folder)
        (broken / DLL_NAME).write_text("this is not a library")

        executable = folder / f"contract{cc.exe_suffix()}"
        subprocess.run(cc.compile_command(
            [ROOT / "tests/mm3_steam_overlay_contract.c"], executable, std="c11", optimize="none", warnings="normal",
            werror=True, obj=folder / "contract.obj"), check=True, cwd=folder)

        # Forward slashes and a trailing slash, as Steam writes SteamPath.
        steam_path = str(steam).replace("\\", "/") + "/"
        scenarios = (
            ("not-steam", scrubbed(), []),
            ("opt-out", scrubbed(SteamGameId="12345", SteamPath=steam_path, MM3_STEAM_OVERLAY="0"), []),
            ("load", scrubbed(SteamGameId="12345", SteamPath=steam_path), []),
            ("load", scrubbed(SteamOverlayGameId="12345", SteamPath=steam_path), []),
            ("already", scrubbed(SteamGameId="12345", SteamPath=steam_path), [str(steam / DLL_NAME)]),
            ("broken", scrubbed(SteamGameId="12345", SteamPath=str(broken)), []),
        )
        # The library pictures: five files to copy, two of which the user already has in another form.
        source = folder / "steam-artwork"
        grid = folder / "userdata" / "1" / "config" / "grid"
        source.mkdir()
        grid.mkdir(parents=True)
        for name in ("capsule-portrait.png", "capsule-wide.png", "hero.png", "logo.png", "icon.png"):
            (source / name).write_bytes(b"picture " + name.encode())
        (grid / "2735476592p.jpg").write_bytes(b"their portrait")
        (grid / "2735476592_hero.png").write_bytes(b"their hero")
        scenarios += (("number", scrubbed(), []), ("art", scrubbed(), [str(source), str(grid)]))
        for scenario, env, extra in scenarios:
            result = subprocess.run([str(executable), scenario, *extra], env=env, check=True,
                                    capture_output=True, text=True)
            print(result.stdout.strip())
            if scenario == "load":
                assert "[MM3_STEAM]" in result.stderr and "loaded" in result.stderr, result.stderr
            if scenario == "broken":
                assert "could not load" in result.stderr, result.stderr
        names = sorted(path.name for path in grid.iterdir())
        assert names == sorted(["2735476592p.jpg", "2735476592_hero.png", "2735476592.png", "2735476592_logo.png",
                                "2735476592_icon.png", "7p.png", "7.png", "7_hero.png", "7_logo.png", "7_icon.png"]), names
        assert (grid / "2735476592p.jpg").read_bytes() == b"their portrait"
        assert (grid / "2735476592_hero.png").read_bytes() == b"their hero"
        assert (grid / "7_logo.png").read_bytes() == b"picture logo.png"


if __name__ == "__main__":
    main()
