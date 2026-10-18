"""Compile and run the actual offline-readiness wrapper with a guest ABI spy."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    if not shutil.which('cl'):
        raise SystemExit('Run scripts/enter-env.ps1 first so cl.exe is available.')
    with tempfile.TemporaryDirectory(prefix='mm3-offline-contract-') as temporary:
        out = Path(temporary)
        exe = out / 'offline-contract.exe'
        subprocess.run(['cl', '/nologo', '/W4', '/WX', '/O2', '/std:c11', '/utf-8',
                        '/D_CRT_SECURE_NO_WARNINGS', '/I' + str(ROOT / 'src/recomp/gen'),
                        '/I' + str(ROOT / 'tools/xboxrecomp/include'),
                        str(ROOT / 'tests/mm3_offline_compat_contract.c'),
                        '/Fe:' + str(exe)], check=True, cwd=out)
        subprocess.run([str(exe)], check=True, cwd=out)


if __name__ == '__main__':
    main()
