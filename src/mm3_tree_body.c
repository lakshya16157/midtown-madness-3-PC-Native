#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mm3_recomp_types.h"
#include "mm3_tree_body.h"

extern void sub_001808CB(void);
extern void sub_0016E41F(void);
extern void sub_0016FF04(void);
extern void sub_00170EC0(void);
extern void sub_00216ACA(void);
extern void sub_002163BD(void);
extern void sub_00216398(void);
extern void sub_001F196C(void);
extern void sub_001F1512(void);
extern void sub_000125EA(void);
extern void sub_000127A9(void);
static RECOMP_TLS uint32_t s_geometry_reserve;
static RECOMP_TLS uint32_t s_created_object, s_created_manager;

/* Only the two native static-piece factories consume this preallocation.
 * Their allocation is otherwise unchecked; reserve it before construction so
 * low memory leaves the original tree intact. Other calls remain unchanged. */
int mm3_tree_body_geometry_allocation(void)
{
    if (!s_geometry_reserve || MEM32(g_esp + 4u) != 0x27Cu) return 0;
    g_eax = s_geometry_reserve;
    s_geometry_reserve = 0;
    g_esp += 8u;
    return 1;
}

static int body_read(uint32_t address, void *out, size_t size)
{
    SIZE_T copied = 0;
    if (address < 0x10000u || address > UINT32_MAX - size) return 0;
    return ReadProcessMemory(GetCurrentProcess(), (const void *)XBOX_PTR(address), out, size, &copied)
        && copied == size;
}

static uint32_t body_word(uint32_t address)
{
    uint32_t value = 0;
    body_read(address, &value, sizeof value);
    return value;
}

static void body_destroy(uint32_t object)
{
    uint32_t vtable = body_word(object), destroy = body_word(vtable);
    recomp_func_t destructor = recomp_lookup(destroy);
    if (destructor) {
        g_ecx = object;
        PUSH32(g_esp, 1u);
        PUSH32(g_esp, 0x001F20E8u);
        destructor();
    }
}

/* The caller holds a complete guest-state/stack snapshot until confirmation.
 * If native city retirement did not clear the static piece, undo registration
 * with the manager's own removal operation before destroying the new body. */
void mm3_tree_body_confirm(int retired)
{
    uint32_t object = s_created_object, manager = s_created_manager;
    s_created_object = s_created_manager = 0;
    if (!retired && object && manager) {
        g_esi = manager;
        PUSH32(g_esp, object);
        PUSH32(g_esp, 0x001F251Fu);
        sub_001F1512();
        body_destroy(object);
    }
}

/* The native prop manager owns detached objects, while the physics controller
 * owns their colliders. Use precisely the constructor/registration branch of
 * 1F2672, rather than its type-4 visual fallback. Native city streaming, pause,
 * contacts, sounds, render transforms and destruction then apply to trees too.
 * The caller validates the struck instance and retires it only after success. */
int mm3_tree_body_create(uint32_t city, uint32_t id, uint32_t metadata,
    const float velocity[3], float position[3])
{
    enum { SCRATCH = 16384 };
    unsigned char scratch[SCRATCH];
    uint32_t regs[] = {g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp,g_seh_ebp,g_fs_base,g_eflags};
    double fp[8];
    int top = g_fp_top, cmp = g_fp_cmp, df = g_df;
    uint16_t cc = g_fp_cc, control = g_fp_control_word;
    RecompXmm xmm[] = {g_xmm0,g_xmm1,g_xmm2,g_xmm3,g_xmm4,g_xmm5,g_xmm6,g_xmm7};
    uint32_t stack, data, manager = 0, object = 0, geometry = 0, root, type, reserve = 0;
    float matrix[12], linear[3], angular[3], horizontal;
    int created = 0;
    unsigned i;
    (void)metadata; /* Metadata was checked against the current native model. */
    s_created_object = s_created_manager = 0;
    if (city < 0x10000u || city > UINT32_MAX - 0x15400u
        || city != body_word(0x4291F0u) || g_esp < SCRATCH + 0x10000u) return 0;
    for (i = 0; i < 3; ++i) if (!isfinite(velocity[i])) return 0;
    stack = g_esp - SCRATCH;
    if (!body_read(stack, scratch, sizeof scratch)) return 0;
    memcpy(fp, g_fp_stack, sizeof fp);
    /* Scratch data stays ABOVE a private native call stack, so constructor
     * frames cannot overwrite the velocity pointers passed to its helpers. */
    data = regs[3] - 256u;
    g_esp = data - 256u;
    g_df = 0; g_fp_top = 0; g_fp_cc = 0; g_fp_cmp = 0; g_fp_control_word = 0x37Fu;
    PUSH32(g_esp, 0u);
    PUSH32(g_esp, 0x00180A46u);
    sub_001808CB();
    manager = g_eax;
    if (manager < 0x10000u || manager > UINT32_MAX - 0x40u
        || body_word(manager + 0x20u) != city
        || !body_word(manager + 0x1Cu) || body_word(manager + 0x18u) > 16u) goto done;
    /* Allocate first: a failed allocation leaves the upright tree solid. */
    PUSH32(g_esp, 0xB0u);
    PUSH32(g_esp, 0x001F26D5u);
    sub_0016FF04();
    object = g_eax;
    if (object < 0x10000u || object > UINT32_MAX - 0xB0u) goto done;
    PUSH32(g_esp, 0x27Cu);
    PUSH32(g_esp, 0x0015CD97u);
    sub_0016FF04();
    reserve = g_eax;
    if (reserve < 0x10000u || reserve > UINT32_MAX - 0x27Cu) goto discard;
    s_geometry_reserve = reserve;
    g_ecx = city;
    PUSH32(g_esp, id);
    PUSH32(g_esp, 0x001F274Cu);
    sub_0016E41F();
    geometry = g_eax;
    if (s_geometry_reserve || geometry != reserve
        || geometry < 0x10000u || geometry > UINT32_MAX - 0x27Cu
        || !body_read(geometry + 0x44u, &type, sizeof type) || type != 0u
        || !body_read(geometry + 8u, matrix, sizeof matrix)) goto discard;
    for (i = 0; i < 12; ++i) if (!isfinite(matrix[i])) goto discard;
    /* 15CD35/15B9AB return the authored collision planes, model and exact
     * world transform, including rotated/scaled static instances. */
    MEM32(geometry + 0x9Cu) = id;
    memcpy((void *)XBOX_PTR(data), matrix, sizeof matrix);
    memcpy((void *)XBOX_PTR(data + 128u), "object", 7u);
    g_ecx = data + 80u;
    PUSH32(g_esp, data + 128u);
    PUSH32(g_esp, 0x001F26E8u);
    sub_000125EA();
    horizontal = hypotf(velocity[0], velocity[2]);
    for (i = 0; i < 3; ++i) linear[i] = velocity[i] * 0.35f;
    linear[1] = fmaxf(0.0f, fminf(2.0f, linear[1]));
    /* cross(up, impact) tips the detached body away from the impact. Its
     * subsequent fall/push/bounce comes from native gravity and impulses. */
    angular[0] = horizontal > 0.1f ? velocity[2] / horizontal * 2.0f : 0.0f;
    angular[1] = 0.0f;
    angular[2] = horizontal > 0.1f ? -velocity[0] / horizontal * 2.0f : -2.0f;
    memcpy((void *)XBOX_PTR(data + 48u), linear, sizeof linear);
    memcpy((void *)XBOX_PTR(data + 64u), angular, sizeof angular);
    PUSH32(g_esp, 0u); /* no initial-time offset */
    PUSH32(g_esp, 0u); /* same initial event state as native 1F2672 */
    PUSH32(g_esp, data + 80u);
    PUSH32(g_esp, geometry);
    PUSH32(g_esp, city);
    PUSH32(g_esp, object);
    PUSH32(g_esp, 0x001F2707u);
    sub_00216ACA();
    object = g_eax;
    g_ecx = data + 80u;
    PUSH32(g_esp, 1u);
    PUSH32(g_esp, 0x001F271Du);
    sub_000127A9();
    root = body_word(object + 8u);
    if (!root || !body_word(root + 0x14u) || body_word(object + 0xCu) != geometry) {
        body_destroy(object);
        body_destroy(geometry);
        goto done;
    }
    g_esi = object;
    PUSH32(g_esp, data);
    PUSH32(g_esp, 0x001F2725u);
    sub_002163BD();
    /* 216398 assigns the actual rigid-body velocities. */
    g_eax = object; g_edx = data + 48u; g_ecx = data + 64u;
    PUSH32(g_esp, 0x001F2732u);
    sub_00216398();
    g_edi = manager;
    PUSH32(g_esp, object);
    PUSH32(g_esp, 0x001F273Bu);
    sub_001F196C();
    s_created_object = object; s_created_manager = manager;
    memcpy(position, matrix + 9, 3u * sizeof(float));
    created = 1;
    {
        const char *trace = getenv("MM3_TREE_TRACE");
        if (trace && *trace && strcmp(trace, "0")) {
            fprintf(stderr, "[TREE_BODY_NATIVE] id=%08X object=%08X geometry=%08X root=%08X body=%08X controller=%08X render=native_mesh collision=native_rigid_body\n",
                id, object, geometry, root, body_word(root + 0x14u), manager);
            fflush(stderr);
        }
    }
    goto done;
discard:
    /* An unexpected geometry class never enters the visual prop path. */
    if (geometry >= 0x10000u && geometry == reserve && !s_geometry_reserve) body_destroy(geometry);
    if (s_geometry_reserve) {
        PUSH32(g_esp, s_geometry_reserve);
        PUSH32(g_esp, 0x0021758Bu);
        sub_00170EC0();
        g_esp += 4u;
    }
    PUSH32(g_esp, object);
    PUSH32(g_esp, 0x0021758Bu);
    sub_00170EC0();
    g_esp += 4u; /* allocator free is cdecl */
done:
    s_geometry_reserve = 0;
    memcpy((void *)XBOX_PTR(stack), scratch, sizeof scratch);
    g_eax=regs[0];g_ecx=regs[1];g_edx=regs[2];g_esp=regs[3];g_ebx=regs[4];
    g_esi=regs[5];g_edi=regs[6];g_ebp=regs[7];g_seh_ebp=regs[8];g_fs_base=regs[9];g_eflags=regs[10];
    memcpy(g_fp_stack, fp, sizeof fp);
    g_fp_top=top;g_fp_cmp=cmp;g_df=df;g_fp_cc=cc;g_fp_control_word=control;
    g_xmm0=xmm[0];g_xmm1=xmm[1];g_xmm2=xmm[2];g_xmm3=xmm[3];
    g_xmm4=xmm[4];g_xmm5=xmm[5];g_xmm6=xmm[6];g_xmm7=xmm[7];
    return created;
}
