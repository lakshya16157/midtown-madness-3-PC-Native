"""Observe a diagnostic game and capture evidence after prolonged lost flips.

The observer never writes game memory, closes the game, or changes its controls.
"""
import argparse
import ctypes as C
from ctypes import wintypes as W
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import subprocess
import sys
import time


class PROCESSENTRY32W(C.Structure):
    _fields_ = [('dwSize', W.DWORD), ('cntUsage', W.DWORD), ('th32ProcessID', W.DWORD),
                ('th32DefaultHeapID', C.c_size_t), ('th32ModuleID', W.DWORD),
                ('cntThreads', W.DWORD), ('th32ParentProcessID', W.DWORD),
                ('pcPriClassBase', W.LONG), ('dwFlags', W.DWORD), ('szExeFile', W.WCHAR * 260)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--parent-pid', type=int)
    parser.add_argument('--pid', type=int)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    args.output_dir.mkdir(parents=True, exist_ok=True)
    kernel = C.WinDLL('kernel32', use_last_error=True)
    user = C.WinDLL('user32', use_last_error=True)
    kernel.CreateToolhelp32Snapshot.argtypes = [W.DWORD, W.DWORD]
    kernel.CreateToolhelp32Snapshot.restype = W.HANDLE
    kernel.Process32FirstW.argtypes = kernel.Process32NextW.argtypes = [W.HANDLE, C.POINTER(PROCESSENTRY32W)]
    kernel.CloseHandle.argtypes = [W.HANDLE]
    kernel.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
    kernel.OpenProcess.restype = W.HANDLE
    kernel.GetExitCodeProcess.argtypes = [W.HANDLE, C.POINTER(W.DWORD)]
    user.GetWindowThreadProcessId.argtypes = [W.HWND, C.POINTER(W.DWORD)]
    user.GetWindowTextW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
    callback_type = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    user.EnumWindows.argtypes = [callback_type, W.LPARAM]

    def child_pid():
        snapshot = kernel.CreateToolhelp32Snapshot(2, 0)
        if snapshot == C.c_void_p(-1).value:
            return None
        try:
            entry = PROCESSENTRY32W()
            entry.dwSize = C.sizeof(entry)
            ok = kernel.Process32FirstW(snapshot, C.byref(entry))
            while ok:
                if entry.th32ParentProcessID == args.parent_pid and entry.szExeFile.lower() == 'mm3_recomp.exe':
                    return int(entry.th32ProcessID)
                ok = kernel.Process32NextW(snapshot, C.byref(entry))
        finally:
            kernel.CloseHandle(snapshot)

    def title(pid):
        titles = []
        @callback_type
        def visit(window, _):
            owner = W.DWORD()
            user.GetWindowThreadProcessId(window, C.byref(owner))
            if owner.value == pid:
                text = C.create_unicode_buffer(512)
                user.GetWindowTextW(window, text, len(text))
                if text.value:
                    titles.append(text.value)
            return True
        user.EnumWindows(visit, 0)
        return next((value for value in titles if 'Midtown Madness' in value), '')

    deadline = time.monotonic() + 120
    if not args.pid and not args.parent_pid:
        parser.error('Supply --parent-pid or --pid')
    pid = args.pid
    while not pid and time.monotonic() < deadline:
        pid = child_pid()
        if not pid:
            time.sleep(0.25)
    if not pid:
        raise RuntimeError('No child game process appeared within 120 seconds')
    handle = kernel.OpenProcess(0x1000, False, pid)
    if not handle:
        raise C.WinError(C.get_last_error())
    print(f'Observing game PID {pid}', flush=True)
    (args.output_dir / 'game-pid.json').write_text(json.dumps({'pid': pid}) + '\n')
    csv = args.output_dir / 'frames.csv'
    previous_size = 0
    last_progress = time.monotonic()
    ready = False
    captured = False
    try:
        with (args.output_dir / 'progress.jsonl').open('a', encoding='utf-8', buffering=1) as log:
            while True:
                code = W.DWORD()
                if not kernel.GetExitCodeProcess(handle, C.byref(code)) or code.value != 259:
                    print(f'Game exited: {code.value}', flush=True)
                    break
                now = time.monotonic()
                window_title = title(pid)
                size = csv.stat().st_size if csv.exists() else 0
                match = re.search(r'FPS:\s*([0-9.]+)', window_title)
                if size > previous_size or (match and float(match[1]) > 0):
                    last_progress = now
                    # Movie-only presentations can stop while loading. Arm only
                    # after persisted guest flips establish actual rendering.
                    ready = ready or size > 128
                    if captured:
                        captured = False
                previous_size = size
                stalled = round(now - last_progress, 2)
                log.write(json.dumps({'utc': datetime.now(timezone.utc).isoformat(), 'pid': pid,
                    'title': window_title, 'csv_bytes': size, 'seconds_without_progress': stalled}) + '\n')
                manual = args.output_dir / 'capture.flag'
                if not captured and ((ready and stalled >= 15) or manual.exists()):
                    helper = root / 'tools/diagnostics/capture_hang.py'
                    if helper.exists():
                        print(f'Capturing evidence after {stalled}s without flips', flush=True)
                        stamp = datetime.now(timezone.utc).strftime('%Y%m%d-%H%M%S')
                        capture_dir = args.output_dir / ('hang-' + stamp)
                        result = subprocess.run([sys.executable, str(helper), '--pid', str(pid),
                            '--output-dir', str(capture_dir)], capture_output=True, text=True, timeout=120)
                        (args.output_dir / 'capture-output.log').write_text(result.stdout + result.stderr, encoding='utf-8')
                        captured = result.returncode == 0
                        if not captured:
                            print(f'Capture returned {result.returncode}; see capture-output.log', flush=True)
                            last_progress = time.monotonic()
                    else:
                        print('Waiting for capture helper', flush=True)
                time.sleep(1)
    finally:
        kernel.CloseHandle(handle)


if __name__ == '__main__':
    main()
