"""Opt-in native x64 PC sampling for an owned MM3 profiling run.

Each sampled thread is briefly suspended, its PC read, then resumed in finally.
This perturbs scheduling: use a separate unsampled run for FPS validation.
No debugger attachment, context writes, process-memory writes or stack dumps;
the one stack read is the 8-byte slot at RSP, which names the caller of a
leaf routine such as memcpy or memcmp (meaningless for framed functions).
Symbols are resolved afterward from local matching files only.
"""
import argparse
from collections import Counter, defaultdict
import csv
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
import os
from pathlib import Path
import struct
import time

from capture_hang import SYMBOL_INFO, MODULEINFO, U32, U64, HANDLE, BOOL, api


class THREADENTRY32(C.Structure):
    _fields_ = [('dwSize', U32), ('cntUsage', U32), ('th32ThreadID', U32),
                ('th32OwnerProcessID', U32), ('tpBasePri', C.c_int32),
                ('tpDeltaPri', C.c_int32), ('dwFlags', U32)]


def save(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False)+'\n', encoding='utf8')


def sample_context(handle, address, suspend, resume, context, clock, read_slot=None):
    """Read a PC and always undo only this observer's successful suspend."""
    begin = clock()
    count = suspend(handle)
    if count == 0xFFFFFFFF:
        return None
    ok, rip, ret = False, 0, 0
    try:
        if count == 0:
            ok = bool(context(handle,HANDLE(address)))
            if ok:
                rip = struct.unpack_from('<Q',C.string_at(address+248,8))[0]
                if read_slot:
                    ret = read_slot(struct.unpack_from('<Q',C.string_at(address+152,8))[0])
    finally:
        if resume(handle) == 0xFFFFFFFF:
            raise OSError('ResumeThread failed after sampling; target state must be checked')
    end = clock()
    return end, rip, (end-begin)*1000, ok, count, ret


def thread_is_running(handle, exit_code):
    code = U32()
    return bool(exit_code(handle, C.byref(code))) and code.value == 259


def open_target_thread(tid, pid, open_thread, owner, exit_code, close):
    """Reject a TID recycled between the owner snapshot and OpenThread."""
    handle = open_thread(0x2|0x8|0x40, False, tid)
    if handle and (owner(handle) != pid or not thread_is_running(handle, exit_code)):
        close(handle)
        return None
    return handle


def evict_terminated_threads(handles, previous, exit_code, close):
    """A cached handle remains tied to the old thread if its TID is reused."""
    for tid, handle in list(handles.items()):
        if not thread_is_running(handle, exit_code):
            close(handles.pop(tid))
            previous.pop(tid, None)


def capture(args):
    if os.name != 'nt' or C.sizeof(HANDLE) != 8 or args.pid == os.getpid():
        raise ValueError('Requires another native x64 Windows process')
    if not 0 < args.seconds <= 300 or not 5 <= args.interval_ms <= 100 or not 1 <= args.threads <= 8:
        raise ValueError('Invalid bounded sampling options')
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    k, ps = C.WinDLL('kernel32', use_last_error=True), C.WinDLL('psapi', use_last_error=True)
    op = api(k, 'OpenProcess', HANDLE, [U32, BOOL, U32])
    ot = api(k, 'OpenThread', HANDLE, [U32, BOOL, U32])
    thread_owner = api(k, 'GetProcessIdOfThread', U32, [HANDLE])
    thread_exit = api(k, 'GetExitCodeThread', BOOL, [HANDLE,C.POINTER(U32)])
    close = api(k, 'CloseHandle', BOOL, [HANDLE])
    image = api(k, 'QueryFullProcessImageNameW', BOOL, [HANDLE,U32,W.LPWSTR,C.POINTER(U32)])
    enum = api(ps, 'EnumProcessModulesEx', BOOL, [HANDLE,C.POINTER(HANDLE),U32,C.POINTER(U32),U32])
    modname = api(ps, 'GetModuleFileNameExW', U32, [HANDLE,HANDLE,W.LPWSTR,U32])
    modinfo = api(ps, 'GetModuleInformation', BOOL, [HANDLE,HANDLE,C.POINTER(MODULEINFO),U32])
    snap = api(k, 'CreateToolhelp32Snapshot', HANDLE, [U32,U32])
    first = api(k, 'Thread32First', BOOL, [HANDLE,C.POINTER(THREADENTRY32)])
    nxt = api(k, 'Thread32Next', BOOL, [HANDLE,C.POINTER(THREADENTRY32)])
    cycles = api(k, 'QueryThreadCycleTime', BOOL, [HANDLE,C.POINTER(U64)])
    suspend = api(k, 'SuspendThread', U32, [HANDLE])
    resume = api(k, 'ResumeThread', U32, [HANDLE])
    context = api(k, 'GetThreadContext', BOOL, [HANDLE,HANDLE])
    qpc = api(k, 'QueryPerformanceCounter', BOOL, [C.POINTER(C.c_longlong)])
    freq = api(k, 'QueryPerformanceFrequency', BOOL, [C.POINTER(C.c_longlong)])
    alive = api(k, 'GetExitCodeProcess', BOOL, [HANDLE,C.POINTER(U32)])
    read_memory = api(k, 'ReadProcessMemory', BOOL, [HANDLE,C.c_void_p,C.c_void_p,C.c_size_t,C.POINTER(C.c_size_t)])
    frequency = C.c_longlong()
    if not freq(C.byref(frequency)) or frequency.value <= 0:
        raise OSError('QPC unavailable')
    def clock():
        value = C.c_longlong()
        if not qpc(C.byref(value)):
            raise C.WinError(C.get_last_error())
        return value.value * 1000 / frequency.value
    proc = op(0x400 | 0x10, False, args.pid)
    if not proc:
        raise C.WinError(C.get_last_error())
    handles, previous, totals = {}, {}, Counter()
    slot, got = C.c_uint64(), C.c_size_t()
    def read_slot(rsp):
        ok = rsp and read_memory(proc, C.c_void_p(rsp), C.byref(slot), 8, C.byref(got))
        return slot.value if ok and got.value == 8 else 0
    report = {'pid':args.pid, 'method':__doc__, 'modules':[], 'errors':[],
              'interval_ms':args.interval_ms, 'max_sampled_threads_per_tick':args.threads,
              'minimum_cycle_delta':args.min_cycles, 'cycles_are_not_milliseconds':True}
    try:
        name, length = C.create_unicode_buffer(32768), U32(32768)
        if not image(proc, 0, name, C.byref(length)):
            raise C.WinError(C.get_last_error())
        exe = Path(name.value).resolve()
        if exe.name.lower() != 'mm3_recomp.exe' or (args.exe and exe != args.exe.resolve()):
            raise ValueError('Target identity does not match the intended MM3 executable')
        report.update(executable=str(exe), sha256=hashlib.sha256(exe.read_bytes()).hexdigest())
        modules, needed = (HANDLE*2048)(), U32()
        if not enum(proc, modules, C.sizeof(modules), C.byref(needed), 3):
            raise C.WinError(C.get_last_error())
        for handle in modules[:min(len(modules), needed.value//C.sizeof(HANDLE))]:
            info, path = MODULEINFO(), C.create_unicode_buffer(32768)
            if modinfo(proc,handle,C.byref(info),C.sizeof(info)):
                modname(proc,handle,path,len(path))
                report['modules'].append({'path':path.value,'base':int(info.lpBaseOfDll or 0),'size':int(info.SizeOfImage)})
        started = report['start_qpc_ms'] = clock()
        save(out/'capture.json', report)
        # AMD64 CONTEXT offsets/alignment are from the bundled Windows SDK
        # winnt.h. CONTROL|INTEGER; RIP at248, RSP at152, flags at48, 16-byte alignment.
        storage = C.create_string_buffer(1232+15)
        address = (C.addressof(storage)+15)&~15
        next_enumeration, deadline = started, started+args.seconds*1000
        with (out/'samples.csv').open('w',encoding='ascii',newline='') as stream:
            writer = csv.writer(stream)
            writer.writerow(['qpc_ms','tid','rip','cycle_delta','suspended_us','context_ok','previous_suspend_count','ret'])
            while clock() < deadline and not (args.stop_file and args.stop_file.exists()):
                tick = clock()
                code = U32()
                if not alive(proc,C.byref(code)) or code.value != 259:
                    report['target_exited'] = True
                    break
                evict_terminated_threads(handles, previous, thread_exit, close)
                if tick >= next_enumeration:
                    snapshot = snap(4,0)
                    if snapshot == C.c_void_p(-1).value:
                        raise C.WinError(C.get_last_error())
                    observed = set()
                    try:
                        entry = THREADENTRY32(); entry.dwSize=C.sizeof(entry)
                        valid = first(snapshot,C.byref(entry))
                        while valid:
                            if entry.th32OwnerProcessID == args.pid:
                                tid = int(entry.th32ThreadID); observed.add(tid)
                                if tid not in handles:
                                    handle = open_target_thread(tid,args.pid,ot,thread_owner,thread_exit,close)
                                    if handle: handles[tid]=handle
                            valid = nxt(snapshot,C.byref(entry))
                    finally:
                        close(snapshot)
                    for tid in set(handles)-observed:
                        close(handles.pop(tid)); previous.pop(tid,None)
                    next_enumeration=tick+1000
                active=[]
                for tid,handle in handles.items():
                    value=U64()
                    if cycles(handle,C.byref(value)):
                        old=previous.get(tid,value.value); delta=max(0,value.value-old)
                        previous[tid]=value.value; totals[tid]+=delta
                        if delta >= args.min_cycles: active.append((delta,tid,handle))
                for delta,tid,handle in sorted(active,reverse=True)[:args.threads]:
                    C.memset(address,0,1232)
                    C.c_uint32.from_address(address+48).value=0x100003
                    sampled=sample_context(handle,address,suspend,resume,context,clock,read_slot)
                    if sampled is None:
                        report['errors'].append({'operation':'SuspendThread','tid':tid,'error':C.get_last_error()})
                        continue
                    end,rip,suspended_us,ok,count,ret=sampled
                    writer.writerow([f'{end:.4f}',tid,hex(rip),delta,f'{suspended_us:.3f}',int(ok),count,hex(ret)])
                time.sleep(max(0,(args.interval_ms-(clock()-tick))/1000))
        report['end_qpc_ms']=clock()
        report['thread_cycle_deltas']={str(t):v for t,v in totals.items()}
    finally:
        for handle in handles.values(): close(handle)
        close(proc)
        save(out/'capture.json',report)
    return 0


def resolve(args):
    out=args.output.resolve()
    report=json.loads((out/'capture.json').read_text())
    exe=Path(report['executable'])
    if hashlib.sha256(exe.read_bytes()).hexdigest()!=report['sha256']:
        raise ValueError('Executable changed since sampling; matching EXE/PDB required')
    d=C.WinDLL('dbghelp',use_last_error=True)
    init=api(d,'SymInitializeW',BOOL,[HANDLE,W.LPCWSTR,BOOL])
    load=api(d,'SymLoadModuleExW',U64,[HANDLE,HANDLE,W.LPCWSTR,W.LPCWSTR,U64,U32,HANDLE,U32])
    addr=api(d,'SymFromAddr',BOOL,[HANDLE,U64,C.POINTER(U64),C.POINTER(SYMBOL_INFO)])
    clean=api(d,'SymCleanup',BOOL,[HANDLE])
    api(d,'SymSetOptions',U32,[U32])(0x2|0x4|0x10|0x200|0x80000)
    session=HANDLE(0x125489)
    if not init(session,str(exe.parent),False): raise C.WinError(C.get_last_error())
    names={}; rows=[]; counts=defaultdict(Counter); callers=defaultdict(Counter); suspensions=[]
    windows=json.loads(args.windows_json.read_text(encoding='utf-8-sig')) if args.windows_json else None
    try:
        image=next(m for m in report['modules'] if Path(m['path']).resolve()==exe.resolve())
        if not load(session,None,str(exe),None,image['base'],image['size'],None,0):
            raise C.WinError(C.get_last_error())
        def name(pc):
            if pc in names:return names[pc]
            module=next((m for m in report['modules'] if m['base']<=pc<m['base']+m['size']),None)
            value='unknown'
            if module:
                value=Path(module['path']).name+'+'+hex(pc-module['base'])
                if module is image:
                    buf=C.create_string_buffer(C.sizeof(SYMBOL_INFO)+2048)
                    info=SYMBOL_INFO.from_buffer(buf);info.SizeOfStruct=C.sizeof(SYMBOL_INFO);info.MaxNameLen=2048
                    displacement=U64()
                    if addr(session,pc,C.byref(displacement),C.byref(info)):
                        value=C.string_at(C.addressof(info)+SYMBOL_INFO.Name.offset,info.NameLen).decode(errors='replace')
            names[pc]=value;return value
        with (out/'samples.csv').open() as stream:
            for row in csv.DictReader(stream):
                if windows and not any(w['start']<=float(row['qpc_ms'])<=w['end'] for w in windows):
                    continue
                if int(row['context_ok']):
                    symbol=name(int(row['rip'],16));row['symbol']=symbol
                    counts[row['tid']][symbol]+=1
                    # A sample inside a system DLL (memcpy, memcmp, waits) is
                    # charged to the code that called it, via the RSP slot.
                    if '+0x' in symbol and int(row.get('ret') or '0',16):
                        row['caller']=name(int(row['ret'],16))
                        callers[row['tid']][symbol.split('+')[0]+' <- '+row['caller']]+=1
                    suspensions.append(float(row['suspended_us']));rows.append(row)
        with (out/'resolved.csv').open('w',encoding='utf8',newline='') as stream:
            fields=list(dict.fromkeys(key for row in rows for key in row)) or ['symbol']
            writer=csv.DictWriter(stream,fieldnames=fields);writer.writeheader();writer.writerows(rows)
        summary={'sample_count':len(rows),'maximum_suspend_us':max(suspensions,default=0),'total_suspend_us':sum(suspensions),'top_PC_samples_by_tid':{tid:count.most_common(30) for tid,count in counts.items()},'top_system_dll_callers_by_tid':{tid:count.most_common(30) for tid,count in callers.items()},'thread_cycle_deltas':report['thread_cycle_deltas'],'limits':['Sampling suspends threads briefly and perturbs scheduling; sampled FPS is not a performance verdict.','PC samples are not stacks or exact CPU-time attribution. Idle/syscall endpoints can be sampled after work happened.','Cycle deltas are not converted to time. Only matching local EXE/PDB symbols are loaded.','System-DLL callers come from the slot at RSP: right for leaf routines such as memcpy/memcmp, a guess elsewhere.']}
        save(out/'summary.json',summary)
        print(json.dumps(summary,indent=2))
    finally:clean(session)
    return 0


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('mode',choices=['capture','resolve'])
    p.add_argument('--pid',type=int)
    p.add_argument('--exe',type=Path)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--seconds',type=float,default=180)
    p.add_argument('--interval-ms',type=float,default=13)
    p.add_argument('--threads',type=int,default=4)
    p.add_argument('--min-cycles',type=int,default=100000)
    p.add_argument('--stop-file',type=Path)
    p.add_argument('--windows-json',type=Path,help='Resolve only PC samples inside recorded driving windows')
    args=p.parse_args()
    if args.mode=='capture' and (not args.pid or args.pid<=0):p.error('Positive --pid required')
    return capture(args) if args.mode=='capture' else resolve(args)


if __name__=='__main__':raise SystemExit(main())
