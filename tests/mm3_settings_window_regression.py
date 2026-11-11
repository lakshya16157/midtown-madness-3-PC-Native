"""The in-game Settings window (src/mods/debug_overlay.cpp, F10 where the Windows dialog is absent) with
fake backends: it stays open, a click reaches the Freeze switch, a choice is committed, Escape closes it.
Needs the SDL3 GPU build tree (build-vulkan) for the ImGui and SDL sources and MSVC's cl; otherwise skips."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    tree = next((ROOT / name for name in ("build-vulkan", "build-linux", "build") if (ROOT / name / "_deps/imgui-src/imgui.cpp").exists()
                 and (ROOT / name / "_deps/sdl3-src/include").exists()), None)
    sdl_lib = next(iter((tree / "_deps/sdl3-build").rglob("SDL3-static.lib")), None) if tree else None
    if not tree or not sdl_lib or not shutil.which("cl"):
        print("SKIP: needs build-vulkan (ImGui, SDL3 static) and MSVC cl on PATH")
        return 0
    imgui = tree / "_deps/imgui-src"
    with tempfile.TemporaryDirectory(prefix="mm3-settings-window-") as temporary:
        out = Path(temporary)
        sources = [str(ROOT / "tests/mm3_settings_window_contract.cpp")] + [str(imgui / name) for name in (
            "imgui.cpp", "imgui_draw.cpp", "imgui_tables.cpp", "imgui_widgets.cpp")]
        command = ["cl", "/nologo", "/EHsc", "/std:c++17", "/MD", "/D_CRT_SECURE_NO_WARNINGS", f"/Fo{out}{os.sep}", f"/Fe{out / 't.exe'}",
                   f"/I{imgui}", f"/I{imgui / 'backends'}", f"/I{tree / '_deps/sdl3-src/include'}", f"/I{ROOT / 'src'}",
                   f"/I{ROOT / 'src/mods'}", f"/I{ROOT / 'tools/xboxrecomp/src/video'}", f"/I{ROOT / 'tools/xboxrecomp/include'}",
                   f"/I{ROOT / 'tools/xboxrecomp/src'}"] + sources + ["/link", str(sdl_lib), "user32.lib", "gdi32.lib", "winmm.lib",
                   "imm32.lib", "ole32.lib", "oleaut32.lib", "version.lib", "setupapi.lib", "advapi32.lib", "shell32.lib", "cfgmgr32.lib"]
        subprocess.run(command, check=True, cwd=out, stdout=subprocess.DEVNULL)
        subprocess.run([str(out / "t.exe")], check=True, cwd=out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
