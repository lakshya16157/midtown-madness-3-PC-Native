#include <windows.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp_types.h"
#include "mm3_police_log.h"

/* [MM3_POLICE]: what the city's police do, so a "no cops" report can be read
 * from the log. Read-only; sampled once a second on the game thread from the
 * primary player's update (sub_002203E5). MM3_POLICE_LOG=0 turns it off.
 *
 * The police manager is module(0x3C5780)+0xE5D8; its repository (+4) lists
 * the pool's actors at +0xB4..+0xB8, 8 bytes each (vtable 0x37EE10). A cop is
 * out while actor+0x2950 (its native execution mode) is nonzero: 15140F sets
 * it when the cop is sent, 151C4C clears it. Its car is controller(actor+0x28)
 * +0x19C (vtable 0x383768) -> +8 car -> +0x3C simulation -> +4 body, position
 * at body+0xC8. A cop just sent sits at (0, 0) for a moment before it is
 * placed, so it is reported once it has a real position. The Cops density
 * chosen for Cruise is the pool size at 0x407C68 (0x407C18 + 0x50).
 *
 * Lines: the pool when a city's police appear, each cop sent (and how far
 * away), its distance every 10 s while out (closing in, holding, or not
 * moving: stuck), each cop called off, and a minute summary. */

#ifndef MM3_POLICE_NOW
#define MM3_POLICE_NOW() GetTickCount()
#endif
#ifndef MM3_POLICE_PRINT
#define MM3_POLICE_PRINT(...) (fprintf(stderr, __VA_ARGS__), fflush(stderr))
#endif

/* The size of the loose disc file D:\Data\<root>\<relative> in the game
 * folder, or -1 when it is not there (a folder of that name counts as not
 * there). The tests supply their own. */
#ifndef MM3_POLICE_FILE_SIZE
BOOL xbox_translate_path(const char *xbox_path, WCHAR *host_path_buf, DWORD buf_size);

static long long disc_file_size(const char *root, const char *relative)
{
    char guest[MAX_PATH];
    WCHAR host[MAX_PATH];
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (snprintf(guest, sizeof guest, "D:\\Data\\%s\\%s", root, relative) >= (int)sizeof guest ||
        !xbox_translate_path(guest, host, MAX_PATH) ||
        !GetFileAttributesExW(host, GetFileExInfoStandard, &data) ||
        (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        return -1;
    return ((long long)data.nFileSizeHigh << 32) | data.nFileSizeLow;
}
#define MM3_POLICE_FILE_SIZE(root, relative) disc_file_size(root, relative)
#endif

enum { POLICE_MAX = 32 };

typedef struct {
    uint32_t actor, mode;
    int out, placed;
    float x, z;                 /* last position */
    float report_x, report_z;   /* where it was at the last 10 s line */
    float report_distance;
    DWORD report_at, sent_at;
} Cop;

static Cop s_cops[POLICE_MAX];
static int s_count = -1;        /* -1: no police manager */
static uint32_t s_manager;
static DWORD s_sampled, s_summary_at;
static unsigned s_sent_minute, s_off_minute;
static int s_enabled = -1;

static int readable(uint32_t address, size_t bytes)
{
    MEMORY_BASIC_INFORMATION info;
    uintptr_t p = XBOX_PTR(address);
    if (address < 0x10000 || address >= 0x08000000 || bytes > 0x08000000u - address)
        return 0;
    return VirtualQuery((void *)p, &info, sizeof info) && info.State == MEM_COMMIT &&
        !(info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
        p + bytes <= (uintptr_t)info.BaseAddress + info.RegionSize;
}

/* The race's AI setup copies its description to module + 0xE5E0
 * (sub_00158291): the cops wanted at +0xE66C (0 outside Cruise, from
 * sub_001E1E4A: the Cops setting unless the player type 0x407C20 is 2 or 3)
 * and the player type at +0xE674 (1 split screen: no police fill). */
#define MODULE 0x003C5780u
#define POLICE_WANTED 0xE66Cu
#define POLICE_PLAYERS 0xE674u

static uint32_t police_manager(void)
{
    uint32_t module = MEM32(MODULE), manager;
    if (!readable(module, 0xE670)) return 0;
    manager = MEM32(module + 0xE5D8);
    return readable(manager, 0x10) ? manager : 0;
}

static uint32_t cop_body(uint32_t actor)
{
    uint32_t controller = MEM32(actor + 0x28), vehicle, car, simulation, body;
    if (!readable(controller, 0x1A0)) return 0;
    vehicle = MEM32(controller + 0x19C);
    if (!readable(vehicle, 0x2C) || MEM32(vehicle) != 0x00383768u) return 0;
    car = MEM32(vehicle + 8);
    if (!readable(car, 0x48)) return 0;
    simulation = MEM32(car + 0x3C);
    if (!readable(simulation, 8)) return 0;
    body = MEM32(simulation + 4);
    if (!readable(body, 0xD4)) return 0;
    return isfinite(MEMF(body + 0xC8)) && isfinite(MEMF(body + 0xD0)) ? body : 0;
}

/* The pool's actors, in list order; -1 when there is no (readable) police
 * manager, as in menus. */
static int sample(uint32_t *manager_out, uint32_t actors[POLICE_MAX])
{
    uint32_t manager = police_manager(), repository, begin, end, entry;
    int count = 0;
    *manager_out = manager;
    if (!manager) return -1;
    repository = MEM32(manager + 4);
    if (!readable(repository, 0xC0)) return -1;
    begin = MEM32(repository + 0xB4); end = MEM32(repository + 0xB8);
    if (end < begin || (end - begin) % 8 || (end - begin) / 8 > POLICE_MAX) return -1;
    if (end > begin && !readable(begin, end - begin)) return -1;
    for (entry = begin; entry < end; entry += 8) {
        uint32_t actor = MEM32(entry);
        if (!readable(actor, 0x2954) || MEM32(actor) != 0x0037EE10u) return -1;
        actors[count++] = actor;
    }
    return count;
}

static float distance(float x, float z, float px, float pz)
{
    return sqrtf((x - px) * (x - px) + (z - pz) * (z - pz));
}

static void sampled_tick(uint32_t player_body)
{
    uint32_t manager, actors[POLICE_MAX];
    int count = sample(&manager, actors);
    unsigned i, j, out = 0;
    DWORD now = MM3_POLICE_NOW();
    float px = MEMF(player_body + 0xC8), pz = MEMF(player_body + 0xD0), nearest = -1.0f;

    if (count < 0) {
        if (s_count >= 0) MM3_POLICE_PRINT("[MM3_POLICE] police gone (race over)\n");
        s_count = -1; s_manager = 0;
        return;
    }
    if (manager != s_manager || s_count < 0) {
        float traffic = MEMF(0x407C64);
        uint32_t module = MEM32(MODULE);
        int wanted = readable(module, POLICE_PLAYERS + 4u) ? (int)MEM32(module + POLICE_WANTED) : -1;
        int players = readable(module, POLICE_PLAYERS + 4u) ? (int)MEM32(module + POLICE_PLAYERS) : -1;
        memset(s_cops, 0, sizeof s_cops);
        for (i = 0; i < (unsigned)count; ++i) s_cops[i].actor = actors[i];
        s_manager = manager; s_count = count;
        s_summary_at = now; s_sent_minute = s_off_minute = 0;
        MM3_POLICE_PRINT("[MM3_POLICE] city %u mode %u: %d police cars in the pool of %d wanted (Cops setting %d, "
                         "traffic %.2f, players %d/%u); a cop is sent after an offence (MM3_POLICE_LOG=0 turns this off)\n",
                         MEM32(0x407C18u), MEM32(0x407C1Cu), count, wanted, (int)MEM32(0x407C68u),
                         isfinite(traffic) ? traffic : -1.0f, players, MEM32(0x407C20u));
    } else if (count != s_count) {
        /* The game adds cops to a running pool (more after a few minutes of
         * chases): keep what is known about the cars already in it. */
        Cop kept[POLICE_MAX];
        memset(kept, 0, sizeof kept);
        for (i = 0; i < (unsigned)count; ++i) {
            kept[i].actor = actors[i];
            for (j = 0; j < (unsigned)s_count; ++j)
                if (s_cops[j].actor == actors[i]) { kept[i] = s_cops[j]; break; }
        }
        memcpy(s_cops, kept, sizeof s_cops);
        MM3_POLICE_PRINT("[MM3_POLICE] pool now %d police cars (was %d)\n", count, s_count);
        s_count = count;
    }
    for (i = 0; i < (unsigned)count; ++i) {
        Cop *cop = &s_cops[i];
        uint32_t mode, body;
        int now_out;
        if (cop->actor != actors[i]) { memset(cop, 0, sizeof *cop); cop->actor = actors[i]; }
        mode = MEM32(cop->actor + 0x2950);
        now_out = mode != 0;
        body = cop_body(cop->actor);
        if (body) { cop->x = MEMF(body + 0xC8); cop->z = MEMF(body + 0xD0); }
        if (now_out && !cop->out) {
            cop->out = 1; cop->placed = 0; cop->sent_at = now; ++s_sent_minute;
        } else if (!now_out && cop->out) {
            cop->out = 0; ++s_off_minute;
            MM3_POLICE_PRINT("[MM3_POLICE] cop %u called off after %lu s, %.0f m away\n", i + 1,
                             (unsigned long)((now - cop->sent_at) / 1000u),
                             body ? distance(cop->x, cop->z, px, pz) : -1.0f);
        }
        cop->mode = mode;
        if (!cop->out || !body) continue;
        ++out;
        {
            float d = distance(cop->x, cop->z, px, pz);
            if (nearest < 0.0f || d < nearest) nearest = d;
            if (!cop->placed) {
                if (fabsf(cop->x) + fabsf(cop->z) < 1.0f) continue;   /* not placed yet */
                cop->placed = 1;
                cop->report_at = now; cop->report_x = cop->x; cop->report_z = cop->z;
                cop->report_distance = d;
                MM3_POLICE_PRINT("[MM3_POLICE] cop %u sent (mode %u), %.0f m away at %.0f %.0f\n",
                                 i + 1, mode, d, cop->x, cop->z);
            } else if (now - cop->report_at >= 10000u) {
                float moved = distance(cop->x, cop->z, cop->report_x, cop->report_z);
                float change = d - cop->report_distance;
                const char *how = d <= 60.0f ? "on you" : moved < 5.0f ? "not moving (stuck?)" :
                    change < -20.0f ? "closing in" : "not gaining";
                MM3_POLICE_PRINT("[MM3_POLICE] cop %u %.0f m away (%+.0f m in 10 s, moved %.0f m): %s\n",
                                 i + 1, d, change, moved, how);
                cop->report_at = now; cop->report_x = cop->x; cop->report_z = cop->z;
                cop->report_distance = d;
            }
        }
    }
    if (now - s_summary_at >= 60000u) {
        if (nearest >= 0.0f)
            MM3_POLICE_PRINT("[MM3_POLICE] last 60 s: %u sent, %u called off; %u of %d out, nearest %.0f m\n",
                             s_sent_minute, s_off_minute, out, count, nearest);
        else
            MM3_POLICE_PRINT("[MM3_POLICE] last 60 s: %u sent, %u called off; %u of %d out\n",
                             s_sent_minute, s_off_minute, out, count);
        s_summary_at = now; s_sent_minute = s_off_minute = 0;
    }
}

static int log_enabled(void)
{
    if (s_enabled < 0) {
        const char *v = getenv("MM3_POLICE_LOG");
        s_enabled = !(v && !strcmp(v, "0"));
    }
    return s_enabled;
}

/* Why a pool stays empty. sub_00158291 sets up the city's AI pools and
 * stops at the first that fails: pedestrians, traffic, then the police fill
 * sub_001507DA (EAX the cops wanted, EDI the manager), which makes each car
 * with sub_0015070A and stops at the first car it can't make. The seam
 * (scripts/mm3_police_create_seam.cmake) notes each step that car creation
 * reached, so a car not made names its step. At most 48 lines a race. */
enum { POLICE_DIAGNOSTIC_LINES = 48 };
static unsigned s_diagnostic_lines;
static int s_step = -1;   /* the last step car creation reached; it stops where one returns 0 */

static int diagnostic_line(void)
{
    return log_enabled() && s_diagnostic_lines < POLICE_DIAGNOSTIC_LINES && ++s_diagnostic_lines;
}

static int pool_size(uint32_t manager)
{
    uint32_t repository, begin, end;
    if (!readable(manager, 0x10)) return -1;
    repository = MEM32(manager + 4);
    if (!readable(repository, 0xC0)) return -1;
    begin = MEM32(repository + 0xB4); end = MEM32(repository + 0xB8);
    return end < begin || (end - begin) % 8 ? -1 : (int)((end - begin) / 8);
}

extern void sub_00158291_original(void);
extern void sub_001507DA_original(void);

void sub_00158291(void)
{
    uint32_t module = g_ecx;
    s_diagnostic_lines = 0;   /* the cap is per race */
    sub_00158291_original();
    if (diagnostic_line()) {
        int saved_errno = errno;
        DWORD saved_error = GetLastError();
        int ok = readable(module, POLICE_PLAYERS + 4u);
        MM3_POLICE_PRINT("[MM3_POLICE] city AI setup %s: %d cops wanted, player type %d (0x407C20 = %u), "
                         "police cars in the pool %d\n", (g_eax & 0xFFu) ? "done" : "stopped early",
                         ok ? (int)MEM32(module + POLICE_WANTED) : -1, ok ? (int)MEM32(module + POLICE_PLAYERS) : -1,
                         MEM32(0x407C20u), ok ? pool_size(MEM32(module + 0xE5D8u)) : -1);
        SetLastError(saved_error);
        errno = saved_errno;
    }
}

void sub_001507DA(void)
{
    uint32_t wanted = g_eax, manager = g_edi, caller = MEM32(g_esp);
    int before = pool_size(manager), after;
    sub_001507DA_original();
    after = pool_size(manager);
    if ((after != (int)wanted || after != before) && diagnostic_line()) {
        int saved_errno = errno;
        DWORD saved_error = GetLastError();
        MM3_POLICE_PRINT("[MM3_POLICE] police fill from %06X: wanted %u, pool %d -> %d (%s)\n", caller, wanted,
                         before, after, (g_eax & 0xFFu) ? "done" : "a car could not be made");
        SetLastError(saved_error);
        errno = saved_errno;
    }
}

/* Inside the factory call (sub_0013BB36): the seam notes the factory's 0x82C
 * allocation, the constructed car, the car init sub_00220761 and the final
 * hand-over to the cop; the init's missing object handler, missing collision
 * files and model build sub_002202EE; the build's streamed or body model
 * (sub_001BDDD9 / sub_000E42B1, with the path it loads), its setup
 * sub_0021CB61 and physics sub_0021ED64. Only while a police car is made. */
enum {
    NOTE_ALLOC, NOTE_CAR, NOTE_INIT, NOTE_ATTACH,
    NOTE_HANDLER, NOTE_COLLISION, NOTE_BUILD,
    NOTE_STREAMED, NOTE_BODY, NOTE_SETUP, NOTE_PHYSICS, NOTE_COUNT
};
static int s_in_factory;
static unsigned s_noted;            /* a bit per note reached */
static uint32_t s_note[NOTE_COUNT];
static char s_note_path[96];

void mm3_police_step(int step)
{
    s_step = step;
    if (step == 5) s_in_factory = 0;   /* the factory returned */
}

void mm3_police_factory_begin(void)
{
    s_in_factory = log_enabled();
    s_noted = 0;
    s_note_path[0] = 0;
}

void mm3_police_note(int where, uint32_t value)
{
    if (!s_in_factory || where < 0 || where >= NOTE_COUNT) return;
    s_noted |= 1u << where;
    s_note[where] = value;
}

/* A std::string (length +0x14, capacity +0x18, text inline at +4 below 16
 * bytes of capacity, else pointed to from +4): the model being loaded. */
void mm3_police_note_path(uint32_t string_object)
{
    uint32_t length, text, i;
    int saved_errno;
    DWORD saved_error;
    if (!s_in_factory) return;
    saved_errno = errno;
    saved_error = GetLastError();
    s_note_path[0] = 0;
    if (readable(string_object, 0x1C)) {
        length = MEM32(string_object + 0x14);
        text = MEM32(string_object + 0x18) < 16 ? string_object + 4 : MEM32(string_object + 4);
        if (length < sizeof s_note_path && readable(text, length)) {
            for (i = 0; i < length; ++i) s_note_path[i] = (char)MEM8(text + i);
            s_note_path[length] = 0;
        }
    }
    SetLastError(saved_error);
    errno = saved_errno;
}

#define NOTE_REACHED(n) (s_noted & (1u << (n)))
#define NOTE_FAILED(n) (NOTE_REACHED(n) && !s_note[n])

/* What the factory could not do, from the notes it reached; `body` is set
 * when that was the body model, whose file the log then looks for. */
static const char *factory_reason(int *with_path, int *body)
{
    *with_path = 0;
    *body = 0;
    if (!NOTE_REACHED(NOTE_ALLOC)) return "it stopped before making the car";
    if (!s_note[NOTE_ALLOC]) return "no memory for the car (0x82C bytes)";
    if (NOTE_FAILED(NOTE_CAR)) return "the car could not be constructed (sub_0021B40A)";
    if (NOTE_FAILED(NOTE_INIT)) {
        *with_path = 1;
        if (NOTE_REACHED(NOTE_HANDLER)) { *with_path = 0; return "the car init found no game object handler (sub_00220761)"; }
        if (NOTE_REACHED(NOTE_COLLISION)) return "the car model has no collision file (Cars\\<model>[_lod1]\\data.ccol), model";
        if (NOTE_FAILED(NOTE_STREAMED)) return "the streamed car model did not load (sub_001BDDD9)";
        if (NOTE_FAILED(NOTE_BODY)) { *body = 1; return "the car body model did not load (sub_000E42B1)"; }
        if (NOTE_FAILED(NOTE_SETUP)) return "the car model setup failed (sub_0021CB61) after loading";
        if (NOTE_FAILED(NOTE_PHYSICS)) return "the car physics did not load (sub_0021ED64) for";
        return "the car init failed (sub_00220761) after";
    }
    if (NOTE_FAILED(NOTE_ATTACH)) return "the cop did not take the car (actor + 0x2954, vtable + 4)";
    return "no failing step noted";
}

/* The body model did not load: is its file in the game folder at all? The
 * engine looks in the cache, each DLC pack, then Data\XBox and Data\Shared,
 * and the police models are loose disc files in the last two. A file the
 * folder lacks is found nowhere; one that is there but unusable is a
 * different fault, so the line says which. */
static void model_file_line(const char *path)
{
    const char *relative = path[0] == '+' ? path + 1 : path;
    long long shared = MM3_POLICE_FILE_SIZE("Shared", relative);
    long long xbox = MM3_POLICE_FILE_SIZE("XBox", relative);
    if (shared > 0 || xbox > 0)
        MM3_POLICE_PRINT("[MM3_POLICE]   the model file %s is on disk (%lld bytes in Data\\%s), so it was found "
                         "but could not be loaded\n", relative, shared > 0 ? shared : xbox,
                         shared > 0 ? "Shared" : "XBox");
    else if (shared == 0 || xbox == 0)
        MM3_POLICE_PRINT("[MM3_POLICE]   the model file %s is on disk but empty (0 bytes): the copy is damaged; "
                         "run MM3-Setup with your disc image again to restore it\n", relative);
    else
        MM3_POLICE_PRINT("[MM3_POLICE]   the model file %s is NOT in this game folder (Data\\Shared and "
                         "Data\\XBox searched): the folder is missing a disc file, so no cop can be made. Run "
                         "MM3-Setup with your disc image again to restore the missing files; saves, DLC and "
                         "settings are kept\n", relative);
}

/* At the return of sub_0015070A: EAX the cop made, or 0; `name` its type. */
void mm3_police_created(uint32_t actor, uint32_t name)
{
    static const char *const stopped[] = {
        "the police car type was not found (sub_0013D5C1)",
        "the type is not a police car (sub_00252F16)",
        "no place for it (sub_0006415F)",
        "no memory for the cop (0x298C bytes)",
        "the police factory refused it (sub_0013BB36)",
    };
    char text[48] = "?";
    int step = s_step;
    uint32_t i;
    s_step = -1;
    s_in_factory = 0;
    if (!diagnostic_line()) return;
    {
        int saved_errno = errno;
        DWORD saved_error = GetLastError();
        if (readable(name, 1)) {
            for (i = 0; i + 1 < sizeof text && readable(name + i, 1) && MEM8(name + i); ++i) text[i] = (char)MEM8(name + i);
            text[i] = 0;
        }
        if (actor) {
            MM3_POLICE_PRINT("[MM3_POLICE] police car \"%s\" made\n", text);
        } else if (step == 5) {
            int with_path, body;
            const char *why = factory_reason(&with_path, &body);
            if (with_path && s_note_path[0])
                MM3_POLICE_PRINT("[MM3_POLICE] police car \"%s\" not made: %s: %s \"%s\"\n", text, stopped[4], why,
                                 s_note_path);
            else
                MM3_POLICE_PRINT("[MM3_POLICE] police car \"%s\" not made: %s: %s\n", text, stopped[4], why);
            if (body && s_note_path[0]) model_file_line(s_note_path);
        } else {
            MM3_POLICE_PRINT("[MM3_POLICE] police car \"%s\" not made: %s\n", text,
                             step < 0 ? "the police manager was not ready (manager + 8 is 0)" :
                             step >= 1 && step <= 4 ? stopped[step - 1] : "?");
        }
        SetLastError(saved_error);
        errno = saved_errno;
    }
}

void mm3_police_log_tick(uint32_t player_body)
{
    DWORD now;
    if (!log_enabled() || !player_body) return;
    now = MM3_POLICE_NOW();
    if (s_sampled && now - s_sampled < 1000u) return;
    s_sampled = now;
    {
        /* Nothing here may leak into the game thread's host state. */
        int saved_errno = errno;
        DWORD saved_error = GetLastError();
        sampled_tick(player_body);
        SetLastError(saved_error);
        errno = saved_errno;
    }
}
