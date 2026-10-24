"""Keep a generated function's x87 state in locals (MM3_GEN_X87_LOCALS, run by
scripts/mm3_gen_register_cache.py after the register cache).

Generated code keeps the x87 stack, its top, the control word and the last
compare in thread-local storage. Every guest memory access is volatile, which
MSVC treats as a compiler barrier, so after each one every x87 operation
reloads them (and RECOMP_FP_PC reloads the control word) and stores its result
back to memory. Functions doing a lot of x87 work get locals instead:

    RECOMP_X87_ENTER   copy the eight slots, top, control word and compare
                       state from thread-local storage (the slots relative to
                       the top on entry, so the slot indices are constants the
                       compiler can keep in registers)
    RECOMP_X87_FLUSH   write them back: before every call and every return
    RECOMP_X87_RELOAD  read them again after every call

Inside such a function the names g_fp_top and g_fp_stack are made invalid, so
the build fails if any use of the thread-local state is left (also inside a
macro); the control word and compare names point at the locals. Calls are
RECOMP_ABI_CALL (the indirect-call macros expand to it, and it is redefined
around the function) or direct `sub_XXXXXXXX();` tail calls. A callee that
unwinds past the function (guest SEH) finds the state written back already.
The results are bit-identical: the same operations run on the same values.
"""
import re

FP_MIN = 16                  # x87 operations in the body to be worth it
PUSH_DEF = ("    #define fp_push(v) do { double _fp_value = (v); \\\n"
            "        g_fp_top = (g_fp_top + 7u) & 7u; \\\n"
            "        g_fp_stack[g_fp_top] = _fp_value; } while (0)\n")
POP_DEF = "    #define fp_pop() (g_fp_top = (g_fp_top + 1u) & 7u)\n"
TOP_DEF = "    #define fp_top() g_fp_stack[g_fp_top]\n"
ST_DEF = "    #define fp_st(i) g_fp_stack[(g_fp_top + (i)) & 7u]\n"
LOCAL_DEFS = (
    "    #define fp_push(v) do { double _fp_value = (v); \\\n"
    "        x87_rel = (x87_rel + 7) & 7; \\\n"
    "        x87_slot[x87_rel] = _fp_value; } while (0)\n",
    "    #define fp_pop() (x87_rel = (x87_rel + 1) & 7)\n",
    "    #define fp_top() x87_slot[x87_rel]\n",
    "    #define fp_st(i) x87_slot[(x87_rel + (i)) & 7]\n")
STACK_INDEX = re.compile(r"g_fp_stack\[\(g_fp_top \+ (\d+)\) & 7u?\]")
STATUS_TOP = "((g_fp_top & 7u) << 11)"
DIRECT_CALL = re.compile(r"\b(sub_[0-9A-F]{8}(?:_original)?)\(\);")
RETURN = re.compile(r"\breturn;")
FP_USE = re.compile(r"\bfp_(?:push|pop|top|st1?)\(")
FORBIDDEN = ("setjmp(", "longjmp(", "RECOMP_UNIMPL(")
# Anything a function calls besides guest functions must be one of these
# macros or pure helpers: a host call the hook seams insert (mm3_*, xbox_*)
# could reach guest code that reads the x87 state, so such a function keeps it
# thread-local.
CALL = re.compile(r"\b([A-Za-z_]\w*)\(")
PURE = re.compile(r"(?:sub_[0-9A-F]{8}(?:_original)?|fp_\w+|st|RECOMP_\w+|S?MEM\w*|XBOX_PTR|PUSH32|POP32|"
                  r"MMX_\w+|XMM_\w+|[SZ]X(?:8|16)|RO[LR]\d+|RC_ROT|(?:LO|HI)(?:8|16)|SET_\w+|CMP_\w+|TEST_\w+|"
                  r"memcpy|memset|fabsf?|sqrtf?|sin|cos|tan|atan2|log2|exp2|ldexp|fmod|floor|ceil|copysign|"
                  r"recomp_fist|recomp_frndint|recomp_fxam|if|while|for|switch|sizeof|return)")

ENTER = "    RECOMP_X87_ENTER\n"
REDEFINE = ("#undef g_fp_top\n#define g_fp_top RECOMP_X87_LOCAL_STATE_ONLY\n"
            "#undef g_fp_stack\n#define g_fp_stack RECOMP_X87_LOCAL_STATE_ONLY\n"
            "#undef g_fp_control_word\n#define g_fp_control_word x87_cw\n"
            "#undef g_fp_cmp\n#define g_fp_cmp x87_cmp\n"
            "#undef g_fp_cc\n#define g_fp_cc x87_cc\n"
            "#undef RECOMP_ABI_CALL\n#define RECOMP_ABI_CALL(va, fn) "
            "do { RECOMP_X87_FLUSH(); RECOMP_ABI_CALL_BASE(va, fn); RECOMP_X87_RELOAD(); } while (0)\n")
RESTORE = ("#undef g_fp_top\n#define g_fp_top (*recomp_p_g_fp_top)\n"
           "#undef g_fp_stack\n#define g_fp_stack (recomp_p_g_fp_stack)\n"
           "#undef g_fp_control_word\n#define g_fp_control_word (*recomp_p_g_fp_control_word)\n"
           "#undef g_fp_cmp\n#define g_fp_cmp (*recomp_p_g_fp_cmp)\n"
           "#undef g_fp_cc\n#define g_fp_cc (*recomp_p_g_fp_cc)\n"
           "#undef RECOMP_ABI_CALL\n#define RECOMP_ABI_CALL(va, fn) RECOMP_ABI_CALL_BASE(va, fn)\n")


def eligible(body):
    if any(token in body for token in FORBIDDEN):
        return False
    if any(not PURE.fullmatch(name) for name in set(CALL.findall(body))):
        return False
    for definition in (PUSH_DEF, POP_DEF, TOP_DEF, ST_DEF):
        if body.count(definition) != 1:
            return False
    return len(FP_USE.findall(body)) - 6 >= FP_MIN


def transform(head, body):
    """head: 'void sub_X(void)\\n{\\n    RECOMP_GEN_PROLOGUE\\n'; body: the rest of
    the function through its closing brace. Returns the new text, or None."""
    if not eligible(body):
        return None
    for old, new in zip((PUSH_DEF, POP_DEF, TOP_DEF, ST_DEF), LOCAL_DEFS):
        body = body.replace(old, new)
    body = STACK_INDEX.sub(lambda m: f"x87_slot[(x87_rel + {m.group(1)}) & 7]", body)
    body = body.replace("g_fp_stack[g_fp_top]", "x87_slot[x87_rel]")
    body = body.replace(STATUS_TOP, "(((x87_base + x87_rel) & 7u) << 11)")
    if "g_fp_top" in body or "g_fp_stack" in body:
        return None
    body = DIRECT_CALL.sub(lambda m: f"{{ RECOMP_X87_FLUSH(); {m.group(1)}(); RECOMP_X87_RELOAD(); }}", body)
    body = RETURN.sub("{ RECOMP_X87_FLUSH(); return; }", body)
    return REDEFINE + head + ENTER + body + RESTORE
