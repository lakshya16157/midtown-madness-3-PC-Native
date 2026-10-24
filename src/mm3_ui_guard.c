#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp_types.h"

/* Screen widgets (menus, HUD, results) form a tree: widget+4 is the next
 * sibling, +8 the first child, +0xC/+0x10 its offset. sub_000C2BFB draws a
 * widget (vtable +0x64), then its first child and its next sibling, each
 * through their vtable +0x54. At the end of a circuit race (log
 * direct-20261023-141159, Washington 28, results screen) a sibling link led to
 * nothing: the widget's next-sibling field held -190.0f, later -3.0f (a float
 * written over it), and "R6025 pure virtual function call" came every frame
 * until the game crashed.
 *
 * Before a widget is drawn, each of its two links must lead to a widget whose
 * vtable lies in .rdata and whose +0x54 entry is game code other than the
 * pure-call stub (a destructor leaves the base vtable behind). A link that
 * does not is replaced, and logged as [MM3_UI_GUARD] with the addresses: by
 * the dead widget's own next sibling while its memory still holds the base
 * vtable (destroyed, not yet reused) and that sibling is live and leads back to
 * neither widget, so the widgets after it are still drawn; otherwise by none.
 * The rest of the tree is drawn and the game keeps running. Nothing else
 * changes.
 *
 * The first time a link is found overwritten (rather than leading to a
 * destroyed widget), that link's slot gets a hardware write watch in every
 * thread (debug register 1; mm3_frontend_watch.c keeps 0). Whatever writes
 * there next logs itself as [MM3_UI_WATCH]: the value, the writing code's
 * host address and return addresses (symbolize with the build's PDB) and the
 * game-code addresses on the guest stack. MM3_UI_WATCH=0 turns the watch off. */
extern void sub_000C2BFB_original(void);

#define RDATA_LO 0x00361F00u
#define RDATA_HI 0x00391EECu
#define TEXT_LO 0x00012000u
#define TEXT_HI 0x00259180u
#define PURE_CALL 0x00093C8Fu      /* _purecall: R6025 */
#define GUARD_LINES 40u

static unsigned s_lines;

static int widget_address(uint32_t widget)
{
    return !(widget & 3u) && widget >= 0x10000u && widget <= 0x07FFFF00u;
}

static int vtable_address(uint32_t vtable)
{
    return !(vtable & 3u) && vtable >= RDATA_LO && vtable <= RDATA_HI - 0x68u;
}

/* Every widget class (218 vtables, screens included) keeps the base class's
 * +0x10 and +0x14 entries. A link that leads to another kind of object passes
 * the other checks: replayed 10-23, a results widget's sibling link led to an
 * object (vtable 384Bxx) whose +0x54 is sub_00217B7E, which stores 0.0 through
 * its two arguments; called as a draw, it wrote 0 over the menu screen's
 * vtable, and the results and post-race menu never appeared. */
#define WIDGET_BASE_10 0x001C4014u
#define WIDGET_BASE_14 0x001C4011u

static int widget_valid(uint32_t widget)
{
    uint32_t vtable, draw;
    if (!widget) return 1;
    if (!widget_address(widget)) return 0;
    vtable = MEM32(widget);
    if (!vtable_address(vtable)) return 0;
    if (MEM32(vtable + 0x10u) != WIDGET_BASE_10 || MEM32(vtable + 0x14u) != WIDGET_BASE_14) return 0;
    draw = MEM32(vtable + 0x54u);
    return draw != PURE_CALL && draw >= TEXT_LO && draw < TEXT_HI;
}

/* A destroyed widget's next sibling, when it can stand in for it: a live
 * widget whose own sibling chain (as far as GUARD_CHAIN) reaches neither the
 * widget being drawn nor the dead one, so no loop is made. Else 0. */
#define GUARD_CHAIN 256u
static uint32_t dead_widget_next(uint32_t widget, uint32_t dead)
{
    uint32_t vtable = MEM32(dead), next = MEM32(dead + 4u), at = next, i;
    if (!vtable_address(vtable) || MEM32(vtable + 0x54u) != PURE_CALL || !next || !widget_valid(next))
        return 0;
    for (i = 0; at && i < GUARD_CHAIN; ++i) {
        if (at == widget || at == dead || !widget_address(at)) return 0;
        at = MEM32(at + 4u);
    }
    return at ? 0 : next;
}

#define WATCH_LINES 16u
static uint32_t s_watch_guest;
static volatile LONG s_watch_hits;
static RECOMP_TLS volatile int s_guard_writing;   /* the guard's own repair is not reported */

static LONG CALLBACK watch_exception(EXCEPTION_POINTERS *p)
{
    CONTEXT *c = p->ContextRecord;
    LONG hit;
    if (p->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP || !(c->Dr6 & 2u))
        return EXCEPTION_CONTINUE_SEARCH;
    c->Dr6 &= ~2ull;
    if (s_guard_writing) return EXCEPTION_CONTINUE_EXECUTION;
    hit = InterlockedIncrement(&s_watch_hits);
    if (hit <= (LONG)WATCH_LINES) {
        uintptr_t base = (uintptr_t)GetModuleHandleW(NULL), frame;
        void *frames[12];
        USHORT n = RtlCaptureStackBackTrace(0, 12, frames, NULL), i;
        uint32_t esp = g_esp;
        char text[768];
        int at = snprintf(text, sizeof text, "[MM3_UI_WATCH] %08X = %08X written at rva %llX thread %lu; host",
                          s_watch_guest, MEM32(s_watch_guest), (unsigned long long)(c->Rip - base),
                          GetCurrentThreadId());
        for (i = 0; i < n && at < (int)sizeof text - 24; ++i) {
            frame = (uintptr_t)frames[i];
            if (frame > base && frame - base < 0x10000000u)
                at += snprintf(text + at, sizeof text - (size_t)at, " %llX", (unsigned long long)(frame - base));
        }
        if (widget_address(esp) && at < (int)sizeof text - 24) {
            at += snprintf(text + at, sizeof text - (size_t)at, "; guest esp %08X", esp);
            for (i = 0; i < 32 && at < (int)sizeof text - 12; ++i) {
                uint32_t word = MEM32(esp + 4u * i);
                if (word >= TEXT_LO && word < TEXT_HI)
                    at += snprintf(text + at, sizeof text - (size_t)at, " %08X", word);
            }
        }
        fprintf(stderr, "%s\n", text);
    }
    if (hit >= (LONG)WATCH_LINES) c->Dr7 &= ~0xCull;   /* slot 1 off in this thread */
    return EXCEPTION_CONTINUE_EXECUTION;
}

/* Runs on its own thread: each other thread is suspended while its slot 1 is
 * set (a thread cannot set its own debug registers this way). */
static DWORD WINAPI watch_arm(void *address)
{
    DWORD self = GetCurrentThreadId(), process = GetCurrentProcessId(), armed = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 entry;
    BOOL more;
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    entry.dwSize = sizeof entry;
    for (more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) {
        HANDLE thread;
        if (entry.th32OwnerProcessID != process || entry.th32ThreadID == self) continue;
        thread = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE,
                            entry.th32ThreadID);
        if (!thread) continue;
        if (SuspendThread(thread) != (DWORD)-1) {
            CONTEXT context;
            memset(&context, 0, sizeof context);
            context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (GetThreadContext(thread, &context)) {
                context.Dr1 = (DWORD64)(uintptr_t)address;
                context.Dr7 = (context.Dr7 & ~0xF0000Cull) | 0xD00004ull;   /* L1, on write, 4 bytes */
                armed += SetThreadContext(thread, &context) != 0;
            }
            ResumeThread(thread);
        }
        CloseHandle(thread);
    }
    CloseHandle(snapshot);
    return armed;
}

static void watch_slot(uint32_t guest)
{
    static int done;
    const char *option = getenv("MM3_UI_WATCH");
    HANDLE thread;
    DWORD armed = 0;
    if (done || (option && !strcmp(option, "0"))) return;
    done = 1;
    s_watch_guest = guest;
    if (!AddVectoredExceptionHandler(1, watch_exception)) return;
    thread = CreateThread(NULL, 0, watch_arm, (void *)XBOX_PTR(guest), 0, NULL);
    if (!thread) return;
    WaitForSingleObject(thread, 5000);
    GetExitCodeThread(thread, &armed);
    CloseHandle(thread);
    fprintf(stderr, "[MM3_UI_WATCH] watching %08X for writes in %lu threads\n", guest, armed);
}

static void check_link(uint32_t widget, uint32_t offset, const char *what)
{
    uint32_t link = MEM32(widget + offset), next;
    if (widget_valid(link)) return;
    next = widget_address(link) ? dead_widget_next(widget, link) : 0;
    if (s_lines < GUARD_LINES) {
        ++s_lines;
        char outcome[40] = "link cut";
        if (next) snprintf(outcome, sizeof outcome, "linked past it to %08X", next);
        fprintf(stderr, "[MM3_UI_GUARD] widget %08X (vtable %08X): %s %08X is not a live widget (vtable %08X); %s\n",
                widget, MEM32(widget), what, link, widget_address(link) ? MEM32(link) : 0u, outcome);
    }
    s_guard_writing = 1;
    MEM32(widget + offset) = next;
    s_guard_writing = 0;
    if (!next) watch_slot(widget + offset);
}

/* Menus (base class 37D218) own a screen (class 37D2C0, built only by
 * sub_0011935B) at +0xC. Open (sub_00119231) builds one when +0xC is 0 and
 * sets the active flag +4; close (sub_00119250) deletes it, clears +0xC and
 * clears the flag. The menu manager (sub_0011E76F's singleton, a std::map of
 * id -> menu at +4) updates the screen of every active menu each frame
 * (sub_0011E7D7).
 *
 * After a second circuit race in one session (Paris 21, then Washington 28;
 * reproduced 10-23) the post-race menu, id 5 (class 37DBB0), was active with
 * a screen that had been deleted some other way. Its text could not be laid
 * out (calls through the freed screen), so neither the results nor the
 * post-race menu appeared, and the freed screen's later writes landed in
 * widgets (the corrupted link the guard above cuts, then the crash).
 *
 * When a screen is destroyed, a menu still holding it now lets it go as its own
 * close would: +0xC and the active flag cleared, so the next open builds a new
 * screen. Deletions by close itself are left alone; any other is logged as
 * [MM3_MENU_SCREEN] with the game code returned to. */
extern void sub_0011965D_original(void);

#define MENU_MANAGER 0x003C5698u
#define CLOSE_RETURN 0x0011926Cu   /* sub_00119250 deleting its own screen */
#define MAP_NODES 256u

static unsigned s_screen_lines;

static void release_menu_screen(uint32_t screen)
{
    uint32_t manager = MEM32(MENU_MANAGER), head, node, stack[64];
    unsigned depth = 0, visited = 0;
    /* At the destructor: [esp] = 6B254 in the deleting destructor, which
     * saved ESI; above that its own return address. */
    uint32_t deleter = MEM32(g_esp + 8u);
    if (!widget_address(manager)) return;
    head = MEM32(manager + 4u);
    if (!widget_address(head)) return;
    stack[depth++] = MEM32(head + 4u);   /* the map's root */
    while (depth && visited < MAP_NODES) {
        uint32_t menu;
        node = stack[--depth];
        if (!widget_address(node) || node == head || MEM8(node + 0x15u)) continue;
        ++visited;
        menu = MEM32(node + 0x10u);
        if (widget_address(menu) && MEM32(menu + 0xCu) == screen) {
            if (deleter != CLOSE_RETURN && s_screen_lines < GUARD_LINES) {
                unsigned i, found = 0;
                char text[256];
                int at = snprintf(text, sizeof text,
                                  "[MM3_MENU_SCREEN] menu id %u (%08X, vtable %08X, %s) lost its screen %08X; "
                                  "menu closed. Deleted from", MEM32(node + 0xCu), menu, MEM32(menu),
                                  MEM8(menu + 4u) ? "open" : "closed", screen);
                for (i = 2; i < 40 && found < 6 && at < (int)sizeof text - 12; ++i) {
                    uint32_t word = MEM32(g_esp + 4u * i);
                    if (word >= TEXT_LO && word < TEXT_HI) {
                        at += snprintf(text + at, sizeof text - (size_t)at, " %08X", word);
                        ++found;
                    }
                }
                ++s_screen_lines;
                fprintf(stderr, "%s\n", text);
            }
            MEM32(menu + 0xCu) = 0;
            MEM8(menu + 4u) = 0;
        }
        if (depth + 2u <= sizeof stack / sizeof stack[0]) {
            stack[depth++] = MEM32(node);
            stack[depth++] = MEM32(node + 8u);
        }
    }
}

void sub_0011965D(void)
{
    release_menu_screen(g_ecx);
    sub_0011965D_original();
}

/* Diagnostic: MM3_UI_WATCH_ADDR=<hex guest address> watches that address
 * from the first frame it holds a live menu screen (its vtable word). */
static uint32_t watch_address_option(void)
{
    static int loaded;
    static uint32_t address;
    if (!loaded) {
        const char *value = getenv("MM3_UI_WATCH_ADDR");
        if (value && *value) address = (uint32_t)strtoul(value, NULL, 16);
        loaded = 1;
    }
    return address;
}

/* Leaf widgets (60 classes) draw with sub_001C3C2D (thiscall, 5 stack
 * arguments, ret 20): themselves through vtable +0x64, then a tail jump to
 * their next sibling's +0x54 with the same arguments. In the replays the
 * foreign object hung off a leaf's sibling link that was still a widget when
 * the leaf began drawing: its own draw freed the sibling and the memory was
 * reused, and the original reads the link only afterwards. So the leaf draw is
 * done here, instruction for instruction (frame, registers, return address
 * 0x001C3C47), with the link checked when it is read. */
static void guest_call(uint32_t va, uint32_t argument_bytes)
{
    recomp_func_t fn = 0;
    if (RECOMP_ICALL_IS_CODE(va)) {
        fn = recomp_lookup_manual(va);
        if (!fn) fn = recomp_lookup(va);
    }
    if (fn) fn();
    else g_esp += 4u + argument_bytes;   /* what the callee would have popped */
}

static unsigned s_abi_lines;

void sub_001C3C2D(void)
{
    uint32_t self = g_ecx, entry = g_esp, sibling, ebx = g_ebx, edi = g_edi, draw;
    int i;
    PUSH32(g_esp, g_ebp);                    /* push ebp; mov ebp, esp */
    g_ebp = g_seh_ebp = g_esp;
    PUSH32(g_esp, g_esi);                    /* push esi */
    for (i = 4; i >= 0; --i) PUSH32(g_esp, MEM32(entry + 4u + 4u * (uint32_t)i));
    g_esi = self;
    g_ecx = self;
    PUSH32(g_esp, 0x001C3C47u);
    draw = MEM32(MEM32(self) + 0x64u);
    guest_call(draw, 20u);
    /* The leaf's own draw must keep ESI/EBX/EDI (the original reads the
     * sibling through ESI afterwards); report one that does not. */
    if ((g_esi != self || g_ebx != ebx || g_edi != edi) && s_abi_lines < GUARD_LINES) {
        ++s_abi_lines;
        fprintf(stderr, "[MM3_UI_ABI] widget %08X (vtable %08X) draw %08X returned esi %08X ebx %08X edi %08X; "
                "expected %08X %08X %08X\n", self, MEM32(self), draw, g_esi, g_ebx, g_edi, self, ebx, edi);
    }
    POP32(g_esp, g_esi);
    POP32(g_esp, g_ebp);
    g_seh_ebp = g_ebp;
    sibling = MEM32(self + 4u);
    if (sibling && !widget_valid(sibling)) {
        check_link(self, 4u, "next sibling (after its draw)");
        sibling = MEM32(self + 4u);
    }
    if (!sibling) {
        g_esp += 24u;                        /* ret 20 */
        return;
    }
    g_ecx = sibling;
    guest_call(MEM32(MEM32(sibling) + 0x54u), 20u);   /* jmp: same return address and arguments */
}

void sub_000C2BFB(void)
{
    uint32_t widget = g_ecx, watch = watch_address_option();
    if (watch && widget_address(watch) && MEM32(watch) == 0x0037D2C0u) watch_slot(watch);
    if (widget_address(widget)) {
        check_link(widget, 8u, "first child");
        check_link(widget, 4u, "next sibling");
    }
    sub_000C2BFB_original();
}
