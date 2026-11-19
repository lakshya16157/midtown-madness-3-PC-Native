"""Keep a generated function's integer registers in locals (MM3_GEN_GPR_LOCALS,
run by scripts/mm3_gen_register_cache.py after the register cache and the x87
locals).

Generated code keeps eax..edi and esp in thread-local storage. Every guest
memory access is volatile, which MSVC treats as a compiler barrier, so after
each one every register is read from memory again and every result is stored
back: a guest `mov eax, [ecx+8]` costs a load of ecx, the guest load, and a
store of eax. With locals whose address is never taken the compiler keeps the
registers in host registers across those barriers. Per function:

    RECOMP_GPR_ENTER   copy the registers the function uses from thread-local
                       storage
    RECOMP_GPR_FLUSH   write back the ones it changes: before every call and
                       every return
    RECOMP_GPR_RELOAD  read the used ones again after every call

Only call-free functions of 20+ lines are transformed (see MIN_LINES): with
calls, the write-back and reload around each one cost more than the locals
saved. The call handling below (RECOMP_ABI_CALL redefined, direct
`sub_XXXXXXXX();` tail calls wrapped) stays for when that rule is relaxed. A
function that calls anything else (a host call a hook seam inserted could read
the thread-local registers) keeps them thread-local, as do setjmp/longjmp users.
The indirect-call macros write esp and eax (a failed lookup) and ebx/esi/edi
(after a kernel thunk) inside their expansion, so with any of them every
register counts as used and changed. ebp stays as the generator wrote it (a
local in framed functions, published to g_ebp before calls). Results are
bit-identical: the same operations run on the same values
(tests/mm3_gen_x87_locals_regression.py --gpr compares them).
"""
import re

import mm3_gen_x87_locals as x87

REGS = ("eax", "ecx", "edx", "ebx", "esi", "edi", "esp")
NAME = r"(?<!\w)(?:g_)?(eax|ecx|edx|ebx|esi|edi|esp)\b"
USE = re.compile(NAME)
WRITES = (
    re.compile(NAME + r"\s*(?:[-+*/%&|^]|<<|>>)?=(?!=)"),
    re.compile(r"(?:\+\+|--)\s*" + NAME),
    re.compile(NAME + r"\s*(?:\+\+|--)"),
    re.compile(r"\b(?:SET_\w+|PUSH32|POP32)\(\s*" + NAME),
    re.compile(r"\bPOP32\([^,()]*,\s*" + NAME),
)
WRITES_ALL = ("RECOMP_ICALL", "RECOMP_ITAIL")
DIRECT_CALL = re.compile(r"\b(sub_[0-9A-F]{8}(?:_original)?)\(\);")
RETURN = re.compile(r"\breturn;")
CALL = re.compile(r"\b([A-Za-z_]\w*)\(")
FORBIDDEN = ("setjmp(", "longjmp(", "RECOMP_UNIMPL(", "recomp_p_g_e", "gr_e")
# Calls besides guest functions: the x87 transform's list of macros and pure
# helpers (none of them reads or writes the thread-local registers).
PURE = x87.PURE
MAX_LINES = 4000             # very large bodies only cost compile time
# Only call-free functions of some size: measured with the harness's --bench
# on 178 sampled functions (11-19), call-free bodies of 20+ lines ran 0.81x the
# time in total (0.84x geometric mean), while functions with calls ran 1.18x:
# writing every changed register back and reading them all again around each
# call cost more than the locals saved.
MIN_LINES = 20
# Registers kept in locals in functions that make calls (written back and read
# again around each call). Empty: such functions are left alone.
CALL_REGS = tuple(r for r in __import__("os").environ.get("MM3_GPR_CALL_REGS", "").split(",") if r in
                  ("eax", "ecx", "edx", "ebx", "esi", "edi", "esp"))
CALLS = re.compile(r"RECOMP_ABI_CALL|RECOMP_ICALL|RECOMP_ITAIL|\bsub_[0-9A-F]{8}(?:_original)?\(\);")

ENTER = "    RECOMP_GPR_ENTER\n"
X87_CALL = ("#define RECOMP_ABI_CALL(va, fn) "
            "do { RECOMP_X87_FLUSH(); RECOMP_ABI_CALL_BASE(va, fn); RECOMP_X87_RELOAD(); } while (0)\n")
BOTH_CALL = ("#define RECOMP_ABI_CALL(va, fn) "
             "do { RECOMP_X87_FLUSH(); RECOMP_GPR_FLUSH(); RECOMP_ABI_CALL_BASE(va, fn); "
             "RECOMP_GPR_RELOAD(); RECOMP_X87_RELOAD(); } while (0)\n")
GPR_CALL = ("#undef RECOMP_ABI_CALL\n#define RECOMP_ABI_CALL(va, fn) "
            "do { RECOMP_GPR_FLUSH(); RECOMP_ABI_CALL_BASE(va, fn); RECOMP_GPR_RELOAD(); } while (0)\n")


def registers(body):
    """(used, changed) register names, or None when the function is not eligible."""
    if any(token in body for token in FORBIDDEN) or not MIN_LINES <= body.count("\n") <= MAX_LINES:
        return None
    if CALLS.search(body):
        if not CALL_REGS:
            return None
        used = set(USE.findall(body)) & set(CALL_REGS)
        changed = set()
        for pattern in WRITES:
            changed.update(pattern.findall(body))
        if any(token in body for token in WRITES_ALL):
            changed |= used
        order = lambda names: tuple(r for r in REGS if r in names)
        return order(used), order(changed & used)
    if not all(PURE.fullmatch(name) for name in set(CALL.findall(body))):
        return None
    if any(token in body for token in WRITES_ALL):
        return REGS, REGS
    used = set(USE.findall(body))
    changed = set()
    for pattern in WRITES:
        changed.update(pattern.findall(body))
    order = lambda names: tuple(r for r in REGS if r in names)
    return order(used), order(changed & used)


def definitions(used, changed):
    enter = ", ".join(f"gr_{r} = *recomp_p_g_{r}" for r in used)
    flush = " ".join(f"*recomp_p_g_{r} = gr_{r};" for r in changed)
    reload = " ".join(f"gr_{r} = *recomp_p_g_{r};" for r in used)
    return ("".join(f"#undef g_{r}\n#define g_{r} gr_{r}\n" for r in used) +
            f"#define RECOMP_GPR_ENTER uint32_t {enter};\n"
            f"#define RECOMP_GPR_FLUSH() do {{ {flush} }} while (0)\n"
            f"#define RECOMP_GPR_RELOAD() do {{ {reload} }} while (0)\n")


def restore(used):
    return ("".join(f"#undef g_{r}\n#define g_{r} (*recomp_p_g_{r})\n" for r in used) +
            "#undef RECOMP_GPR_ENTER\n#undef RECOMP_GPR_FLUSH\n#undef RECOMP_GPR_RELOAD\n"
            "#undef RECOMP_ABI_CALL\n#define RECOMP_ABI_CALL(va, fn) RECOMP_ABI_CALL_BASE(va, fn)\n")


def transform(head, body, with_x87=False):
    """head: 'void sub_X(void)\\n{\\n    RECOMP_GEN_PROLOGUE\\n' (plus
    '    RECOMP_X87_ENTER\\n' with_x87); body: the rest through the closing brace
    (already x87-transformed with_x87). Returns the new text, or None."""
    found = registers(body)
    if not found or not found[0]:
        return None
    used, changed = found
    body = DIRECT_CALL.sub(lambda m: f"{{ RECOMP_GPR_FLUSH(); {m.group(1)}(); RECOMP_GPR_RELOAD(); }}", body)
    body = RETURN.sub("{ RECOMP_GPR_FLUSH(); return; }", body)
    if not with_x87:
        return definitions(used, changed) + GPR_CALL + head + ENTER + body + restore(used)
    assert x87.REDEFINE.count(X87_CALL) == 1
    return (definitions(used, changed) + x87.REDEFINE.replace(X87_CALL, BOTH_CALL) + head + ENTER +
            body + x87.RESTORE + restore(used))


def split_x87(text, head):
    """The x87 transform's output for head -> (head + ENTER, its body), or None."""
    start = x87.REDEFINE + head + x87.ENTER
    if not (text.startswith(start) and text.endswith(x87.RESTORE)):
        return None
    return head + x87.ENTER, text[len(start):len(text) - len(x87.RESTORE)]
