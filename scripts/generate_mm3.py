"""Regenerate MM3's translated C from game_files/default.xbe, natively.

    python scripts/generate_mm3.py [--out src/recomp/gen] [--skip-analysis]

Runs the whole recipe the build depends on, in order:
  1. analysis (disasm with mm3_runtime_function_seeds.json, func_id, abi)
  2. the batch: every function, minus recomp_manual.c's hand-written bodies,
     with the native tail constructor entries
  3. overlays the project wraps: memmove tails, alternate-SEH and ABI bodies
  4. runtime-proven entries (mm3_runtime_entry_repairs.json)
Tested with Capstone 5.0.7; scripts/setup.py installs that disassembler version.
"""
import argparse
import bisect
import hashlib
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

import os

REPO = Path(__file__).resolve().parents[1]


def generator_dir():
    """The runtime's own tools generate the code. MM3_GENERATOR_DIR points at
    another checkout for comparisons."""
    if os.environ.get('MM3_GENERATOR_DIR'):
        return Path(os.environ['MM3_GENERATOR_DIR']).resolve()
    return REPO / 'tools/xboxrecomp'


def generation_key():
    toolkit_tree = subprocess.check_output(
        ['git', '-C', str(generator_dir()), 'rev-parse', 'HEAD:tools', 'HEAD:templates/runtime'])
    digest = hashlib.sha256(toolkit_tree)
    for path in ('scripts/generate_mm3.py', 'scripts/Generate-MM3RuntimeEntries.py',
                 'scripts/mm3_runtime_unwind.py', 'tools/prepare_mm3_gui_boundaries.py',
                 'mm3_runtime_entry_repairs.json', 'mm3_runtime_function_seeds.json',
                 'mm3_pointer_function_entries.json',
                 'mm3_tail_constructor_functions.json', 'src/recomp_manual.c',
                 'src/mm3_recomp_types.h', 'game_files/default.xbe'):
        digest.update((REPO / path).read_bytes())
    # shortcut: committed toolkit trees only; use --force for local toolkit edits.
    return digest.hexdigest() + '\n'


XBE = REPO / 'game_files/default.xbe'
TOOLKIT = None  # set by setup_toolkit(); importing this module has no side effects
DISASM = None


def setup_toolkit():
    global TOOLKIT, DISASM
    TOOLKIT = generator_dir()
    os.environ['MM3_GENERATOR_DIR'] = str(TOOLKIT)  # for the helper scripts
    DISASM = TOOLKIT / 'tools/disasm/output'
    sys.path.insert(0, str(TOOLKIT))

# Computed reverse-copy entries share 0x93860's frame and end at its epilogue.
MEMMOVE_TAILS = (0x93B04, 0x93B58, 0x93B60, 0x93B70, 0x93B84)
MEMMOVE_END = 0x93B9D
# Remainder entries inside the 0x93B04 tail: reverse_memmove's jump table and
# the tails above reach them directly, so each is a resume entry of that tail.
MEMMOVE_RESUME = (0x93B0C, 0x93B14, 0x93B1C, 0x93B24, 0x93B2C, 0x93B3F)
# Bodies recomp_manual.c wraps (sub_X calls sub_X_gen): CRT functions on the
# alternate SEH prolog and FPO helpers whose callee-save contract is enforced.
SEH_OVERLAYS = (0x83FBB, 0x84020, 0x8427E, 0x842EA, 0x854CF, 0x858F3, 0x860AA,
                0x8629E, 0x96738, 0x97AFC, 0x9E85A)
ABI_OVERLAYS = (0x93C45, 0x1E7B8F, 0x2539A, 0x943ED, 0x9418C)
PRELUDE = '#define RECOMP_GENERATED_CODE\n{}#include "recomp_funcs.h"\n#include <math.h>\n\n'


def py(*args, **kw):
    return subprocess.run([sys.executable, *map(str, args)], cwd=TOOLKIT, check=True, **kw)


def write_tail_functions(path):
    functions = json.loads((DISASM / 'functions.json').read_text())
    by_start = {int(f['start'], 16): f for f in functions}
    for address in MEMMOVE_TAILS:
        f = by_start.get(address)
        if f is None:
            f = {'start': f'0x{address:08X}', 'name': f'sub_{address:08X}', 'section': '.text',
                 'confidence': 0.75, 'detection_method': 'vtable_thunk', 'num_instructions': 60,
                 'calls_to': [], 'called_by': []}
            functions.append(f)
        f.update(end=f'0x{MEMMOVE_END:08X}', size=MEMMOVE_END - address, has_prologue=False)
    path.write_text(json.dumps(functions, indent=1))


def undefined_entry_calls(out):
    """Functions recomp_runtime_entries.c calls that no generated file defines."""
    defined = set()
    for path in out.glob('*.c'):
        defined.update(re.findall(r'^void (sub_[0-9A-F]{8})\(void\)(?!;)', path.read_text(), re.M))
    called = set(re.findall(r'\b(sub_[0-9A-F]{8})\b', (out / 'recomp_runtime_entries.c').read_text()))
    return {int(name[4:], 16) for name in called - defined}


def runtime_entries(extra=()):
    """The repair manifest plus the pointer-only entries. The detector folds code the
    title reaches only through a data pointer (vtable, callback) into the body before
    it; seeding a split there loses the flags across the cut, so each is lifted as its
    own entry over the rest of that body, which stays whole."""
    entries = json.loads((REPO / 'mm3_runtime_entry_repairs.json').read_text())
    functions = sorted((int(f['start'], 16), int(f['end'], 16))
                       for f in json.loads((DISASM / 'functions.json').read_text()))
    starts = [start for start, _ in functions]
    known = set(starts) | {int(e['start'], 16) for e in entries}
    pointers = [int(item['start'], 16)
                for item in json.loads((REPO / 'mm3_pointer_function_entries.json').read_text())]
    for start in pointers + sorted(extra):
        if start in known:
            continue
        i = bisect.bisect_right(starts, start) - 1
        if i >= 0 and start < functions[i][1]:
            end = functions[i][1]
        elif i + 1 < len(starts):
            end = starts[i + 1]
        else:
            continue
        entries.append({'start': f'0x{start:08X}', 'end': f'0x{end:08X}', 'optional': True})
        known.add(start)
    return entries


def write_runtime_entry_functions(path, entries):
    functions = [f for f in json.loads((DISASM / 'functions.json').read_text())]
    starts = {e['start'] for e in entries}
    functions = [f for f in functions if f['start'] not in starts]
    for e in entries:
        start, end = int(e['start'], 16), int(e['end'], 16)
        functions.append({'start': e['start'], 'end': e['end'], 'size': end - start,
                          'name': f'sub_{start:08X}', 'section': '.text', 'confidence': 1.0,
                          'detection_method': 'runtime_indirect_call',
                          'has_prologue': start == 0x104DD3, 'calls_to': [], 'called_by': [],
                          'num_instructions': 200})
    path.write_text(json.dumps(functions, indent=1))


# Statements that leave the flags as they were, so a walk back through them is exact.
FLAG_NEUTRAL = re.compile(
    r'^\s*(?:$|/\*.*\*/$|\(void\)|_(?:cf|zf) = [^;]*;(?: /\*.*\*/)?$'
    r'|if \(.*\) (?:goto loc_[0-9A-F]+;|\{ .*return; \}|\(void\)0; /\*.*\*/)(?: /\*.*\*/)?$)')
FLAG_LABEL = re.compile(r'^(loc_[0-9A-F]+): ;$')
FLAG_JUMP_OUT = re.compile(r'^\s*(goto |return\b|esp \+= \d+; return)')
FLAG_FUNC = re.compile(r'^\w[^;]*\b(sub_[0-9A-F]+)\(void\)\s*$')


def func_name(lines, i):
    """The sub_XXXX whose definition is the nearest one above line i."""
    return next((lines[k].split('(')[0].split()[-1] for k in range(i, -1, -1)
                 if FLAG_FUNC.match(lines[k])), '?')


def flag_setter_zf(text):
    """The C statement that sets _zf from a flag-producing line, or None.

    Each form matches the lifter's own je/jne condition for that setter: cmp and
    test keep their operands in _fa/_fb, and the result-based setters keep the
    result in _fa (see _make_condition in tools/recomp/lifter.py).
    """
    if '/* cmp ' in text:
        return '_zf = (_fa == _fb);'
    if '/* test ' in text:
        return '_zf = ((_fa & _fb) == 0);'
    if re.search(r'/\* (\w+ )?(sub|dec|inc|add|and|or|xor|neg) result', text):
        return '_zf = (_fa == 0);'
    return None


def fix_flag_joins(out, verbose=False):
    """Materialize ZF where a je/jne reads the never-set `_flags` fallback.

    The pinned lifter keeps flags lazily: a cmp leaves its operands in _fa/_fb, an
    arithmetic op leaves its result in _fa, a test leaves both operands. When a
    je/jne has no flag state it falls back to `_flags`, which nothing ever assigns,
    so the branch is never taken. Lua's code generator hit this at 0x89689
    (`x ~= y` compiled as `x ~= nil`), which kept System Link hosts from spawning a car.

    The fix walks back from each such branch through the statements that leave the
    flags alone (labels, conditional jumps, _cf writes) to every flag producer that
    reaches it, across labels and their gotos. When all of them are one of the
    forms above, each producer stores ZF in a per-function _zf and the branch tests
    that. Anything else is left as it was and listed, with the line that blocked it,
    so a silent miscompile is visible in the generator output.

    Returns (fixed, unfixed): the branch count rewritten, and a list of
    (file, function, label, line, reason) for every je/jne still on `_flags`.
    """
    jcc = re.compile(r'^(\s*)if \(_flags /\* (je|jne): [^*]*\*/\) (.*)$')
    fixed = 0
    unfixed = []
    for path in sorted(out.glob('recomp_*.c')):
        lines = path.read_text().splitlines()
        targets = {}
        for i, line in enumerate(lines):
            for m in re.finditer(r'goto (loc_[0-9A-F]+);', line):
                targets.setdefault(m.group(1), []).append(i)
        blocked = []

        def sources(j, seen=frozenset()):
            """Lines that set the flags in effect after line j, or None when one is unknown."""
            while j >= 0 and FLAG_NEUTRAL.match(lines[j]):
                j -= 1
            if j < 0:
                blocked.append(None)  # function entry: flags are undefined
                return None
            label = FLAG_LABEL.match(lines[j])
            if label:
                if label.group(1) in seen:
                    return []
                seen = seen | {label.group(1)}
                found = []
                for g in targets.get(label.group(1), []):
                    s = sources(g - 1, seen)
                    if s is None:
                        return None
                    found += s
                prev = j - 1
                while prev >= 0 and not lines[prev].strip():
                    prev -= 1
                if prev >= 0 and not FLAG_JUMP_OUT.match(lines[prev]):
                    s = sources(prev, seen)
                    if s is None:
                        return None
                    found += s
                return found
            if flag_setter_zf(lines[j]) is None:
                blocked.append(j)
                return None
            return [j]

        inserts = {}   # setter line -> _zf statement
        rewrites = {}  # branch line -> replacement
        decls = set()
        for i, line in enumerate(lines):
            m = jcc.match(line)
            if not m:
                continue
            indent, cond, rest = m.groups()
            if '(void)0; /* goto' in rest or 'RECOMP_DEAD_BRANCH(' in rest:
                # translator.py comments out a goto whose label is not in the function
                # (mark_dead_branches rewrites that to RECOMP_DEAD_BRANCH). The branch
                # has no ZF to materialize.
                unfixed.append((path.name, func_name(lines, i), '-', i + 1,
                                'target label outside this function; translator dropped the jump'))
                continue
            func = func_name(lines, i)
            prev = next((k for k in range(i - 1, -1, -1) if lines[k].strip()), None)
            label = FLAG_LABEL.match(lines[prev]) if prev is not None else None
            blocked.clear()
            srcs = sources(i - 1)
            decl = next((k for k in range(i, -1, -1)
                         if 'int _flags = 0; /* fallback flag var */' in lines[k]), None)
            if srcs is None or not srcs or decl is None:
                if srcs is not None and not srcs:
                    reason = 'no flag producer reaches it'
                elif decl is None:
                    reason = 'no _flags declaration'
                else:
                    at = blocked[-1] if blocked else None
                    reason = ('function entry' if at is None else
                              f'blocked at {path.name}:{at + 1}: {lines[at].strip()[:90]}')
                unfixed.append((path.name, func, label.group(1) if label else '-', i + 1, reason))
                continue
            for j in srcs:
                indent_j = lines[j][:len(lines[j]) - len(lines[j].lstrip())]
                inserts[j] = f'{indent_j}{flag_setter_zf(lines[j])}'
            decls.add(decl)
            rewrites[i] = f'{indent}if ({"" if cond == "je" else "!"}_zf) {rest}'
            fixed += 1
        if not rewrites:
            continue
        for d in decls:
            if '_zf' not in lines[d]:
                lines[d] += ' int _zf = 0;'
        new = []
        for k, line in enumerate(lines):
            new.append(rewrites.get(k, line))
            if k in inserts:
                new.append(inserts[k])
        path.write_text('\n'.join(new) + '\n')
    print(f'flag joins materialized: {fixed}')
    print(f'je/jne left on the _flags fallback: {len(unfixed)}')
    if verbose:
        for name, func, label, line, reason in unfixed:
            print(f'  {name}:{line} {func} {label}: {reason}')
    return fixed, unfixed



def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=Path, default=REPO / 'src/recomp/gen')
    ap.add_argument('--skip-analysis', action='store_true', help='reuse tools/*/output')
    ap.add_argument('--fix-flag-joins', type=Path, metavar='GEN_DIR',
                    help='run only the je/jne flag join pass over an existing generated directory')
    flag_report = ap.add_mutually_exclusive_group()
    flag_report.add_argument('--quiet-flag-joins', action='store_true',
                             help='print only flag-join counts (the default)')
    flag_report.add_argument('--verbose-flag-joins', action='store_true',
                             help='also list every unresolved flag-join diagnostic')
    args = ap.parse_args()
    if args.fix_flag_joins:
        fix_flag_joins(args.fix_flag_joins.resolve(), verbose=args.verbose_flag_joins)
        return
    setup_toolkit()
    out = args.out.resolve()
    if not XBE.exists():
        sys.exit(f'{XBE} missing: run scripts/setup.py --iso <disc> first')
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)

    if not args.skip_analysis:
        # disasm reads the parsed header beside the XBE as <stem>_analysis.json.
        py('-m', 'tools.xbe_parser', XBE, '--json', XBE.with_name('default_analysis.json'))
        py('-m', 'tools.disasm', XBE, '--force', '--seed-functions', REPO / 'mm3_runtime_function_seeds.json')
        py('-m', 'tools.func_id', XBE)
        py('-m', 'tools.abi_analysis', XBE)

    gui = out / 'gui_boundary_functions.json'
    py(REPO / 'tools/prepare_mm3_gui_boundaries.py', '--output', gui)
    from tools.recomp.manual_scan import scan
    skip, wrap, _ = scan(str(REPO / 'src/recomp_manual.c'))
    recovered = {int(entry['start'], 16) for entry in
                 json.loads((REPO / 'mm3_runtime_entry_repairs.json').read_text())}
    manual = out / 'manual_functions.json'
    manual.write_text(json.dumps({hex(a): f'sub_{a:08X}' for a in skip | wrap | recovered}))
    py('-m', 'tools.recomp', XBE, '--all', '--split', 1000, '--gen-dir', out, '--functions', gui,
       '--manual-functions', manual,
       '--skip-binary-check')

    tails = out / 'memmove_tail_functions.json'
    write_tail_functions(tails)
    for address in MEMMOVE_TAILS:
        name = f'sub_{address:08X}'
        resume = []
        if address == 0x93B04:
            resume = [arg for entry in MEMMOVE_RESUME for arg in ('--resume-entry', hex(entry))]
        body = py('-m', 'tools.recomp', XBE, '--function', hex(address), '--functions', tails,
                  '--seh-prolog', '0x00097AA4', '--skip-binary-check', *resume,
                  capture_output=True, text=True).stdout
        body = body.replace(f'void {name}(void)', f'void {name}_gen(void)')
        (out / f'recomp_{address:08x}_tail.c').write_text(PRELUDE.format('') + body)

    # Overlays keep the generated body under a _gen alias for the project wrapper.
    from tools.recomp import config
    from tools.recomp.translator import BatchTranslator
    config.configure_from_xbe(str(XBE))
    translator = BatchTranslator(
        xbe_path=str(XBE), func_json_path=str(DISASM / 'functions.json'),
        labels_json_path=str(DISASM / 'labels.json'),
        identified_json_path=str(TOOLKIT / 'tools/func_id/output/identified_functions.json'),
        abi_json_path=str(TOOLKIT / 'tools/abi_analysis/output/abi_functions.json'))
    for kind, addresses in (('seh', SEH_OVERLAYS), ('abi', ABI_OVERLAYS)):
        for address in addresses:
            name = f'sub_{address:08X}'
            body = translator.translate_single(address)
            if not body or f'void {name}(void)' not in body:
                sys.exit(f'generator did not return {name}')
            prelude = PRELUDE.format(f'#define {name} {name}_gen\n')
            (out / f'recomp_{address:08x}_{kind}.c').write_text(prelude + body)

    # Pointer entries call functions only they reach; lift those too until closed.
    extra = set()
    for _ in range(10):
        entries = runtime_entries(extra)
        manifest = out / 'runtime_entry_manifest.json'
        manifest.write_text(json.dumps(entries, indent=1))
        write_runtime_entry_functions(out / 'runtime_entry_functions.json', entries)
        py(REPO / 'scripts/Generate-MM3RuntimeEntries.py', out, '--manifest', manifest)
        missing = undefined_entry_calls(out) - extra
        if not missing:
            break
        extra |= missing
    fix_flag_joins(out, verbose=args.verbose_flag_joins)

    # Runtime semantics stay project-owned.
    shutil.copy(REPO / 'src/mm3_recomp_types.h', out / 'recomp_types.h')
    shutil.copy(REPO / 'tools/xboxrecomp/templates/runtime/recomp_types.h', out / 'recomp_base.h')
    (out / 'generation.sha256').write_text(generation_key())
    print(f'generated {len(list(out.glob("*.c")))} sources in {out}')


if __name__ == '__main__':
    main()
