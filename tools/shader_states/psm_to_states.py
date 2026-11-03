"""Merge renderer pixel-shader manifests (.psm) into the states list the build
prebuilds specialised shaders for (src/shaders/mm3_ps_states.txt).

The renderer records every register-combiner configuration it specialises in
%LOCALAPPDATA%/xboxrecomp/ShaderCache/<family>.psm. Wine/Proton (the Steam
Deck) cannot compile those variants, so the build compiles the listed ones
with Microsoft's compiler (xboxrecomp tools/shader_prebuild); everything else
is drawn by the slower ubershader there.

usage: psm_to_states.py OUTPUT FILE.psm [FILE.psm ...]
"""
import struct
import sys

HEADER = 88
WORDS = 54


def records(path):
    data = open(path, 'rb').read()
    if data[:8] != b'NVPSMAN1' or struct.unpack_from('<I', data, 12)[0] != WORDS * 4:
        raise SystemExit(f'{path}: not a version-1 manifest')
    count = struct.unpack_from('<I', data, 16)[0]
    for i in range(count):
        yield struct.unpack_from(f'<{WORDS}I', data, HEADER + i * WORDS * 4)


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    seen = {}
    for path in sys.argv[2:]:
        for record in records(path):
            # variant (word 41) selects flat shading / w-buffer; 0..3 only.
            if record[41] <= 3:
                seen.setdefault(record, None)
    with open(sys.argv[1], 'w', newline='\n') as out:
        out.write('# MM3 pixel-shader configurations prebuilt for Wine/Proton\n')
        out.write('# (tools/shader_states/psm_to_states.py). One PsKey a line, 54 hex words.\n')
        for record in seen:
            out.write(' '.join(f'{w:x}' for w in record) + '\n')
    print(f'{len(seen)} configurations -> {sys.argv[1]}')


if __name__ == '__main__':
    main()
