"""Live, read-only: the menu manager's menus and their screens. menus.py NAME"""
import ctypes, json, re, struct, sys
from ctypes import wintypes
from mm3bot import inst
name = sys.argv[1]
log = (inst(name) / 'stderr.log').read_text(errors='replace')
base = int(re.search(r'xbox_MemoryLayoutInit: mapped \d+ KB at 0x([0-9A-Fa-f]+)', log).group(1), 16)
pid = json.loads((inst(name) / 'state.json').read_text())['pid']
k = ctypes.WinDLL('kernel32', use_last_error=True)
k.OpenProcess.restype = wintypes.HANDLE
k.ReadProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
h = k.OpenProcess(0x0010 | 0x1000, False, pid)
def u(va):
    buf = ctypes.create_string_buffer(4); got = ctypes.c_size_t()
    k.ReadProcessMemory(h, ctypes.c_void_p(base + va), buf, 4, ctypes.byref(got))
    return struct.unpack('<I', buf.raw)[0]
def b(va):
    return u(va) & 0xFF
ok = lambda p: 0x10000 <= p < 0x08000000
mgr = u(0x3C5698)
if not ok(mgr): print('no menu manager'); sys.exit()
head = u(mgr + 4); stack = [u(head + 4)]; seen = set(); rows = []
while stack:
    n = stack.pop()
    if not ok(n) or n in seen or n == head: continue
    seen.add(n); rows.append(n); stack += [u(n), u(n + 8)]
for n in sorted(rows, key=lambda n: u(n + 0xC)):
    m = u(n + 0x10); s = u(m + 0xC) if ok(m) else 0
    sv = u(s) if ok(s) else 0
    print('id %d menu %08X vt %08X active %d screen %08X screen_vt %08X %s' % (u(n + 0xC), m, u(m), b(m + 4), s, sv, 'DEAD' if s and sv != 0x37D2C0 else ''))
