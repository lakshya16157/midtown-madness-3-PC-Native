"""MM3_HEAP_LIST_WATCH (src/mm3_heap_list_watch.c) finds a damaged link on the heap's large free list.

The contract builds a fake guest heap with three free chunks on the list at heap + 0x180, damages one
forward link at a time (zero, the misaligned 04026DFC of the 2026-11-13 crash, a float past guest
memory) and checks the right node is named and nothing is written; then that the report says so."""
from pathlib import Path
import os
import subprocess
import tempfile
import cc

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="mm3-heap-list-") as temporary:
    folder = Path(temporary)
    executable = folder / ("contract" + cc.exe_suffix())
    # POSIX has no CreateThread/Sleep/GetTickCount64: the compat layer provides them.
    sources = [ROOT / "tests/mm3_heap_list_watch_contract.c"]
    if os.name != "nt":
        sources.append(ROOT / "tools/xboxrecomp/src/platform/win32_compat.c")
    subprocess.run(cc.compile_command(
        sources, executable,
        includes=[ROOT / "tools/xboxrecomp/templates/runtime", ROOT / "tools/xboxrecomp/src", ROOT / "tools/xboxrecomp/src/kernel", ROOT / "tools/xboxrecomp/src/video", ROOT / "tools/xboxrecomp/include"],
        warnings="none", obj=folder / "contract.obj", gcc_extra=["-lm"]), check=True, cwd=folder)
    done = subprocess.run([str(executable)], check=True, cwd=folder, capture_output=True, text=True)
    print(done.stdout.strip())
    log = done.stderr
    assert ("[HEAP_LIST] heap 00200000: free chunk 00224000 (768 units) links forward to 3DCCCCCD, not a node "
            "(back 00222008, list neighbour before it 00222008, item 2, 12 ms after the watch started)") in log, log
    assert "[HEAP_LIST] chunk 00224000: 02000300 00000002 3DCCCCCD 00222008" in log, log
    assert "[HEAP_LIST] the chunk before it (by its size field) 00222000: 01000200" in log, log
print("PASS: MM3_HEAP_LIST_WATCH names the damaged node and dumps it")
