"""Compile the production embedded-menu hooks without archives or saved profiles."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MOCK_RESOURCES = """#ifndef MM3_MENU_RESOURCES_H
#define MM3_MENU_RESOURCES_H
#include <stdint.h>
typedef struct MM3MenuResource {
    const char *name;
    const unsigned char *data;
    uint32_t size;
} MM3MenuResource;
static const unsigned char main_menu[] = {
    0x00, 0x47, 0xF0, 0x0D, 0xFF, 0x80, 0x41, 0x00, 0x7E
};
static const unsigned char graphics_settings[] = { 0x53, 0x45, 0x54, 0x00, 0x10 };
static const MM3MenuResource mm3_menu_resources[] = {
    { "MainMenu.men", main_menu, sizeof main_menu },
    { "GraphicsSettings.men", graphics_settings, sizeof graphics_settings }
};
#define MM3_MENU_RESOURCE_COUNT 2u
#endif
"""


def main():
    if not shutil.which("cl"):
        raise SystemExit("Run scripts/enter-env.ps1 first so cl.exe is available.")
    with tempfile.TemporaryDirectory(prefix="mm3-menu-resources-") as temporary:
        folder = Path(temporary)
        shutil.copyfile(ROOT / "src/mm3_recomp_types.h", folder / "recomp_types.h")
        (folder / "mm3_menu_resources.h").write_text(MOCK_RESOURCES, encoding="utf-8")
        executable = folder / "contract.exe"
        subprocess.run([
            "cl", "/nologo", "/W4", "/WX", "/Od", "/std:c11",
            "/D_CRT_SECURE_NO_WARNINGS", "/I" + str(folder),
            "/I" + str(ROOT / "tools/xboxrecomp/include"),
            str(ROOT / "tests/mm3_menu_resources_contract.c"),
            "/Fo:" + str(folder / "contract.obj"), "/Fe:" + str(executable),
        ], check=True, cwd=folder)
        subprocess.run([str(executable)], check=True, cwd=folder, timeout=20)


if __name__ == "__main__":
    main()
