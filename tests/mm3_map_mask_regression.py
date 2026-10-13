"""Validate the square alpha mask against independently decoded retail bytes.

Mocks only the D3D11 allocation boundary. Run after scripts/enter-env.ps1.
"""
import ctypes
import _ctypes
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    if not shutil.which("cl"):
        raise SystemExit("Run scripts/enter-env.ps1 first.")
    renderer = (ROOT / "tools/xboxrecomp/src/video/nv2a_d3d11.c").read_text()
    production = re.search(r"^static ID3D11ShaderResourceView \*map_square_mask\(.*?^\}", renderer, re.M | re.S).group()
    assert "s_map_texture_query(st->tex[n].addr) == 1" in renderer
    assert "if (map_base_draw) view = map_square_mask(t);\n            if (!view) view = texture_get_with_levels(t, &levels);" in renderer
    candidates = (ROOT / "game_files/.mm3-car-mods/Data_dvd.zip", ROOT / "game_files/Data/Data_dvd.zip")
    archive = next(path for path in candidates if path.is_file())
    with zipfile.ZipFile(archive) as source:
        compressed = source.read("Menu/Gfx/HUD/MapMask.cdds")
    with tempfile.TemporaryDirectory(prefix="mm3-map-mask-") as temporary:
        folder = Path(temporary)
        decoder = ROOT / "tools/cache/decdds/decdds.c"
        if not decoder.is_file():
            raise SystemExit("Install the local decdds oracle in tools/cache/decdds first.")
        (folder / "oracle.c").write_text(
            '#include <stdint.h>\n#include <stdlib.h>\n'
            'int decdds_extract(const uint8_t *, uint32_t, uint8_t **, uint32_t *, int);\n'
            '__declspec(dllexport) int decode(const uint8_t *in, uint32_t n, uint8_t **out, uint32_t *len) { return decdds_extract(in,n,out,len,0); }\n'
            '__declspec(dllexport) void release(void *p) { free(p); }\n')
        subprocess.run(["cl", "/nologo", "/O2", "/LD", str(decoder), str(folder / "oracle.c"),
                        "/Fe:" + str(folder / "oracle.dll"), "/Fo:" + str(folder) + "\\"], check=True, cwd=folder)
        oracle = ctypes.CDLL(str(folder / "oracle.dll"))
        pointer, length = ctypes.c_void_p(), ctypes.c_uint32()
        oracle.decode.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_uint32)]
        oracle.release.argtypes = [ctypes.c_void_p]
        assert oracle.decode(compressed, len(compressed), ctypes.byref(pointer), ctypes.byref(length)) == 0
        decoded = ctypes.string_at(pointer, length.value); oracle.release(pointer)
        _ctypes.FreeLibrary(oracle._handle)
        del oracle
        assert decoded[:4] == b"DDS " and len(decoded) == 128 + 16384
        (folder / "native_mask.inc").write_text("static const unsigned char native_mask[16384] = {" +
                                              ",".join(str(byte) for byte in decoded[128:]) + "};\n")
        (folder / "production_mask.inc").write_text(production)
        shutil.copyfile(ROOT / "tools/xboxrecomp/src/video/nv2a_map_mask.h", folder / "nv2a_map_mask.h")
        executable = folder / "contract.exe"
        subprocess.run(["cl", "/nologo", "/W3", "/WX", "/O2", "/std:c11", "/I" + str(folder),
                        str(ROOT / "tests/mm3_map_mask_contract.c"), "/Fe:" + str(executable),
                        "/Fo:" + str(folder / "contract.obj")], check=True, cwd=folder)
        for mode in ("normal", "allocation", "view"):
            subprocess.run([str(executable), mode], check=True, cwd=folder)


if __name__ == "__main__":
    main()
