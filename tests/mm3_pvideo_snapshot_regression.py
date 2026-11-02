"""Build real PVIDEO snapshot/overlay code; optional --benchmark uses no GPU."""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    if not shutil.which('cl'):
        raise SystemExit('Run scripts/enter-env.ps1 first.')
    core = (ROOT / 'tools/xboxrecomp/src/nv2a/nv2a_core.c').read_text()
    bodies = []
    for result, name in [('uint64_t', 'pvideo_read'), ('void', 'pvideo_write')]:
        match = re.search(result + r' ' + name + r'\([^)]*\)\n\{.*?\n\}', core, re.S)
        assert match, name
        bodies.append(match.group())
    with tempfile.TemporaryDirectory(prefix='mm3-pvideo-') as temporary:
        folder = Path(temporary)
        (folder / 'pvideo_core.inc').write_text('\n'.join(bodies))
        # fb_present.c's two outside dependencies: installed RAM bounds the
        # scanout check (retail 64 MiB) and the window lives in fb_window_*.c.
        (folder / 'host_stubs.c').write_text(
            '#include <stddef.h>\n'
            'size_t g_xbox_total_ram = 64u * 1024u * 1024u;\n'
            'void *fbw_native_window(void) { return NULL; }\n')
        executable = folder / 'pvideo.exe'
        subprocess.run(['cl', '/nologo', '/O2', '/Ob1', '/MD', '/W3', '/std:c11',
            '/D_CRT_SECURE_NO_WARNINGS', '/I' + str(folder),
            '/I' + str(ROOT / 'tools/xboxrecomp/src'),
            str(ROOT / 'tools/xboxrecomp/tests/nv2a_pvideo_snapshot/test_main.c'),
            str(folder / 'host_stubs.c'),
            '/Fo:' + str(folder) + '\\', '/Fe:' + str(executable),
            '/link', 'user32.lib', 'gdi32.lib'], cwd=folder, check=True)
        subprocess.run([str(executable)] + (['benchmark'] if '--benchmark' in sys.argv else []), cwd=folder, check=True)


if __name__ == '__main__':
    main()
