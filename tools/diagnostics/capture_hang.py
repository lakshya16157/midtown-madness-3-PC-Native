"""Capture an already-running MM3 process once, without debugger attachment.

Usage: python capture_hang.py --pid 1234 --output-dir path
Writes a minidump with stacks/data/thread metadata and bounded guest RAM copies.
No suspension is performed by this script; MiniDumpWriteDump manages its own
brief snapshot. RAM copies are live and are timestamped, not an atomic snapshot.
"""
import argparse
import ctypes as C
from ctypes import wintypes as W
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import sys
import time

U32,U64,SIZE,HANDLE,BOOL=C.c_uint32,C.c_uint64,C.c_size_t,C.c_void_p,C.c_int32
class SYMBOL_INFO(C.Structure):
    _fields_=[('SizeOfStruct',U32),('TypeIndex',U32),('Reserved',U64*2),('Index',U32),('Size',U32),('ModBase',U64),('Flags',U32),('Value',U64),('Address',U64),('Register',U32),('Scope',U32),('Tag',U32),('NameLen',U32),('MaxNameLen',U32),('Name',C.c_char*1)]
class MODULEINFO(C.Structure):
    _fields_=[('lpBaseOfDll',HANDLE),('SizeOfImage',U32),('EntryPoint',HANDLE)]
def utc(): return datetime.now(timezone.utc).isoformat()
def api(dll,name,restype,args):
    f=getattr(dll,name);f.restype=restype;f.argtypes=args;return f
def error(action):
    e=C.get_last_error();return {'action':action,'winerror':e,'message':C.FormatError(e).strip()}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--pid',type=int,required=True)
    p.add_argument('--output-dir',type=Path,required=True)
    args=p.parse_args()
    if os.name!='nt' or C.sizeof(HANDLE)!=8: p.error('Requires native x64 Windows Python')
    if args.pid<=0 or args.pid==os.getpid():p.error('Specify another positive process ID')
    out=args.output_dir.resolve();out.mkdir(parents=True,exist_ok=True)
    report={'pid':args.pid,'started_utc':utc(),'method':'MiniDumpWriteDump + bounded live ReadProcessMemory; no debugger attach','errors':[],'modules':[],'symbols':{},'memory_copies':[]}
    def persist(): (out/'capture.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    k=C.WinDLL('kernel32',use_last_error=True);d=C.WinDLL('dbghelp',use_last_error=True);ps=C.WinDLL('psapi',use_last_error=True)
    OpenProcess=api(k,'OpenProcess',HANDLE,[U32,BOOL,U32])
    CloseHandle=api(k,'CloseHandle',BOOL,[HANDLE])
    ReadProcessMemory=api(k,'ReadProcessMemory',BOOL,[HANDLE,HANDLE,HANDLE,SIZE,C.POINTER(SIZE)])
    QueryImage=api(k,'QueryFullProcessImageNameW',BOOL,[HANDLE,U32,W.LPWSTR,C.POINTER(U32)])
    EnumModules=api(ps,'EnumProcessModulesEx',BOOL,[HANDLE,C.POINTER(HANDLE),U32,C.POINTER(U32),U32])
    GetModuleName=api(ps,'GetModuleFileNameExW',U32,[HANDLE,HANDLE,W.LPWSTR,U32])
    GetModuleInfo=api(ps,'GetModuleInformation',BOOL,[HANDLE,HANDLE,C.POINTER(MODULEINFO),U32])
    MiniDump=api(d,'MiniDumpWriteDump',BOOL,[HANDLE,U32,HANDLE,U32,HANDLE,HANDLE,HANDLE])
    SymOptions=api(d,'SymSetOptions',U32,[U32]);SymInit=api(d,'SymInitializeW',BOOL,[HANDLE,W.LPCWSTR,BOOL]);SymClean=api(d,'SymCleanup',BOOL,[HANDLE])
    SymLoad=api(d,'SymLoadModuleExW',U64,[HANDLE,HANDLE,W.LPCWSTR,W.LPCWSTR,U64,U32,HANDLE,U32])
    SymName=api(d,'SymFromName',BOOL,[HANDLE,C.c_char_p,C.POINTER(SYMBOL_INFO)])
    proc=OpenProcess(0x400|0x10|0x40,False,args.pid) # QUERY_INFORMATION|VM_READ|DUP_HANDLE
    if not proc:
        report['errors'].append(error('OpenProcess'));persist();return 2
    initialized=False
    def read(address,size):
        buf=C.create_string_buffer(size);count=SIZE()
        ok=ReadProcessMemory(proc,HANDLE(address),buf,size,C.byref(count))
        return buf.raw[:count.value],None if ok else error('ReadProcessMemory')
    try:
        name=C.create_unicode_buffer(32768);length=U32(len(name))
        if QueryImage(proc,0,name,C.byref(length)):report['executable']=name.value
        else:report['errors'].append(error('QueryFullProcessImageNameW'))
        modules=(HANDLE*2048)();needed=U32()
        if EnumModules(proc,modules,C.sizeof(modules),C.byref(needed),3):
            for handle in modules[:min(needed.value//C.sizeof(HANDLE),len(modules))]:
                info=MODULEINFO();path=C.create_unicode_buffer(32768)
                if GetModuleInfo(proc,handle,C.byref(info),C.sizeof(info)):
                    GetModuleName(proc,handle,path,len(path))
                    report['modules'].append({'path':path.value,'base':int(info.lpBaseOfDll or 0),'size':int(info.SizeOfImage)})
        else:report['errors'].append(error('EnumProcessModulesEx'))
        exe=next((m for m in report['modules'] if Path(m['path']).name.lower()=='mm3_recomp.exe'),None)
        if exe is None:
            report['errors'].append({'action':'identity','message':'Target is not a loaded mm3_recomp.exe process'});persist();return 2
        report['image_base']=exe['base'];report['image_size']=exe['size'];persist()
        # This mode includes stacks and module data, but does not enumerate and
        # copy all the large aliased guest mirror views.
        flags=0x1|0x4|0x20|0x100|0x800|0x1000|0x20000|0x80000
        report['dump']={'path':'hang.dmp','flags':hex(flags),'started_utc':utc()};persist()
        import msvcrt
        with (out/'hang.dmp').open('wb') as f:
            ok=MiniDump(proc,args.pid,HANDLE(msvcrt.get_osfhandle(f.fileno())),flags,None,None,None)
            if not ok:report['errors'].append(error('MiniDumpWriteDump'))
        report['dump'].update(ok=bool(ok),finished_utc=utc(),bytes=(out/'hang.dmp').stat().st_size);persist()
        # Load the actual running image's PDB; never use hard-coded offsets from
        # a previous build. No public/network symbol server is configured.
        exepath=Path(exe['path']);SymOptions(0x2|0x4|0x10|0x200|0x80000)
        initialized=bool(SymInit(proc,str(exepath.parent),False))
        if not initialized: report['errors'].append(error('SymInitializeW'))
        elif not SymLoad(proc,None,str(exepath),'mm3_recomp',exe['base'],exe['size'],None,0):report['errors'].append(error('SymLoadModuleExW'))
        else:
            names=['g_memory_base','g_memory_size','g_memory_offset','g_xbox_mem_offset','g_contig_memory','g_nv2a_memory','g_nv2a_wake','g_nv2a_dma_lock','s_fifo','s_packet','s_capture','s_tot_words','s_tot_jumps','s_tot_calls','s_tot_unknown','s_scan_incomplete','s_get_held','s_get_held_value','s_get_held_notify','g_notify_device_ptr','g_notify_count','g_notify_param','g_notify_event_off','g_fence_mirrors','g_fence_mirror_count']
            for symbol_name in names:
                buf=C.create_string_buffer(C.sizeof(SYMBOL_INFO)+2048);symbol=SYMBOL_INFO.from_buffer(buf);symbol.SizeOfStruct=C.sizeof(SYMBOL_INFO);symbol.MaxNameLen=2048
                if not SymName(proc,('mm3_recomp!'+symbol_name).encode(),C.byref(symbol)):
                    report['symbols'][symbol_name]={'error':error('SymFromName')};continue
                b,err=read(symbol.Address,min(symbol.Size,1024*1024))
                item={'address':int(symbol.Address),'rva':int(symbol.Address-exe['base']),'size':int(symbol.Size),'bytes':len(b)}
                if len(b)<=256:item['hex']=b.hex()
                else:
                    fn='symbol-'+symbol_name+'.bin';(out/fn).write_bytes(b);item['path']=fn
                if err:item['error']=err
                if len(b) in (4,8):item['value']=int.from_bytes(b,'little')
                report['symbols'][symbol_name]=item
        persist()
        def value(name):return report['symbols'].get(name,{}).get('value',0)
        def copy_memory(label,address,size,guest_va):
            item={'label':label,'address':address,'guest_va':guest_va,'size':size,'started_utc':utc(),'path':label+'.bin','holes':[],'bytes_read':0};report['memory_copies'].append(item);persist()
            with (out/item['path']).open('wb') as f:
                for offset in range(0,size,1024*1024):
                    n=min(1024*1024,size-offset);b,err=read(address+offset,n)
                    if err or len(b)!=n:
                        block=bytearray(n)
                        for page in range(0,n,4096):
                            pn=min(4096,n-page);pb,pe=read(address+offset+page,pn);block[page:page+len(pb)]=pb;item['bytes_read']+=len(pb)
                            if pe or len(pb)!=pn:item['holes'].append({'offset':offset+page,'size':pn,'bytes_read':len(pb),'error':pe})
                        f.write(block)
                    else:f.write(b);item['bytes_read']+=len(b)
            item['finished_utc']=utc();persist()
        contig=value('g_contig_memory')
        if contig:copy_memory('guest-physical-64mb',contig,64*1024*1024,0x80000000)
        else:report['errors'].append({'action':'physical RAM copy','message':'g_contig_memory pointer unavailable'})
        offset=value('g_memory_base') or value('g_memory_offset') or value('g_xbox_mem_offset')
        size=value('g_memory_size')
        if offset and 0<size<=128*1024*1024:copy_memory('guest-main-ram',offset,size,0)
        else:report['errors'].append({'action':'main RAM copy','message':'bounded g_memory_offset/g_memory_size unavailable','offset':offset,'size':size})
        nv2a=value('g_nv2a_memory')
        if nv2a:copy_memory('nv2a-registers',nv2a,16*1024*1024,0xfd000000)
        # Preserve the exact image and PDB before any future rebuild replaces it.
        symbols=out/'symbols';symbols.mkdir(exist_ok=True)
        for src in (exepath,exepath.with_suffix('.pdb')):
            if src.is_file():
                dst=symbols/src.name;shutil.copy2(src,dst)
                with dst.open('rb') as matching_file:digest=hashlib.file_digest(matching_file,'sha256').hexdigest()
                report.setdefault('matching_files',[]).append({'path':str(dst.relative_to(out)),'sha256':digest,'bytes':dst.stat().st_size})
        report['finished_utc']=utc();persist()
        print(str(out/'capture.json'));return 0 if report['dump']['ok'] else 2
    finally:
        if initialized:SymClean(proc)
        CloseHandle(proc)
        persist()

if __name__=='__main__':sys.exit(main())
