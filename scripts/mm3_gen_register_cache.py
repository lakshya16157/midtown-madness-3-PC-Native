"""Give each generated function its register/base cache (CMakeLists.txt,
MM3_GEN_REGISTER_CACHE): RECOMP_GEN_PROLOGUE as its first line, see
src/mm3_recomp_types.h.

Data the recompiler decoded as code (strings read as `outsd`, `arpl`, ... -
dozens of overlapping 4,000-12,000-line bodies around 0x33C000) is never run,
but with the cache MSVC takes minutes on each such body instead of seconds.
Those keep the thread-local registers: the cache is switched off around them
(mm3_gen_cache_off.h / mm3_gen_cache_on.h). They are recognised by
RECOMP_UNIMPL lines, which real game code has at most three of (the CPUID
probes), or by runs of zero bytes read as `add [eax], al`.

    python mm3_gen_register_cache.py <input.c> <output.c> [--x87-locals]

With --x87-locals (MM3_GEN_X87_LOCALS), functions doing a lot of x87 work
also keep their x87 state in locals (scripts/mm3_gen_x87_locals.py).
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import mm3_gen_x87_locals  # noqa: E402

FUNCTION = re.compile(r"(?ms)^void (\w+)\(void\)\n\{\n(.*?)^\}\n")
JUNK_UNIMPL = 4
ZERO_PADDING = "MEM8(eax) = MEM8(eax) + LO8(eax);"   # bytes 00 00
JUNK_PADDING = 32


def transform(text, x87_locals=False):
    out, last, cached, skipped, local = [], 0, 0, 0, 0
    for m in FUNCTION.finditer(text):
        out.append(text[last:m.start()])
        body = m.group(0)
        if (m.group(2).count("RECOMP_UNIMPL(") >= JUNK_UNIMPL or
                m.group(2).count(ZERO_PADDING) >= JUNK_PADDING):
            out.append('#include "mm3_gen_cache_off.h"\n' + body + '#include "mm3_gen_cache_on.h"\n')
            skipped += 1
        else:
            head = f"void {m.group(1)}(void)\n{{\n"
            changed = (mm3_gen_x87_locals.transform(head + "    RECOMP_GEN_PROLOGUE\n", body[len(head):])
                       if x87_locals else None)
            if changed:
                out.append(changed)
                local += 1
            else:
                out.append(head + "    RECOMP_GEN_PROLOGUE\n" + body[len(head):])
            cached += 1
        last = m.end()
    out.append(text[last:])
    return "".join(out), cached, skipped, local


def main():
    source, target = Path(sys.argv[1]), Path(sys.argv[2])
    with open(source, encoding="utf-8", errors="surrogateescape", newline="") as f:
        raw = f.read()
    # Keep the file's own line endings (the generated chunks are CRLF).
    crlf = "\r\n" in raw
    text, cached, skipped, local = transform(raw.replace("\r\n", "\n") if crlf else raw,
                                             "--x87-locals" in sys.argv[3:])
    with open(target, "w", encoding="utf-8", errors="surrogateescape", newline="") as f:
        f.write(text.replace("\n", "\r\n") if crlf else text)
    print(f"{source.name}: {cached} functions cached ({local} with x87 locals), "
          f"{skipped} left on thread-local registers")


if __name__ == "__main__":
    main()
