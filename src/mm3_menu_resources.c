#include <stdio.h>
#include <string.h>
#include "recomp_types.h"
#include "mm3_mod_flags.h"
#include "mm3_menu_resources.h"

extern void sub_00119AEE_original(void);
extern void sub_00102168_original(void);
extern void sub_00102191_original(void);
extern void sub_001021B5_original(void);
extern void sub_0006EA22_original(void);
extern void sub_0016FF04(void);

/* Use the title's normal binary stream type and its typed/string readers.
 * Only the byte source changes. Its 16-byte guest allocation retains the
 * original ownership and destructor; embedded resource bytes stay on the host.
 * +8 normally holds a guest file object. Our reserved high values are indices,
 * never pointers or host addresses placed into Xbox memory. */
#define MENU_STREAM_VTABLE 0x003791F0u
#define MENU_STREAM_TAG 0xFFFFFF00u

static const MM3MenuResource *stream_resource(uint32_t object)
{
    uint32_t tag;
    if (MEM32(object) != MENU_STREAM_VTABLE) return NULL;
    tag = MEM32(object + 8u);
    if (tag < MENU_STREAM_TAG || tag - MENU_STREAM_TAG >= MM3_MENU_RESOURCE_COUNT)
        return NULL;
    return &mm3_menu_resources[tag - MENU_STREAM_TAG];
}

/* The launcher can leave out the Graphics Settings menu (its pages, the main
 * menu row and the pause menu's HUD row: MM3_GRAPHICS_MENU=0) and the
 * Circuit mode (its screens and the single player row: MM3_EXTRA_CIRCUITS=0);
 * those screens then load from the game's own files. The main menu carries
 * both, so without the graphics row it is the variant generate.py builds
 * ("#nographics"), and without either the retail one. */
static int graphics_resource(const char *name)
{
    return !_strnicmp(name, "graphics/", 9) || !_strnicmp(name, "titles/titleGraphics", 20) ||
           !_stricmp(name, "Pausemenu/menuHud") || !_stricmp(name, "Pausemenu/menuOptions");
}

static int circuit_resource(const char *name)
{
    static const char *const names[] = {
        "singlePlayer/screenSinglePlayerType", "singlePlayer/screenSinglePlayerRaceSetup",
        "shared/screenCitySelect", "singlePlayer/screenCircuitCitySelect",
        "singlePlayer/screenCircuitRaceSetup", "singlePlayer/modes/spCircuitMode", "titles/titleCircuit"};
    unsigned i;
    for (i = 0; i < sizeof names / sizeof names[0]; ++i)
        if (!_stricmp(name, names[i])) return 1;
    return 0;
}

static const char *served_name(const char *name)
{
    int graphics = mm3_mod_graphics_menu(), circuits = mm3_mod_extra_circuits();
    if (!_stricmp(name, "main/screenMain"))
        return graphics ? name : circuits ? "main/screenMain#nographics" : NULL;
    if (graphics_resource(name)) return graphics ? name : NULL;
    if (circuit_resource(name)) return circuits ? name : NULL;
    return name;
}

void sub_00119AEE(void)
{
    const char *name = (const char *)XBOX_PTR(MEM32(g_esp + 4u));
    unsigned i;
    if (*name == '+') ++name;
    if (_strnicmp(name, "Menu/Frontend/", 14) == 0) name += 14;
    else if (_strnicmp(name, "Menu/Pausemenu/", 15) == 0) name += 5;
    if (!(name = served_name(name))) {
        sub_00119AEE_original();
        return;
    }
    for (i = 0; i < MM3_MENU_RESOURCE_COUNT; ++i)
        if (_stricmp(name, mm3_menu_resources[i].name) == 0) break;
    if (i == MM3_MENU_RESOURCE_COUNT) {
        sub_00119AEE_original();
        return;
    }
    {
        uint32_t sp = g_esp, cx = g_ecx, dx = g_edx;
        uint32_t bx = g_ebx, si = g_esi, di = g_edi;
        uint32_t bp = g_ebp, seh = g_seh_ebp;
        PUSH32(g_esp, 16u);
        PUSH32(g_esp, 0x00119B03u);
        sub_0016FF04();
        if (g_eax) {
            MEM32(g_eax) = MENU_STREAM_VTABLE;
            MEM32(g_eax + 4u) = g_eax;
            MEM32(g_eax + 8u) = MENU_STREAM_TAG + i;
            MEM32(g_eax + 12u) = 0;
            fprintf(stderr, "[MM3_GRAPHICS_MENU] loaded %s\n", mm3_menu_resources[i].name);
        }
        g_esp = sp + 8u; /* original loader RET 4 */
        g_ecx = cx; g_edx = dx; g_ebx = bx; g_esi = si; g_edi = di;
        g_ebp = bp; g_seh_ebp = seh;
    }
}

void sub_00102168(void)
{
    const MM3MenuResource *resource = stream_resource(g_ecx);
    uint32_t destination, count, offset;
    if (!resource) { sub_00102168_original(); return; }
    destination = MEM32(g_esp + 4u);
    count = MEM32(g_esp + 8u);
    offset = MEM32(g_ecx + 12u);
    if (offset > resource->size || count > resource->size - offset) {
        fprintf(stderr, "[MM3_GRAPHICS_MENU] invalid read %s at %u + %u\n",
                resource->name, offset, count);
        g_eax = 0;
    } else {
        memcpy((char *)XBOX_PTR(destination), resource->data + offset, count);
        MEM32(g_ecx + 12u) = offset + count;
        g_eax = count;
    }
    g_esp += 12u; /* RET 8 */
}

void sub_00102191(void)
{
    const MM3MenuResource *resource = stream_resource(g_ecx);
    int32_t delta;
    int64_t offset;
    if (!resource) { sub_00102191_original(); return; }
    delta = (int32_t)MEM32(g_esp + 4u);
    offset = (int64_t)MEM32(g_ecx + 12u) + delta;
    if (offset < 0 || offset > resource->size) g_eax = 0;
    else { MEM32(g_ecx + 12u) = (uint32_t)offset; g_eax = (uint32_t)delta; }
    g_esp += 8u; /* RET 4 */
}

void sub_001021B5(void)
{
    const MM3MenuResource *resource = stream_resource(g_ecx);
    if (!resource) { sub_001021B5_original(); return; }
    g_eax = resource->size;
    g_esp += 4u;
}

void sub_0006EA22(void)
{
    /* The original deleting destructor can release the normal guest block
     * after its file member is cleared. There is no file handle to close. */
    if (stream_resource(g_ecx)) MEM32(g_ecx + 8u) = 0;
    sub_0006EA22_original();
}
