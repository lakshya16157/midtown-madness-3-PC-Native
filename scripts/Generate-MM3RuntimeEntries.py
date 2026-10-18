"""Lift explicit runtime entries using MM3's pinned translator.

The normal invocation regenerates every manifest entry.  For a narrow repair,
use ``OUT --incremental --entries 0xADDRESS ...``: existing translated bodies
and lookup cases are retained byte for byte, and only missing selected bodies
are lifted from the XBE.  Both paths apply the same project-owned x87 repairs.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import sys
import tempfile

REPO = Path(__file__).resolve().parents[1]
LOOKUP_TYPE = 'typedef void (*mm3_recovered_func_t)(void);'
LOOKUP_DEFAULT = '    default: return (mm3_recovered_func_t)0;'


def address(value):
    result = int(value, 16) if isinstance(value, str) else int(value)
    if not 0 < result <= 0xFFFFFFFF:
        raise ValueError(f'Invalid entry address: {value}')
    return result


def validate_entries(entries):
    """Reject ambiguous metadata before writing or invoking the translator."""
    seen = set()
    for entry in entries:
        start, end = address(entry['start']), address(entry['end'])
        if start in seen:
            raise ValueError(f'Duplicate manifest entry: 0x{start:08X}')
        if end <= start:
            raise ValueError(f'Empty/reversed entry: 0x{start:08X}')
        seen.add(start)
    return entries


def select_entries(entries, requested):
    if not requested:
        return entries
    wanted = [address(item) for item in requested]
    if len(wanted) != len(set(wanted)):
        raise ValueError('Duplicate --entries address')
    available = {address(entry['start']) for entry in entries}
    unknown = set(wanted) - available
    if unknown:
        raise ValueError('Entries absent from manifest: ' +
                         ', '.join(f'0x{item:08X}' for item in sorted(unknown)))
    # Manifest order makes output independent of command-line address order.
    return [entry for entry in entries if address(entry['start']) in wanted]


def overlay_function_metadata(functions, entries):
    selected = {address(entry['start']) for entry in entries}
    result = [item for item in functions if address(item['start']) not in selected]
    for entry in entries:
        start, end = address(entry['start']), address(entry['end'])
        result.append({'start': f'0x{start:08X}', 'end': f'0x{end:08X}',
                       'size': end - start, 'name': f'sub_{start:08X}',
                       'section': '.text', 'confidence': 1.0,
                       'detection_method': 'runtime_indirect_call',
                       'has_prologue': start == 0x104DD3,
                       'calls_to': [], 'called_by': [], 'num_instructions': 200})
    return result


def has_body(source, start):
    name = f'sub_{start:08X}'
    matches = re.findall(r'^void ' + name + r'\(void\)\s*\{', source or '', re.MULTILINE)
    if len(matches) > 1:
        raise ValueError(f'Duplicate generated body: {name}')
    return bool(matches)


def repair_runtime_body(body, entry):
    start = address(entry['start'])
    if not has_body(body, start):
        raise RuntimeError(f'Generation failed: 0x{start:08X}')
    for operand in entry.get('x87_extended_loads', []):
        # The pinned lifter otherwise treats an 80-bit constant as a float.
        operand_address = f'0x{address(operand):X}'
        old = f'fp_push(MEMF({operand_address})); /* fld */'
        new = f'fp_push(MEMFP80({operand_address})); /* fld tbyte */'
        if old not in body and new not in body:
            raise RuntimeError(f'Missing extended load {operand}: {entry["start"]}')
        body = body.replace(old, new)
    if entry.get('x87_helper_unwinds_caller'):
        # 9BC04 can discard 9BC52's return address and return from its caller.
        lines = body.splitlines()
        found = False
        for index, line in enumerate(lines):
            if 'RECOMP_ABI_CALL(0x0009BC52u, sub_0009BC52)' in line:
                lines[index] = ('    { uint32_t math_sp = esp;\n' + line +
                                '\n      if (esp == math_sp + 4u) return; }')
                found = True
        if not found:
            raise RuntimeError(f'Missing unwind helper: {entry["start"]}')
        body = '\n'.join(lines)
    return body


def render_full(entries, bodies):
    code = ['#define RECOMP_GENERATED_CODE', '#include "recomp_funcs.h"', '#include <math.h>']
    for entry in entries:
        code.append(f'void sub_{address(entry["start"]):08X}(void);')
    code.extend(bodies[address(entry['start'])] for entry in entries)
    code += [LOOKUP_TYPE, 'mm3_recovered_func_t mm3_lookup_recovered_entry(uint32_t va)',
             '{', '    switch (va) {']
    for entry in entries:
        start = address(entry['start'])
        code.append(f'    case 0x{start:08X}u: return sub_{start:08X};')
    code += [LOOKUP_DEFAULT, '    }', '}']
    return '\n'.join(code)


def render_incremental(source, entries, bodies):
    """Insert additions at validated anchors; never rewrite existing bodies."""
    if source.count(LOOKUP_TYPE) != 1:
        raise ValueError('Expected one recovered-entry lookup typedef')
    before, after = source.split(LOOKUP_TYPE)
    if after.count(LOOKUP_DEFAULT) != 1 or 'mm3_lookup_recovered_entry(uint32_t va)' not in after:
        raise ValueError('Expected one recovered-entry lookup switch/default')
    newline = '\r\n' if '\r\n' in source else '\n'
    additions, cases = [], []
    missing = [entry for entry in entries if not has_body(source, address(entry['start']))]
    for entry in missing:
        start = address(entry['start'])
        if start not in bodies:
            raise ValueError(f'Missing translation: 0x{start:08X}')
        additions.append(f'void sub_{start:08X}(void);')
    for entry in missing:
        additions.append(bodies[address(entry['start'])].replace('\r\n', '\n').rstrip())
    for entry in entries:
        start = address(entry['start'])
        matches = re.findall(r'^\s*case 0x' + f'{start:08X}' +
                             r'u: return (sub_[0-9A-F]+);', after, re.MULTILINE)
        if len(matches) > 1 or (matches and matches[0] != f'sub_{start:08X}'):
            raise ValueError(f'Ambiguous lookup case: 0x{start:08X}')
        if not matches:
            cases.append(f'    case 0x{start:08X}u: return sub_{start:08X};')
    if additions:
        before += '\n'.join(additions).replace('\n', newline)
        before += newline + newline
    if cases:
        after = after.replace(LOOKUP_DEFAULT, newline.join(cases) + newline + LOOKUP_DEFAULT, 1)
    return before + LOOKUP_TYPE + after


def resolve_toolkit(repo):
    if os.environ.get('MM3_GENERATOR_DIR'):
        return Path(os.environ['MM3_GENERATOR_DIR']).resolve()
    # Share the full recipe's pin instead of silently using the runtime lifter.
    spec = importlib.util.spec_from_file_location('mm3_generator_pin', repo / 'scripts/generate_mm3.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.generator_dir()


def translate_bodies(repo, out, entries, functions):
    if not entries:
        return {}
    toolkit = resolve_toolkit(repo)
    sys.path.insert(0, str(toolkit))
    from tools.recomp import config
    from tools.recomp.translator import BatchTranslator
    xbe = repo / 'game_files/default.xbe'
    config.configure_from_xbe(str(xbe))
    tool = toolkit / 'tools'
    # Keep the existing outputs untouched if any selected translation fails.
    with tempfile.TemporaryDirectory(prefix='mm3-runtime-lift-', dir=out) as tmp:
        metadata = Path(tmp) / 'functions.json'
        metadata.write_text(json.dumps(functions, indent=1), encoding='utf-8')
        translator = BatchTranslator(
            xbe_path=str(xbe), func_json_path=str(metadata), output_dir=tmp,
            labels_json_path=str(tool / 'disasm/output/labels.json'),
            identified_json_path=str(tool / 'func_id/output/identified_functions.json'),
            abi_json_path=str(tool / 'abi_analysis/output/abi_functions.json'),
            seh_prolog=0x00097AA4)
        return {address(entry['start']): repair_runtime_body(
            translator.translate_single(address(entry['start'])), entry) for entry in entries}


def atomic_write(path, data):
    # Keep incremental build dependencies stable when no repair is needed.
    try:
        if path.read_bytes() == data:
            return
    except FileNotFoundError:
        pass
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(prefix=path.name + '.', suffix='.tmp',
                                         dir=path.parent, delete=False) as output:
            temporary = Path(output.name)
            output.write(data)
        os.replace(temporary, path)
    finally:
        if temporary and temporary.exists():
            temporary.unlink()


def main(argv=None, repo=None):
    repo = Path(repo) if repo else REPO
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('out', type=Path)
    parser.add_argument('--incremental', action='store_true')
    parser.add_argument('--entries', nargs='+', metavar='VA')
    parser.add_argument('--manifest', type=Path, default=repo / 'mm3_runtime_entry_repairs.json')
    args = parser.parse_args(argv)
    if args.entries and not args.incremental:
        parser.error('--entries requires --incremental (full mode always regenerates the manifest)')
    if args.incremental and not args.entries:
        parser.error('--incremental requires an explicit --entries selection')
    entries = validate_entries(json.loads(args.manifest.read_text(encoding='utf-8')))
    selected = select_entries(entries, args.entries)
    out = args.out.resolve()
    metadata = out / 'runtime_entry_functions.json'
    source_path = out / 'recomp_runtime_entries.c'
    functions = json.loads(metadata.read_text(encoding='utf-8'))
    functions = overlay_function_metadata(functions, selected)
    source = source_path.read_bytes().decode('utf-8') if args.incremental else None
    if source is not None:
        # Validate anchors/cases before doing expensive translation work.
        placeholders = {address(entry['start']): '' for entry in selected}
        render_incremental(source, selected, placeholders)
    to_lift = [entry for entry in selected if source is None or
               not has_body(source, address(entry['start']))]
    bodies = translate_bodies(repo, out, to_lift, functions)
    result = render_incremental(source, selected, bodies) if args.incremental else render_full(selected, bodies)
    # The existing source bytes survive an idempotent incremental invocation.
    atomic_write(source_path, result.encode('utf-8'))
    atomic_write(metadata, json.dumps(functions, indent=1).encode('utf-8'))
    print(f'Runtime entries: lifted {len(to_lift)}, selected {len(selected)} ({"incremental" if args.incremental else "full"})')


if __name__ == '__main__':
    main()
