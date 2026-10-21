"""Exercise the mission zone observer and its exact generated-source seam."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CALL = '    mm3_zone_status(ebx, ebp + -132);\n'


def seam(folder):
    body = None
    for path in (ROOT / 'src/recomp/gen').glob('recomp_*.c'):
        match = re.search(r'void sub_0020F1AB\(void\)\n\{.*?\n\}', path.read_text(), re.S)
        if match:
            assert body is None
            body = match.group()
    assert body, 'sub_0020F1AB is not in the generated sources'
    script = folder / 'seam.cmake'
    script.write_text(f'''include("{(ROOT / 'scripts/mm3_zone_diagnostics_seam.cmake').as_posix()}")
file(READ "${{INPUT}}" source)
mm3_add_zone_diagnostics(source)
file(WRITE "${{OUTPUT}}" "${{source}}")
''')
    inp, out = folder / 'input.c', folder / 'output.c'

    def apply(source, success=True):
        inp.write_bytes(source)
        result = subprocess.run(['cmake', '-DINPUT=' + str(inp), '-DOUTPUT=' + str(out), '-P', str(script)],
                                capture_output=True, text=True)
        assert (result.returncode == 0) == success, result.stderr
        return out.read_bytes().decode() if success else ''

    for ending in ('\n', '\r\n'):
        fixed = apply(body.replace('\n', ending).encode()).replace('\r\n', '\n')
        assert fixed.count('#include "mm3_zone_diagnostics.h"') == 1
        assert fixed.count('mm3_zone_status') == 1
        assert 'loc_0020F256: ;\n' + CALL + '    _fa = (uint32_t)(MEM8(ebx + 0x14A))' in fixed
        stripped = fixed.replace('#include "mm3_zone_diagnostics.h"\n', '', 1).replace(CALL, '', 1)
        assert stripped == body
    apply((body + '\n' + body).encode(), False)
    apply(body.replace('loc_0020F256: ;', 'loc_0020F256: ; /* changed */').encode(), False)
    apply(apply(body.encode()).encode(), False)
    print('PASS: actual generated sub_0020F1AB seam, LF/CRLF, unchanged guest instructions, '
          'strict duplicate/changed/already-patched anchors')


def main():
    if not shutil.which('cl') or not shutil.which('cmake'):
        raise SystemExit('Run scripts/enter-env.ps1 first so cl.exe and cmake are available.')
    with tempfile.TemporaryDirectory(prefix='mm3-zone-diagnostics-') as temporary:
        folder = Path(temporary)
        seam(folder)
        executable = folder / 'contract.exe'
        for optimization in ('/Od', '/O2'):
            subprocess.run(['cl', '/nologo', '/W4', '/WX', optimization, '/std:c11', '/D_CRT_SECURE_NO_WARNINGS',
                            '/I' + str(ROOT / 'src'), '/I' + str(ROOT / 'tools/xboxrecomp/include'),
                            str(ROOT / 'tests/mm3_zone_diagnostics_contract.c'),
                            '/Fo:' + str(folder / 'contract.obj'), '/Fe:' + str(executable)],
                           check=True, cwd=folder)
            subprocess.run([str(executable)], check=True)


if __name__ == '__main__':
    main()
