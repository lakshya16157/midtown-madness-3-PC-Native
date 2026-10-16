"""Run isolated distance contracts with actual recovered PAL setters.

Only scratch executables are created. No running game, save or user config is
opened. Run with the repository Python after scripts/enter-env.ps1.
"""
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile

import mm3_x87_exp_regression as common

ROOT = Path(__file__).resolve().parents[1]


def generated(name):
    for chunk in (ROOT / 'src/recomp/gen').glob('*.c'):
        match = re.search(r'void ' + name + r'\(void\)\n\{.*?\n\}', chunk.read_text(), re.S)
        if match:
            return match.group()
    raise AssertionError(name)


def originals(loader):
    wanted = {'sub_000E2D5B', 'sub_000D9E2E', 'sub_000FE71E', 'sub_001661D0'}
    bodies = {}
    for chunk in ('recomp_0010.c', 'recomp_0011.c', 'recomp_0014.c'):
        text = (ROOT / 'src/recomp/gen' / chunk).read_text()
        for name in wanted - bodies.keys():
            match = re.search(r'void ' + name + r'\(void\)\n\{.*?\n\}', text, re.S)
            if match:
                body = match.group().replace(name + '(void)', name + '_original(void)')
                body = re.sub(r'\b(eax|ecx|edx|esp|esi|edi|ebx)\b', r'g_\1', body)
                bodies[name] = body
    assert bodies.keys() == wanted
    image = common.Image(str(ROOT / 'game_files/default.xbe'))
    constants = []
    for address in (0x384EF4, 0x384EF8, 0x384EFC, 0x384D08, 0x385088,
                    0x3860E0, *range(0x39ADC0, 0x39ADD4, 4)):
        value = None
        for base, offset, size, _ in image.secs:
            if base <= address <= base + size - 4:
                value = struct.unpack_from('<I', image.d, offset + address - base)[0]
                break
        assert value is not None, hex(address)
        constants.append(f'MEM32(0x{address:X}u)=0x{value:X}u;')
    # Run the actual PAL clamp and argument handover, with only camera matrix
    # construction at the existing observation boundary. This includes the
    # production seam, not a test reimplementation of its comparison/math.
    head = loader[:loader.index('loc_00224A07:')]
    branch = loader[loader.index('loc_00224C24:'):loader.index('loc_00224C73:')]
    branch = re.sub(r'RECOMP_ABI_CALL\(0x[0-9A-F]+u, (sub_[0-9A-F]+)\)', r'\1()', branch)
    fragment = head.replace('sub_00224A07(void)', 'contract_native_city_limit(void)') + branch + '''
loc_00224C73: ;
    return;
    #undef fp_push
    #undef fp_pop
    #undef fp_top
    #undef fp_st
    #undef fp_st1
}
'''
    fragment = re.sub(r'\b(eax|ecx|edx|esp|esi|edi|ebx)\b', r'g_\1', fragment)
    return '\n'.join(bodies.values()) + '\n' + fragment + '\nstatic void load_original_constants(void){' + ''.join(constants) + '}\n'


def main():
    if not shutil.which('cl') or not shutil.which('cmake'):
        raise SystemExit('Run scripts/enter-env.ps1 first.')
    cases = [('-', '-', '2', '2'), ('1', '-', '1', '1'), ('4', '-', '4', '4'),
             ('1', '2', '1', '2'), ('4', '1', '4', '1'), ('2', '4', '2', '4'),
             ('1.25', '1.5', '1.25', '1.5'), ('3', '2.5', '3', '2.5'),
             ('bad', '1', '2', '1'), ('nan', '-', '2', '2'),
             ('2', 'nan', '2', '2'), ('4', '0', '4', '4'),
             ('1', '5', '1', '1'), ('1.5', 'bad', '1.5', '1.5')]
    with tempfile.TemporaryDirectory(prefix='mm3-distance-') as temporary:
        folder = Path(temporary)
        source_file, transformed_file = folder / 'loader.c', folder / 'loader_after.c'
        cmake_script = folder / 'transform.cmake'
        cmake_script.write_text(
            f'include("{(ROOT / "scripts/mm3_city_streaming_seam.cmake").as_posix()}")\n'
            'file(READ "${INPUT}" content)\n'
            'mm3_add_city_streaming_limit(content)\n'
            'file(WRITE "${OUTPUT}" "${content}")\n')

        def transform(source, reject=False):
            source_file.write_bytes(source.encode())
            result = subprocess.run(['cmake', f'-DINPUT={source_file}', f'-DOUTPUT={transformed_file}',
                                     '-P', str(cmake_script)], capture_output=True, text=True)
            if reject:
                assert result.returncode and 'MM3 city streaming limit' in result.stderr
                return None
            assert not result.returncode, result.stderr
            return transformed_file.read_text()

        loader = generated('sub_00224A07')
        modified = transform(loader)
        assert transform(loader.replace('\n', '\r\n')) == modified
        transform(loader.replace('MEMF(eax + 0x1505C)', 'MEMF(eax + 0x15060)'), reject=True)
        transform(loader + '\nloc_00224C24: ;\n', reject=True)
        transform(modified, reject=True)
        assert modified.count('mm3_city_streaming_limit(eax);') == 1
        (folder / 'distance_original_setters.inc').write_text(originals(modified))
        executable = folder / 'contract.exe'
        def compile_contract(target, extra=()):
            subprocess.run(['cl', '/nologo', '/W3', '/wd4102', '/Od', '/std:c11',
                            '/D_CRT_SECURE_NO_WARNINGS', *extra, '/I' + str(folder),
                            '/I' + str(ROOT / 'src'), '/I' + str(ROOT / 'src/recomp/gen'),
                            '/I' + str(ROOT / 'tools/xboxrecomp/include'),
                            str(ROOT / 'tests/mm3_render_distance_contract.c'),
                            '/Fo:' + str(target.with_suffix('.obj')), '/Fe:' + str(target)],
                           check=True, cwd=folder)
        compile_contract(executable)
        for case in cases:
            for mode in ('custom-first', 'invalid-first'):
                result = subprocess.run([str(executable), *case, mode], capture_output=True, text=True)
                if result.returncode:
                    raise AssertionError((case, mode, result.stdout, result.stderr))
        # Retain the former custom-setter initialization omission in a scratch
        # hook. The same real-setter contract must catch its double scale.
        source = (ROOT / 'src/mm3_render_distance.c').read_text()
        marker = 'Those globals are already effective and must not be scaled again. */\n        s_pedestrian_distances_initialized = 1;'
        assert source.count(marker) == 1
        (folder / 'distance_order_before.c').write_text(source.replace(marker, marker.rsplit('\n', 1)[0]))
        before = folder / 'before.exe'
        compile_contract(before, ('/DMM3_DISTANCE_SOURCE="distance_order_before.c"',))
        result = subprocess.run([str(before), '2', '2', '2', '2', 'custom-first'],
                                capture_output=True, text=True, timeout=15)
        assert result.returncode and 'base[i] * scale' in result.stderr, (result.returncode, result.stderr)
        # Restore the old cap scaling only in scratch. The real loader fragment
        # must reject its re-expansion beyond the resident neighborhood.
        marker = '    /* 0x224C4E already supplies the native resident-cell cap'
        assert source.count(marker) == 1
        old_cap = '''    if (caller == 0x00224C4Eu) {
        MEMF(g_esp + 4) *= mm3_view_distance_scale();
        MEMF(g_ebp + 8) = MEMF(g_esp + 4);
    }
'''
        (folder / 'distance_cap_before.c').write_text(source.replace(marker, old_cap + marker))
        before = folder / 'cap_before.exe'
        compile_contract(before, ('/DMM3_DISTANCE_SOURCE="distance_cap_before.c"',))
        result = subprocess.run([str(before), '2', '2', '2', '2', 'invalid-first'],
                                capture_output=True, text=True, timeout=15)
        assert result.returncode and 'wanted' in result.stderr, (result.returncode, result.stderr)
    print(f'PASS: {len(cases) * 2} distance cases using actual PAL setters and loader clamp; resident-city cap, effective haze, Lua/FOV/temporary ranges, native fades and ordering; both former regressions rejected.')


if __name__ == '__main__':
    main()
