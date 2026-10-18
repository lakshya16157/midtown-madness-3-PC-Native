#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mm3_recomp_types.h"
#include "mm3_tree_body.h"
#ifndef MM3_TREE_TEST_HELPERS
#include "mm3_tree_audio.h"
#else
#define mm3_tree_audio_begin() ((void)0)
#define mm3_tree_audio_select(metadata, city, id, car) ((void)0)
#define mm3_tree_audio_finish(converted, contacting, position, speed) ((void)0)
#define mm3_tree_body_create(city, id, metadata, velocity, position) (1)
#define mm3_tree_body_confirm(retired) ((void)0)
#endif

extern void sub_0016AEDB_original(void);
extern void sub_000E1F49_original(void);
extern void sub_001D137B_original(void);
extern void sub_0016CABD(void);

#ifndef MM3_TREE_CLOCK
#define MM3_TREE_CLOCK GetTickCount64
#endif
#ifndef MM3_TREE_LOG
#define MM3_TREE_LOG(...) fprintf(stderr, __VA_ARGS__)
#endif
#ifndef MM3_TREE_FLUSH
#define MM3_TREE_FLUSH() fflush(stderr)
#endif

/* Keep native first-impact resistance/effects, detach the authored geometry
 * into the native rigid-body prop controller, then retire the static instance
 * through the city's own change list. Shared models and masks stay intact. */
#define TREE_LINES_PER_SECOND 32u
#define TREE_NAME_SIZE 48u
#define TREE_BREAK_SPEED 5.0f /* metres/second: gentle nudges remain solid */
static volatile LONG s_tree_enabled = -1;
static RECOMP_TLS uint64_t s_tree_second;
static RECOMP_TLS unsigned s_tree_lines, s_tree_suppressed;
static RECOMP_TLS unsigned s_tree_render_depth;
static RECOMP_TLS int s_tree_fall_drawing;
static RECOMP_TLS uint64_t s_tree_contact_second;
static RECOMP_TLS unsigned s_tree_contact_lines;
static RECOMP_TLS uint32_t s_tree_contact_car, s_tree_contact_body, s_tree_contact_before_count;
static RECOMP_TLS uint32_t s_tree_contact_record, s_tree_contact_metadata;
static RECOMP_TLS uint16_t s_tree_contact_flags;
static RECOMP_TLS unsigned s_tree_contact_trees;
static RECOMP_TLS uint32_t s_tree_break_city, s_tree_break_id, s_tree_break_metadata;
static RECOMP_TLS uint32_t s_tree_break_body;
static RECOMP_TLS int s_tree_break_pending, s_tree_broken;
static RECOMP_TLS unsigned s_tree_expired_contacts;
static RECOMP_TLS int s_tree_contact_log;
static RECOMP_TLS float s_tree_contact_velocity[3], s_tree_contact_position[3];
static RECOMP_TLS float s_tree_break_velocity[3];
static RECOMP_TLS float s_tree_impact_speed_loss;
static RECOMP_TLS float s_tree_break_position[3];
static RECOMP_TLS float s_tree_audio_position[3], s_tree_audio_speed;
static RECOMP_TLS int s_tree_audio_position_valid;
static RECOMP_TLS uint32_t s_tree_solid_before;

static int tree_enabled(void)
{
    if (s_tree_enabled < 0) {
        const char *value = getenv("MM3_TREE_TRACE");
        LONG enabled = value && *value && strcmp(value, "0") != 0;
        if (enabled && strcmp(value, "contacts") == 0) enabled = 2;
        if (InterlockedCompareExchange(&s_tree_enabled, enabled, -1) == -1 && enabled) {
            MM3_TREE_LOG("[TREE_TRACE] enabled mode=%s object_render=%s collision_events=E1F49 breakable_tree_contacts=1D137B lines_per_second=%u metadata=retail_LocalObject\n",
                enabled == 2 ? "contacts" : "full", enabled == 2 ? "off" : "16AEDB", TREE_LINES_PER_SECOND);
            MM3_TREE_FLUSH();
        }
    }
    return (int)s_tree_enabled;
}

static int tree_read(uint32_t address, void *out, size_t size)
{
    SIZE_T copied = 0;
    uintptr_t host;
    if (address < 0x10000u || size > UINT32_MAX || address > UINT32_MAX - size) return 0;
    host = (uintptr_t)address + (uintptr_t)g_xbox_mem_offset;
    if (host > UINTPTR_MAX - size) return 0;
    return ReadProcessMemory(GetCurrentProcess(), (const void *)host, out, size, &copied) && copied == size;
}

static uint32_t tree_word(uint32_t address)
{
    uint32_t value = 0;
    tree_read(address, &value, sizeof value);
    return value;
}

static uint32_t tree_word_at(uint64_t address)
{
    return address <= UINT32_MAX ? tree_word((uint32_t)address) : 0;
}

static int tree_name(uint32_t address, char out[TREE_NAME_SIZE])
{
    unsigned hops, length = 0;
    int emit = 1;
    uint32_t node[3];
    out[0] = 0;
    if (!tree_read(address, node, sizeof node) || ((node[0] & 15u) << 4 | (node[1] & 15u))) return 0;
    /* Retail sub_0017069B decodes an interned trie terminal. Each node
     * carries one character in two low nibbles and an aligned parent in
     * word0. Emit an ancestor only when its word2 backlink matches the
     * child, then reverse the bytes (including the first terminal NUL). */
    for (hops = 0; hops < 160; ++hops) {
        uint32_t parent = node[0] & ~15u;
        if (emit) {
            unsigned value = (node[0] & 15u) << 4 | (node[1] & 15u);
            if (length == TREE_NAME_SIZE - 1 || (length && (value < 33 || value > 126))) break;
            out[length++] = (char)value;
        }
        if (!parent) {
            unsigned i;
            for (i = 0; i < length / 2; ++i) {
                char value = out[i]; out[i] = out[length - i - 1]; out[length - i - 1] = value;
            }
            return length > 0;
        }
        if (!tree_read(parent, node, sizeof node)) break;
        emit = (((node[0] & 15u) << 4 | (node[1] & 15u)) != 0) && node[2] == address;
        address = parent;
    }
    out[0] = 0;
    return 0;
}

static int tree_budget(void)
{
    uint64_t second = MM3_TREE_CLOCK() / 1000;
    if (second != s_tree_second) {
        if (s_tree_suppressed)
            MM3_TREE_LOG("[TREE_TRACE] suppressed=%u previous_second=%llu\n", s_tree_suppressed, (unsigned long long)s_tree_second);
        s_tree_second = second;
        s_tree_lines = s_tree_suppressed = 0;
    }
    if (s_tree_lines == TREE_LINES_PER_SECOND) { ++s_tree_suppressed; return 0; }
    ++s_tree_lines;
    return 1;
}

static int local_object_tree(const unsigned char local[52], char sound[TREE_NAME_SIZE])
{
    uint32_t pointer, events;
    unsigned i;
    memcpy(&pointer, local + 0x10, sizeof pointer);
    tree_name(pointer, sound);
    if (strcmp(sound, "tree") == 0) return 1;
    memcpy(&events, local + 0x2C, sizeof events);
    /* Paris identifies tree impacts in an event sound instead of the main
     * object sound. Bound the scan independently of any malformed count. */
    for (i = 0; i < local[0] && i < 64; ++i) {
        char event_sound[TREE_NAME_SIZE];
        uint64_t address = (uint64_t)events + (uint64_t)i * 86u + 0x27u;
        if (address > UINT32_MAX) break;
        pointer = tree_word((uint32_t)address);
        if (tree_name(pointer, event_sound) && strcmp(event_sound, "tree") == 0) return 1;
    }
    return 0;
}

int mm3_tree_fall_drawing(void)
{
    return s_tree_fall_drawing;
}

static void tree_draw_original(void)
{
    /* This is the native renderer's live call frame, which the original
     * function consumes too. Avoid an OS memory read for every static prop.
     * Object and metadata pointers below still require checked reads. */
    uint32_t caller = MEM32(g_esp);
    int previous_drawing = s_tree_fall_drawing;
    s_tree_fall_drawing = 0;
    /* Native detached geometry calls the model renderer from 167D94. Its
     * billboard shortcut discards the rotated body basis, so use the mesh
     * for this tree only. The real body pose stays untouched, including nested
     * unrelated/static draws and both native render-queue matrix copies. */
    if (caller == 0x00167D94u) {
        unsigned char local[52];
        char sound[TREE_NAME_SIZE];
        uint32_t object = tree_word_at((uint64_t)g_esp + 4u);
        if (tree_read(tree_word_at((uint64_t)object + 0xA0u), local, sizeof local)
            && local_object_tree(local, sound)) s_tree_fall_drawing = 1;
    }
    sub_0016AEDB_original();
    s_tree_fall_drawing = previous_drawing;
}

static int tree_contact_budget(void)
{
    uint64_t second = MM3_TREE_CLOCK() / 1000;
    if (second != s_tree_contact_second) {
        s_tree_contact_second = second;
        s_tree_contact_lines = 0;
    }
    return s_tree_contact_lines++ < TREE_LINES_PER_SECOND;
}

int mm3_tree_use_solid_contact(uint32_t record, float speed)
{
    /* Retail static collision producers copy primitive flags+0xC8 and
     * LocalObject metadata+0xA0 into these temporary 0x10C contact records.
     * Above 40km/h, flag 2 makes 1D137B add 10% of speed vertically and exit
     * without registering a solid contact. For trees, let its normal branch
     * register the first contact/penetration correction instead. Once its
     * impact effects finish, remove this instance through native 16CABD.
     * Classify only the selected contact; never retain a city across updates. */
    unsigned char local[52];
    char sound[TREE_NAME_SIZE];
    uint32_t metadata;
    uint16_t flags;
    if (record > UINT32_MAX - 0x10Cu || !tree_read(record + 0x100, &flags, sizeof flags)) return 0;
    metadata = tree_word(record + 0x108);
    if (!tree_read(metadata, local, sizeof local) || !local_object_tree(local, sound)) return 0;
    ++s_tree_contact_trees;
    {
        uint32_t sim = tree_word_at((uint64_t)s_tree_contact_car + 0x3Cu);
        uint32_t body = tree_word_at((uint64_t)sim + 4u);
        s_tree_audio_speed = isfinite(speed) ? speed : 0.0f;
        s_tree_audio_position_valid = body <= UINT32_MAX - 0xD4u
            && tree_read(body + 0xC8u, s_tree_audio_position, sizeof s_tree_audio_position);
        mm3_tree_audio_select(metadata, tree_word(0x4291F0), tree_word(record + 0x104), s_tree_contact_car);
    }
    if (speed >= TREE_BREAK_SPEED && isfinite(speed)) {
        s_tree_break_city = tree_word(0x4291F0); /* current native world.city */
        s_tree_break_id = tree_word(record + 0x104);
        s_tree_break_metadata = metadata;
        s_tree_break_pending = s_tree_break_city != 0;
        s_tree_break_body = 0;
        memset(s_tree_break_velocity, 0, sizeof s_tree_break_velocity);
        {
            uint32_t sim = tree_word_at((uint64_t)s_tree_contact_car + 0x3Cu);
            uint32_t body = tree_word_at((uint64_t)sim + 4u);
            if (body <= UINT32_MAX - 0x2Cu
                && tree_read(body + 0x20u, s_tree_break_velocity, sizeof s_tree_break_velocity))
                s_tree_break_body = body;
        }
    }
    if (!s_tree_contact_log && tree_enabled() && tree_contact_budget()) {
        s_tree_contact_record = record;
        s_tree_contact_metadata = metadata;
        s_tree_contact_flags = flags;
        s_tree_contact_body = tree_word(tree_word(s_tree_contact_car + 0x3C) + 4);
        tree_read(s_tree_contact_body + 0x20, s_tree_contact_velocity, sizeof s_tree_contact_velocity);
        tree_read(s_tree_contact_body + 0xC8, s_tree_contact_position, sizeof s_tree_contact_position);
        s_tree_contact_before_count = MEM32(0x3C0344u);
        s_tree_contact_log = 1;
    }
    return 1;
}

static unsigned tree_expire_contacts(uint32_t car, uint32_t id, uint32_t metadata)
{
    /* 1D47F4 refills these native vectors once per collision update, while
     * physics substeps reuse their narrowphase points in 1D137B. Native city
     * removal prevents future collection but does not expire these copies.
     * Set only the retired piece's intersection count to zero: 1D137B skips
     * empty records, and every other prop survives. Authored tree flags do
     * not enable the secondary vector's separate flag-4 launch response. */
    static const unsigned vector_offsets[] = {0x43F0u, 0x8700u};
    static const unsigned buffer_offsets[] = {0xF0u, 0x4400u};
    unsigned expired = 0, vector, index;
    if (car < 0x10000u || car > UINT32_MAX - 0x8710u) return 0;
    for (vector = 0; vector < 2; ++vector) {
        uint32_t header[3];
        if (!tree_read(car + vector_offsets[vector], header, sizeof header)
            || header[0] > 64u || header[1] > header[0]
            || header[2] != car + buffer_offsets[vector]) continue;
        for (index = 0; index < header[1]; ++index) {
            uint32_t record = header[2] + index * 0x10Cu, tail[19];
            if (!tree_read(record + 0xC0u, tail, sizeof tail)
                || !tail[0] || tail[17] != id || tail[18] != metadata) continue;
            MEM32(record + 0xC0u) = 0;
            ++expired;
        }
    }
    return expired;
}

static int tree_remove_native(void)
{
    /* Validate the current city and the native instance before calling its
     * removal API. The packed contact ID is an instance ID, not a pointer.
     * Type 0/1 pieces use per-instance availability masks read by BOTH native
     * collision collection and rendering. Other packed types fail closed. */
    uint32_t city = s_tree_break_city, id = s_tree_break_id;
    uint32_t cell, instances, instance, mask, dirty, identity, context, owner, grid[2], header[8];
    uint32_t descriptors, geometry, models;
    uint16_t descriptor_index, model_index;
    unsigned char local[52], primitive[0xF8];
    char sound[TREE_NAME_SIZE];
    unsigned type = (id >> 21) & 3u, index = (id >> 5) & 0xFFFFu;
    unsigned piece = id & 31u, cell_index = id >> 23;
    unsigned stride = type ? 0xF8u : 0xCCu, availability = type ? 0xCu : 8u;
    uint64_t address;
    uint32_t regs[] = {g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp,g_seh_ebp,g_fs_base,g_eflags};
    double fp[8];
    int top = g_fp_top, cmp = g_fp_cmp, df = g_df;
    uint16_t cc = g_fp_cc, control = g_fp_control_word;
    RecompXmm xmm[] = {g_xmm0,g_xmm1,g_xmm2,g_xmm3,g_xmm4,g_xmm5,g_xmm6,g_xmm7};
    unsigned char scratch[4096];
    uint32_t stack;
    if (!s_tree_break_pending || city != tree_word(0x4291F0) || city > UINT32_MAX - 0x15400u || type > 1u
        || !tree_read(s_tree_break_metadata, local, sizeof local) || !local_object_tree(local, sound)
        || !tree_read(city + 0x15010, grid, sizeof grid) || !grid[0] || !grid[1]
        || (uint64_t)grid[0] * grid[1] > 512u
        || cell_index >= (uint64_t)grid[0] * grid[1]) return 0;
    address = (uint64_t)tree_word(city + 0x15304) + (uint64_t)cell_index * 4u;
    if (address > UINT32_MAX) return 0;
    cell = tree_word((uint32_t)address);
    if (!tree_read(cell, header, sizeof header)) return 0;
    instances = header[type ? 4 : 6];
    address = (uint64_t)instances + (uint64_t)index * stride;
    if (address > UINT32_MAX - stride || instances < 0x10000u
        || address + stride > header[type ? 5 : 7]) return 0;
    instance = (uint32_t)address;
    if (!tree_read(instance, primitive, stride)) return 0;
    memcpy(&identity, primitive + (type ? 0xF4u : 0xC8u), sizeof identity);
    if ((identity & ~31u) != (id & ~31u)) return 0;
    memcpy(&mask, primitive + availability, sizeof mask);
    if (!(mask & (1u << piece))) return 0;
    /* Resolve the same piece/model chain as native 15D03A / 15BCBB. A cell
     * reload can reuse an instance ID: its current piece must still own the
     * contact's tree metadata before we retire it. Never mutate shared models. */
    memcpy(&context, primitive, sizeof context);
    descriptors = tree_word_at((uint64_t)context + 4u);
    if (type) memcpy(&owner, primitive + 4, sizeof owner);
    else {
        owner = tree_word(context);
        descriptors = tree_word_at((uint64_t)descriptors + 4u);
    }
    memcpy(&descriptor_index, primitive + (type ? 8u : 4u), sizeof descriptor_index);
    address = (uint64_t)descriptors + (uint64_t)descriptor_index * 0x6Cu;
    if (address > UINT32_MAX - 0x30u || piece >= tree_word((uint32_t)address + 0x2Cu)) return 0;
    geometry = tree_word_at((uint64_t)tree_word((uint32_t)address) + 0x14u);
    address = (uint64_t)geometry + ((uint64_t)tree_word((uint32_t)address + 0x28u) + piece) * 2u;
    if (address > UINT32_MAX || !tree_read((uint32_t)address, &model_index, sizeof model_index)) return 0;
    models = tree_word_at((uint64_t)owner + 0x11D0u);
    address = (uint64_t)models + (uint64_t)model_index * 0xF4u + 0xA0u;
    if (tree_word_at(address) != s_tree_break_metadata) return 0;
    /* Native 16CABD appends the packed ID to a 16-byte vector for this cell,
     * then calls 15FFE0 to clear only this piece's availability bit. */
    address = (uint64_t)tree_word(city + 0x15308) + (uint64_t)cell_index * 16u;
    if (address > UINT32_MAX || !tree_read((uint32_t)address, &dirty, sizeof dirty)) return 0;
    if (g_esp < 0x11000u) return 0;
    stack = g_esp - sizeof scratch;
    if (!tree_read(stack, scratch, sizeof scratch)) return 0;
    /* Validate retirement scratch before creating anything. Keep the upright
     * collider until the native street-prop controller owns a real rigid body. */
    if (!mm3_tree_body_create(city, id, s_tree_break_metadata,
            s_tree_break_velocity, s_tree_break_position)) return 0;
    memcpy(fp, g_fp_stack, sizeof fp);
    g_edi = city;
    PUSH32(g_esp, 1u);
    PUSH32(g_esp, id);
    PUSH32(g_esp, 0x001F0AD9u); /* native static-piece retirement caller */
    sub_0016CABD();
    mm3_tree_body_confirm(!(tree_word(instance + availability) & (1u << piece)));
    memcpy((void *)XBOX_PTR(stack), scratch, sizeof scratch);
    g_eax=regs[0];g_ecx=regs[1];g_edx=regs[2];g_esp=regs[3];g_ebx=regs[4];
    g_esi=regs[5];g_edi=regs[6];g_ebp=regs[7];g_seh_ebp=regs[8];g_fs_base=regs[9];g_eflags=regs[10];
    memcpy(g_fp_stack, fp, sizeof fp);
    g_fp_top=top;g_fp_cmp=cmp;g_df=df;g_fp_cc=cc;g_fp_control_word=control;
    g_xmm0=xmm[0];g_xmm1=xmm[1];g_xmm2=xmm[2];g_xmm3=xmm[3];
    g_xmm4=xmm[4];g_xmm5=xmm[5];g_xmm6=xmm[6];g_xmm7=xmm[7];
    if (tree_word(instance + availability) & (1u << piece)) return 0;
    /* Rendering and subsequent contacts now belong to the detached native
     * body. Re-enabling the static draw mask would submit an upright duplicate. */
    s_tree_expired_contacts = tree_expire_contacts(s_tree_contact_car, id, s_tree_break_metadata);
    return 1;
}

static void tree_apply_impact_resistance(void)
{
    float velocity[3], speed, dx, dz, forward, loss;
    uint32_t sim, body;
    /* Apply once, only after this instance was successfully retired. Retain
     * the native solver's lateral/vertical response and any larger slowdown;
     * breaking the tree costs at most 8% of forward speed, capped at 1.5m/s. */
    if (!s_tree_broken || !s_tree_break_body) return;
    sim = tree_word_at((uint64_t)s_tree_contact_car + 0x3Cu);
    body = tree_word_at((uint64_t)sim + 4u);
    if (body != s_tree_break_body
        || !tree_read(body + 0x20u, velocity, sizeof velocity)
        || !isfinite(velocity[0]) || !isfinite(velocity[2])) return;
    speed = hypotf(s_tree_break_velocity[0], s_tree_break_velocity[2]);
    if (!isfinite(speed) || speed < TREE_BREAK_SPEED) return;
    dx = s_tree_break_velocity[0] / speed;
    dz = s_tree_break_velocity[2] / speed;
    forward = velocity[0] * dx + velocity[2] * dz;
    if (!isfinite(forward) || forward <= 0.0f) return;
    loss = fminf(1.5f, 0.08f * fminf(speed, forward));
    MEMF(body + 0x20u) = velocity[0] - dx * loss;
    MEMF(body + 0x28u) = velocity[2] - dz * loss;
    s_tree_impact_speed_loss = loss;
}

void sub_001D137B(void)
{
    /* Native arguments and the title's static contact counter are mapped
     * throughout this call. Keep checked reads for indirect car/body data. */
    s_tree_contact_car = MEM32(g_esp + 4u);
    s_tree_contact_trees = 0;
    s_tree_contact_log = 0;
    s_tree_break_pending = s_tree_broken = 0;
    s_tree_expired_contacts = 0;
    s_tree_impact_speed_loss = 0.0f;
    s_tree_break_body = 0;
    s_tree_break_city = s_tree_break_id = s_tree_break_metadata = 0;
    memset(s_tree_break_position, 0, sizeof s_tree_break_position);
    memset(s_tree_audio_position, 0, sizeof s_tree_audio_position);
    s_tree_audio_speed = 0.0f;
    s_tree_audio_position_valid = 0;
    s_tree_solid_before = MEM32(0x3C0344u);
    mm3_tree_audio_begin();
    sub_001D137B_original();
    s_tree_broken = tree_remove_native();
    tree_apply_impact_resistance();
    mm3_tree_audio_finish(s_tree_broken,
        s_tree_contact_trees != 0 && s_tree_audio_position_valid
            && MEM32(0x3C0344u) > s_tree_solid_before,
        s_tree_broken ? s_tree_break_position : s_tree_audio_position, s_tree_audio_speed);
    s_tree_break_pending = 0;
    if (s_tree_contact_log) {
        float velocity[3] = {0}, position[3] = {0};
        uint32_t contacts = MEM32(0x3C0344u);
        tree_read(s_tree_contact_body + 0x20, velocity, sizeof velocity);
        tree_read(s_tree_contact_body + 0xC8, position, sizeof position);
        MM3_TREE_LOG("[TREE_BREAK_CONTACT] car=%08X contact=%08X metadata=%08X flags=%04X trees=%u native_hop_bit=2 response=%s original_flags=preserved id=%08X static_piece_retired=%d detached_collision=%s cached_contacts_expired=%u impact_speed_loss=%.3f velocity=(%.3f,%.3f,%.3f)->(%.3f,%.3f,%.3f) position=(%.3f,%.3f,%.3f)->(%.3f,%.3f,%.3f) solid_contacts=%u->%u\n",
            s_tree_contact_car, s_tree_contact_record, s_tree_contact_metadata, s_tree_contact_flags,
            s_tree_contact_trees, s_tree_broken ? "break" : "solid", s_tree_break_id, s_tree_broken,
            s_tree_broken ? "native_rigid_body" : "none", s_tree_expired_contacts, s_tree_impact_speed_loss,
            s_tree_contact_velocity[0], s_tree_contact_velocity[1], s_tree_contact_velocity[2],
            velocity[0], velocity[1], velocity[2], s_tree_contact_position[0], s_tree_contact_position[1], s_tree_contact_position[2],
            position[0], position[1], position[2], s_tree_contact_before_count, contacts);
        MM3_TREE_FLUSH();
    }
}

void sub_0016AEDB(void)
{
    unsigned char instance[0xF0], local[52], owner_flags[3] = {0};
    uint32_t stack, caller, object, owner = 0, metadata = 0, flags = 0;
    uint32_t pool_before[2] = {0, 0}, pool_after[2] = {0, 0};
    uint32_t contact_geometry = 0;
    uint16_t pieces[4] = {0};
    float bounds[6] = {0}, center[3] = {0};
    float transform_position[3] = {0};
    char sound[TREE_NAME_SIZE] = {0};
    int valid, local_valid = 0, is_tree = 0;
    unsigned i;
    if (tree_enabled() != 1 || !tree_budget()) { tree_draw_original(); return; }
    stack = g_esp;
    caller = tree_word(stack);
    object = tree_word(stack + 4);
    tree_read(tree_word(stack + 24) + 0x24, transform_position, sizeof transform_position);
    valid = tree_read(object, instance, sizeof instance);
    if (valid) {
        memcpy(&owner, instance, 4);
        memcpy(&metadata, instance + 0xA0, 4);
        memcpy(&flags, instance + 0xC8, 4);
        memcpy(&contact_geometry, instance + 0x40, 4);
        memcpy(pieces, instance + 0xA4, 4);
        memcpy(pieces + 2, instance + 0xEC, 4);
        tree_read(owner + 0xA0, owner_flags, sizeof owner_flags);
        memcpy(bounds, instance + 0x20, sizeof bounds);
        for (i = 0; i < 3; ++i) center[i] = (bounds[i] + bounds[i + 3]) * 0.5f;
        pool_before[0] = tree_word(owner + 0x112C);
        pool_before[1] = tree_word(owner + 0x13E38);
        local_valid = tree_read(metadata, local, sizeof local);
        if (local_valid) is_tree = local_object_tree(local, sound);
    }
    ++s_tree_render_depth;
    tree_draw_original();
    --s_tree_render_depth;
    if (valid) {
        pool_after[0] = tree_word(owner + 0x112C);
        pool_after[1] = tree_word(owner + 0x13E38);
    }
    {
        MM3_TREE_LOG("[TREE_OBJECT_RENDER] caller=%08X object=%08X read=%d tree=%d local=%08X local_read=%d sound=%s flags=%08X owner_flags=%u,%u,%u geometry40=%08X piecesA4=%u,%u piecesEC=%u,%u transform_position=(%.3f,%.3f,%.3f) local_bounds_center=(%.3f,%.3f,%.3f) local_bounds=(%.3f,%.3f,%.3f,%.3f,%.3f,%.3f) local_bytes=%u,%u,%u,%u render_pool0=%u->%u render_pool1=%u->%u result=%02X stack=%08X->%08X\n",
            caller, object, valid, is_tree, metadata, local_valid, sound[0] ? sound : "none", flags,
            owner_flags[0], owner_flags[1], owner_flags[2], contact_geometry,
            pieces[0], pieces[1], pieces[2], pieces[3],
            transform_position[0], transform_position[1], transform_position[2],
            center[0], center[1], center[2], bounds[0], bounds[1], bounds[2], bounds[3], bounds[4], bounds[5],
            local_valid ? local[0] : 0, local_valid ? local[1] : 0,
            local_valid ? local[2] : 0, local_valid ? local[3] : 0,
            pool_before[0], pool_after[0], pool_before[1], pool_after[1], g_eax & 255u, stack, g_esp);
        MM3_TREE_FLUSH();
    }
}

void sub_000E1F49(void)
{
    unsigned char event[86];
    uint32_t stack, caller, event_address, flags = 0, particle_ptr = 0, sound_ptr = 0;
    uint32_t velocity_ptr, output_ptr, output = 0;
    float velocity[3] = {0}, threshold = 0;
    char particle[TREE_NAME_SIZE] = {0}, sound[TREE_NAME_SIZE] = {0};
    int valid, velocity_valid;
    unsigned render_depth;
    if (!tree_enabled()) { sub_000E1F49_original(); return; }
    /* Reject render-only events before the expensive metadata/name reads. */
    flags = tree_word(g_ecx + 0xD);
    if ((flags & 3u) != 1u || !tree_budget()) {
        sub_000E1F49_original();
        return;
    }
    stack = g_esp;
    caller = tree_word(stack);
    event_address = g_ecx;
    render_depth = s_tree_render_depth;
    valid = tree_read(event_address, event, sizeof event);
    if (valid) {
        memcpy(&flags, event + 0xD, 4);
        memcpy(&particle_ptr, event + 9, 4);
        memcpy(&sound_ptr, event + 0x27, 4);
        memcpy(&threshold, event + 0x1A, 4);
        tree_name(particle_ptr, particle);
        tree_name(sound_ptr, sound);
    }
    velocity_ptr = tree_word(stack + 24);
    velocity_valid = tree_read(velocity_ptr, velocity, sizeof velocity);
    output_ptr = tree_word(stack + 36);
    sub_000E1F49_original();
    if (output_ptr) output = tree_word(output_ptr);
    /* Render-time leaf effects are too frequent to be useful. Only genuine
     * collision-kind events reach this report. */
    {
        double speed2 = (double)velocity[0] * velocity[0] + (double)velocity[1] * velocity[1] + (double)velocity[2] * velocity[2];
        MM3_TREE_LOG("[TREE_COLLISION_EVENT] caller=%08X event=%08X read=%d during_render=%u kind=%u flags=%08X particle=%s sound=%s velocity_read=%d velocity=(%.3f,%.3f,%.3f) speed_squared=%.3f threshold_squared=%.3f result=%02X output=%08X stack=%08X->%08X\n",
            caller, event_address, valid, render_depth, flags & 3u, flags,
            particle[0] ? particle : "none", sound[0] ? sound : "none", velocity_valid,
            velocity[0], velocity[1], velocity[2], speed2, threshold, g_eax & 255u, output, stack, g_esp);
        MM3_TREE_FLUSH();
    }
}
