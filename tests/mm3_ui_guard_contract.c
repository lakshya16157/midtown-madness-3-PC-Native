/* The screen-widget link guard (src/mm3_ui_guard.c), run on the widget draw
 * hook against a fake guest: widget+0 vtable, +4 next sibling, +8 first child;
 * a live vtable's +0x54 is game code, a destroyed widget's base vtable has the
 * pure-call stub there. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "recomp_types.h"

ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi, g_ebp, g_seh_ebp;
uint32_t g_xbox_code_lo = 0x00012000u, g_xbox_code_hi = 0x00259180u;
static unsigned char memory[0x400000];
static unsigned drawn;
static uint32_t drawn_widget;

void sub_000C2BFB_original(void) { ++drawn; drawn_widget = g_ecx; }
void sub_000C2BFB(void);
static unsigned destroyed;
void sub_0011965D_original(void) { ++destroyed; }
void sub_0011965D(void);
void sub_001C3C2D_original(void) { assert(0); }   /* replaced, never called */
void sub_001C3C2D(void);
int mm3_heap_double_free(uint32_t heap, uint32_t caller, uint32_t pointer);
void mm3_unresolved_stub_reached(uint32_t va);

/* The leaf draw's calls: a leaf's own draw (+0x64 = 0x00150000) and a group
 * sibling's draw (+0x54 = C2BFB, stubbed). Each pops its return address and
 * five arguments, as thiscall ret 20. */
#define LEAF_DRAW_SELF 0x00150000u
static uint32_t leaf_self_seen, leaf_args_seen, leaf_turns_sibling;
static unsigned sibling_drawn;
static void leaf_draw_self(void)
{
    leaf_self_seen = g_ecx;
    leaf_args_seen = MEM32(g_esp + 4) == 0xA1u && MEM32(g_esp + 20) == 0xA5u && MEM32(g_esp) == 0x001C3C47u;
    if (leaf_turns_sibling) MEM32(leaf_turns_sibling) = 0x00384B30u;   /* freed and reused */
    g_esp += 24;
}
static void sibling_group_draw(void) { ++sibling_drawn; g_esp += 24; }
recomp_func_t recomp_lookup_manual(uint32_t va)
{
    if (va == LEAF_DRAW_SELF) return leaf_draw_self;
    if (va == 0x000C2BFBu) return sibling_group_draw;
    return 0;
}
recomp_func_t recomp_lookup(uint32_t va) { (void)va; return 0; }

#define LIVE 0x00370000u          /* in .rdata; +0x54 is a draw in .text */
#define BASE 0x00370100u          /* a destructor's base vtable: +0x54 is _purecall */
#define GARBAGE 0xC33E0000u       /* -190.0f, as the crash log's reused memory read */

static uint32_t widget(unsigned k, uint32_t vtable, uint32_t next, uint32_t child)
{
    uint32_t w = 0x00100000u + k * 0x40u;
    MEM32(w) = vtable; MEM32(w + 4) = next; MEM32(w + 8) = child;
    return w;
}

static void draw(uint32_t w)
{
    unsigned before = drawn;
    g_ecx = w;
    sub_000C2BFB();
    assert(drawn == before + 1 && drawn_widget == w);
}

/* The menu manager (0x3C5698) keeps id -> menu in a std::map: node +0 left,
 * +4 parent, +8 right, +0xC id, +0x10 menu, +0x15 "is the header". Menus keep
 * their screen at +0xC and the active flag at +4. */
#define MANAGER 0x00200000u
#define HEAD 0x00200100u
static uint32_t map_node(unsigned k, uint32_t id, uint32_t menu, uint32_t left, uint32_t right)
{
    uint32_t n = 0x00200200u + k * 0x20u;
    MEM32(n) = left; MEM32(n + 4) = HEAD; MEM32(n + 8) = right;
    MEM32(n + 0xC) = id; MEM32(n + 0x10) = menu; MEM8(n + 0x14) = 0; MEM8(n + 0x15) = 0;
    return n;
}

static uint32_t menu(unsigned k, uint32_t screen, int open)
{
    uint32_t m = 0x00210000u + k * 0x100u;
    MEM32(m) = 0x0037DBB0u; MEM8(m + 4) = (uint8_t)open; MEM32(m + 0xC) = screen;
    return m;
}

static void destroy_screen(uint32_t screen, uint32_t deleter)
{
    unsigned before = destroyed;
    g_esp = 0x0000FF00u;
    MEM32(g_esp) = 0x0006B254u;        /* in the deleting destructor */
    MEM32(g_esp + 4) = 0;              /* its saved ESI */
    MEM32(g_esp + 8) = deleter;        /* its caller */
    MEM32(g_esp + 12) = 1;
    g_ecx = screen;
    sub_0011965D();
    assert(destroyed == before + 1);
}

static void menu_screens(void)
{
    uint32_t results, other, idle, left, right, root;
    MEM32(0x003C5698u) = MANAGER;
    MEM32(MANAGER + 4) = HEAD;
    MEM8(HEAD + 0x15) = 1;
    results = menu(0, 0x00300000u, 1);
    other = menu(1, 0x00300100u, 1);
    idle = menu(2, 0, 0);
    left = map_node(1, 2, idle, HEAD, HEAD);
    right = map_node(2, 7, other, HEAD, HEAD);
    root = map_node(0, 5, results, left, right);
    MEM32(HEAD + 4) = root;
    /* A screen deleted behind its open menu's back: the menu lets it go and
     * closes, so its next open builds a new one; the other menus keep theirs. */
    destroy_screen(0x00300000u, 0x00123456u);
    assert(MEM32(results + 0xC) == 0 && MEM8(results + 4) == 0);
    assert(MEM32(other + 0xC) == 0x00300100u && MEM8(other + 4) == 1);
    assert(MEM32(idle + 0xC) == 0);
    /* Close deleting its own screen ends the same way. */
    destroy_screen(0x00300100u, 0x0011926Cu);
    assert(MEM32(other + 0xC) == 0 && MEM8(other + 4) == 0);
    /* A screen no menu holds changes nothing. */
    MEM32(results + 0xC) = 0x00300200u; MEM8(results + 4) = 1;
    destroy_screen(0x00300300u, 0x00123456u);
    assert(MEM32(results + 0xC) == 0x00300200u && MEM8(results + 4) == 1);
    puts("PASS menu screens: a destroyed screen is let go by the menu holding it; others unchanged");
}

int main(void)
{
    uint32_t parent, a, b, c, dead, reused;
    g_xbox_mem_offset = (ptrdiff_t)memory;
    MEM32(LIVE + 0x10) = 0x001C4014u; MEM32(LIVE + 0x14) = 0x001C4011u;   /* the widget base's */
    MEM32(LIVE + 0x54) = 0x000C2BFBu; MEM32(LIVE + 0x64) = 0x000C2D00u;
    MEM32(BASE + 0x10) = 0x001C4014u; MEM32(BASE + 0x14) = 0x001C4011u;
    MEM32(BASE + 0x54) = 0x00093C8Fu; MEM32(BASE + 0x64) = 0x00093C8Fu;

    /* Live links and empty ones are left as they are. */
    c = widget(3, LIVE, 0, 0);
    b = widget(2, LIVE, c, 0);
    a = widget(1, LIVE, 0, 0);
    parent = widget(0, LIVE, b, a);
    draw(parent);
    assert(MEM32(parent + 4) == b && MEM32(parent + 8) == a);
    draw(a);
    assert(MEM32(a + 4) == 0 && MEM32(a + 8) == 0);

    /* A destroyed next sibling (base vtable, memory not reused): linked past
     * it to its own next sibling, so the widgets after it are still drawn. */
    dead = widget(4, BASE, b, 0);
    parent = widget(0, LIVE, dead, a);
    draw(parent);
    assert(MEM32(parent + 4) == b && MEM32(parent + 8) == a);
    /* The same for a destroyed first child. */
    parent = widget(0, LIVE, 0, dead);
    draw(parent);
    assert(MEM32(parent + 8) == b && MEM32(parent + 4) == 0);

    /* Memory reused for other data: not an address, or no vtable. Cut. */
    parent = widget(0, LIVE, GARBAGE, a);
    draw(parent);
    assert(MEM32(parent + 4) == 0 && MEM32(parent + 8) == a);
    reused = widget(5, 0x12345678u, b, 0);
    parent = widget(0, LIVE, reused, reused);
    draw(parent);
    assert(MEM32(parent + 4) == 0 && MEM32(parent + 8) == 0);
    /* A live vtable whose draw entry is not game code is not a widget. */
    MEM32(LIVE + 0x200 + 0x10) = 0x001C4014u; MEM32(LIVE + 0x200 + 0x14) = 0x001C4011u;
    MEM32(LIVE + 0x200 + 0x54) = 0x00400000u;
    reused = widget(5, LIVE + 0x200, b, 0);
    parent = widget(0, LIVE, reused, 0);
    draw(parent);
    assert(MEM32(parent + 4) == 0);
    /* Nor is another class whose +0x54 is game code (replayed 10-23: vtable
     * 384B30, +0x54 sub_00217B7E stores 0.0 through two pointers). */
    MEM32(0x00384B30u + 0x10) = 0x0021581Du; MEM32(0x00384B30u + 0x14) = 0x00015DF4u;
    MEM32(0x00384B30u + 0x54) = 0x00217B7Eu;
    reused = widget(5, 0x00384B30u, b, 0);
    parent = widget(0, LIVE, reused, 0);
    draw(parent);
    assert(MEM32(parent + 4) == 0);

    /* A destroyed widget whose next sibling would lead back round (to the
     * widget drawn or to itself) is cut rather than relinked into a loop. */
    parent = widget(0, LIVE, 0, 0);
    b = widget(2, LIVE, parent, 0);
    dead = widget(4, BASE, b, 0);
    MEM32(parent + 4) = dead;
    draw(parent);
    assert(MEM32(parent + 4) == 0);
    dead = widget(4, BASE, 0, 0);
    MEM32(dead + 4) = dead;
    parent = widget(0, LIVE, dead, 0);
    draw(parent);
    assert(MEM32(parent + 4) == 0);
    /* Its next sibling must itself be live. */
    dead = widget(4, BASE, GARBAGE, 0);
    parent = widget(0, LIVE, dead, 0);
    draw(parent);
    assert(MEM32(parent + 4) == 0);

    /* The leaf draw: itself with the caller's five arguments, then its next
     * sibling with the same stack. A sibling that becomes another class's
     * object during the leaf's own draw is cut when the link is read. */
    {
        uint32_t leafvt = 0x00370300u, leaf = 0x00100400u, sibling = 0x00100440u, entry = 0x0000F000u, k;
        MEM32(leafvt + 0x10) = 0x001C4014u; MEM32(leafvt + 0x14) = 0x001C4011u;
        MEM32(leafvt + 0x54) = 0x001C3C2Du; MEM32(leafvt + 0x64) = LEAF_DRAW_SELF;
        for (k = 0; k < 2; ++k) {
            MEM32(sibling) = LIVE; MEM32(sibling + 4) = 0; MEM32(sibling + 8) = 0;
            MEM32(leaf) = leafvt; MEM32(leaf + 4) = sibling;
            g_esp = entry; MEM32(entry) = 0x000C2CB9u;
            MEM32(entry + 4) = 0xA1u; MEM32(entry + 20) = 0xA5u;
            g_esi = 0x5151u; g_ebp = 0xEBEBu;
            leaf_turns_sibling = k ? sibling : 0;
            sibling_drawn = 0;
            g_ecx = leaf; sub_001C3C2D();
            assert(leaf_self_seen == leaf && leaf_args_seen);
            assert(g_esp == entry + 24 && g_esi == 0x5151u && g_ebp == 0xEBEBu);
            assert(sibling_drawn == (k ? 0u : 1u));
            assert(MEM32(leaf + 4) == (k ? 0u : sibling));
        }
    }
    puts("PASS UI guard: live links kept, a destroyed widget linked past, reused memory, other classes and loops cut");
    menu_screens();
    {
        /* RtlFreeHeap's header: 16 bytes before the block, flags at +5 (0x01 busy). */
        uint32_t block = 0x00220010u;
        g_esp = 0x0000FE00u;
        MEM32(block - 0x10u) = 0x00030003u; MEM32(block - 0x0Cu) = 0x001C0102u;   /* busy: flags 01 at +5 */
        assert(!mm3_heap_double_free(0x00200000u, 0x00012345u, block));
        MEM8(block - 0x10u + 5u) = 0x00u;                                          /* now free */
        assert(mm3_heap_double_free(0x00200000u, 0x00012345u, block));
        assert(!mm3_heap_double_free(0x00200000u, 0x00012345u, 0));                /* free(NULL) */
        puts("PASS heap guard: a second free of a block is caught, live blocks and NULL pass");
    }
    /* Unresolved stubs report the first time each runs, once. */
    g_esp = 0x0000FE00u;
    mm3_unresolved_stub_reached(0x00241E85u);
    mm3_unresolved_stub_reached(0x00241E85u);
    mm3_unresolved_stub_reached(0x00012EA3u);
    mm3_unresolved_stub_reached(0x00241E85u);
    return 0;
}
