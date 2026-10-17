"""Inspect captured native x64 thread registers and potential return addresses.

Usage: python tools/diagnostics/analyze_hang.py CAPTURE_DIRECTORY
Requires Windows DbgHelp and the matching symbols copied by capture_hang.py.
Stack scans are explicitly candidates, not an unwound or proven call chain.
"""
import argparse
import bisect
import ctypes as C
import json
import math
from pathlib import Path
import struct

from capture_hang import SYMBOL_INFO, U32, U64, HANDLE, BOOL, api


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('capture', type=Path)
    p.add_argument('--stack-bytes', type=int, default=16384)
    p.add_argument('--tid', type=int, help='Limit console output to one native thread ID')
    args = p.parse_args()
    report = json.loads((args.capture / 'capture.json').read_text())
    data = (args.capture / report['dump']['path']).read_bytes()
    if data[:4] != b'MDMP':
        p.error('Not a minidump')
    count, directory = struct.unpack_from('<II', data, 8)
    streams = {kind: (size, rva) for kind, size, rva in
               (struct.unpack_from('<III', data, directory + 12 * i)
                for i in range(count))}
    if 7 not in streams or struct.unpack_from('<H', data, streams[7][1])[0] != 9:
        p.error('Requires an AMD64 minidump')
    regions = []
    if 5 in streams:  # MINIDUMP_MEMORY_LIST
        rva = streams[5][1]
        nregions = struct.unpack_from('<I', data, rva)[0]
        for i in range(nregions):
            address, size, file_offset = struct.unpack_from('<QII', data, rva + 4 + 16 * i)
            regions.append((address, size, file_offset))
    if 9 in streams:  # MINIDUMP_MEMORY64_LIST
        rva = streams[9][1]
        nregions, file_offset = struct.unpack_from('<QQ', data, rva)
        for i in range(nregions):
            address, size = struct.unpack_from('<QQ', data, rva + 16 + 16 * i)
            regions.append((address, size, file_offset))
            file_offset += size
    regions.sort()
    region_starts = [item[0] for item in regions]

    def read_memory(address, size):
        """Read only captured memory, never the current process or disk images."""
        index = bisect.bisect_right(region_starts, address) - 1
        if index >= 0:
            start, length, file_offset = regions[index]
            if address + size <= start + length:
                offset = file_offset + address - start
                return data[offset:offset + size]
        return None

    def integer(address, size):
        raw = read_memory(address, size)
        return None if raw is None else int.from_bytes(raw, 'little')
    d = C.WinDLL('dbghelp', use_last_error=True)
    sym_init = api(d, 'SymInitializeW', BOOL, [HANDLE, C.c_wchar_p, BOOL])
    sym_options = api(d, 'SymSetOptions', U32, [U32])
    sym_load = api(d, 'SymLoadModuleExW', U64,
                   [HANDLE, HANDLE, C.c_wchar_p, C.c_wchar_p, U64, U32, HANDLE, U32])
    sym_addr = api(d, 'SymFromAddr', BOOL,
                   [HANDLE, U64, C.POINTER(U64), C.POINTER(SYMBOL_INFO)])
    sym_name = api(d, 'SymFromName', BOOL,
                   [HANDLE, C.c_char_p, C.POINTER(SYMBOL_INFO)])
    sym_clean = api(d, 'SymCleanup', BOOL, [HANDLE])
    session = HANDLE(0x125487)
    # Use only local symbols: no downloads, live process access, or debugger.
    sym_options(0x2 | 0x4 | 0x10 | 0x200 | 0x80000)
    if not sym_init(session, str((args.capture / 'symbols').resolve()), False):
        raise C.WinError(C.get_last_error())
    exe_base = report['image_base']
    exe_end = exe_base + report['image_size']
    try:
        for mod in report['modules']:
            path = args.capture / 'symbols' / Path(mod['path']).name
            if not path.exists():
                path = Path(mod['path'])
            sym_load(session, None, str(path.resolve()), None,
                     mod['base'], mod['size'], None, 0)

        tls_symbols = {}
        tls_index_address = None
        for key in ('_tls_index', 'g_eax', 'g_ecx', 'g_edx', 'g_ebx', 'g_esi', 'g_edi',
                    'g_esp', 'g_ebp', 'g_fp_cmp', 'g_fp_cc', 'g_fp_control_word',
                    'g_fp_top', 'g_fp_stack'):
            buf = C.create_string_buffer(C.sizeof(SYMBOL_INFO) + 2048)
            info = SYMBOL_INFO.from_buffer(buf)
            info.SizeOfStruct = C.sizeof(SYMBOL_INFO)
            info.MaxNameLen = 2048
            if sym_name(session, ('mm3_recomp!' + key).encode(), C.byref(info)):
                if key == '_tls_index':
                    tls_index_address = info.Address
                elif info.Flags & 0x4000:  # SYMFLAG_TLSREL
                    tls_symbols[key] = (info.Address, info.Size)
        tls_index = integer(tls_index_address, 4) if tls_index_address else None

        def name(address):
            buf = C.create_string_buffer(C.sizeof(SYMBOL_INFO) + 2048)
            info = SYMBOL_INFO.from_buffer(buf)
            info.SizeOfStruct = C.sizeof(SYMBOL_INFO)
            info.MaxNameLen = 2048
            disp = U64()
            if sym_addr(session, address, C.byref(disp), C.byref(info)):
                text = C.string_at(C.addressof(info) + SYMBOL_INFO.Name.offset,
                                   info.NameLen).decode(errors='replace')
                return f'{text}+0x{disp.value:x}'
            for mod in report['modules']:
                if mod['base'] <= address < mod['base'] + mod['size']:
                    return f"{Path(mod['path']).name}+0x{address-mod['base']:x}"
            return '?'

        size, rva = streams[3]  # MINIDUMP_THREAD_LIST
        nthreads = struct.unpack_from('<I', data, rva)[0]
        output = {'tls_index': tls_index, 'threads': []}
        for i in range(nthreads):
            tid, suspend, pri_class, pri, teb, stack_start, stack_size, stack_rva, ctx_size, ctx_rva = struct.unpack_from('<IIIIQQIIII', data, rva + 4 + 48 * i)
            # Windows AMD64 CONTEXT has sixteen integer registers at 120 and RIP at 248.
            registers = dict(zip(('rax', 'rcx', 'rdx', 'rbx', 'rsp', 'rbp', 'rsi', 'rdi',
                                  'r8', 'r9', 'r10', 'r11', 'r12', 'r13', 'r14', 'r15', 'rip'),
                                 struct.unpack_from('<17Q', data, ctx_rva + 120)))
            rip = registers['rip']
            item = {'tid': tid, 'rip': hex(rip), 'symbol': name(rip),
                    'registers': {k: hex(v) for k, v in registers.items()},
                    'teb': hex(teb),
                    'stack_start': hex(stack_start), 'stack_size': stack_size,
                    'return_address_candidates': []}
            # AMD64 TEB.ThreadLocalStoragePointer is at 0x58. Resolve the title's
            # TLS module index and every symbol offset from this capture's PDB.
            tls_vector = integer(teb + 0x58, 8)
            tls_base = (integer(tls_vector + tls_index * 8, 8)
                        if tls_vector and tls_index is not None else None)
            if tls_base:
                item['tls_base'] = hex(tls_base)
                item['guest_state'] = {}
                for key, (offset, length) in tls_symbols.items():
                    address = tls_base + offset
                    raw = read_memory(address, length)
                    if raw is None:
                        continue
                    value = ([v if math.isfinite(v) else str(v)
                              for v in struct.unpack('<8d', raw)] if key == 'g_fp_stack'
                             else int.from_bytes(raw, 'little'))
                    item['guest_state'][key] = {'address': hex(address), 'value': value}
            start = max(0, registers['rsp'] - stack_start)
            for offset in range(start, min(stack_size - 7, start + args.stack_bytes), 8):
                address = struct.unpack_from('<Q', data, stack_rva + offset)[0]
                if exe_base <= address < exe_end:
                    item['return_address_candidates'].append({
                        'slot': hex(stack_start + offset), 'address': hex(address),
                        'symbol': name(address)})
            output['threads'].append(item)
        dest = args.capture / 'native-threads.json'
        dest.write_text(json.dumps(output, indent=2, allow_nan=False), encoding='utf8')
        for item in output['threads']:
            if args.tid is not None and item['tid'] != args.tid:
                continue
            print(f"TID {item['tid']} RIP {item['rip']} {item['symbol']}")
            print('  ' + ' '.join(f'{k}={v}' for k, v in item['registers'].items()))
            if 'guest_state' in item:
                print('  guest ' + ' '.join(f'{k}={v["value"]}' for k, v in item['guest_state'].items()
                                          if k != 'g_fp_stack'))
            for frame in item['return_address_candidates'][:25]:
                print(f"  candidate {frame['slot']} -> {frame['symbol']}")
        print(dest)
    finally:
        sym_clean(session)


if __name__ == '__main__':
    main()
