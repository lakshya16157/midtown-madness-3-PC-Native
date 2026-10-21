#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#ifndef MM3_ZONE_CLOCK
#define MM3_ZONE_CLOCK() GetTickCount64()
#endif
#include <errno.h>
#include <fenv.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "mm3_recomp_types.h"
#include "mm3_zone_diagnostics.h"

/* Mission zones: the green circles of Undercover pickups and drop-offs, such
 * as the money bags of Paris "Go for the gold". A script enables them per
 * player with playerEnableZone. Each frame the player's update (sub_0020F1AB)
 * works out its car's seven reference points and, only while +0x14A is set
 * (the vehicle's +0x9B "placed" byte from the previous frame), tests them
 * against every enabled zone in the player's list at +0x48 (sub_0020DFC9,
 * sub_0020E02E, sub_0020943A): a point no farther from a zone's centre than
 * its radius fires the script's zone() event, which normally disables that
 * zone. This observer runs just before that check, writes no guest memory and
 * calls no guest code. At most 200 lines a run:
 *   [MM3_ZONE] player P: N zones enabled (was M), zone check on|off
 *     whenever the count changes (a pickup that counted lowers it by one);
 *   [MM3_ZONE] player P: car inside a zone at (x, y, z), D m from its centre
 *       (radius R m), zone check on|off
 *     each time the car enters an enabled zone by the native rule;
 *   [MM3_ZONE] player P: still inside a zone after 2 s with the zone check
 *       on; the zone was not disabled
 *     once per entry, if nothing changed by then. */

#ifndef MM3_ZONE_LOG
#define MM3_ZONE_LOG(...) fprintf(stderr, __VA_ARGS__)
#endif
#define ZONE_PLAYERS 4u
#define ZONE_LIST_LIMIT 64u
#define ZONE_LINE_LIMIT 200u
#define ZONE_STUCK_MS 2000u

typedef struct {
    uint32_t player;
    int count, inside, gate, stuck_reported;
    uint64_t inside_since;
} ZonePlayer;

static ZonePlayer s_zone_players[ZONE_PLAYERS];
static unsigned s_zone_next, s_zone_lines;

static int zone_address(uint32_t address, uint32_t size)
{
    return address >= 0x10000u && address <= 0x08000000u - size;
}

static ZonePlayer *zone_player(uint32_t player, int create)
{
    unsigned i;
    for (i = 0; i != ZONE_PLAYERS; ++i)
        if (s_zone_players[i].player == player) return &s_zone_players[i];
    if (!create) return NULL;
    i = s_zone_next++ % ZONE_PLAYERS;
    memset(&s_zone_players[i], 0, sizeof s_zone_players[i]);
    s_zone_players[i].player = player;
    s_zone_players[i].gate = -1;
    return &s_zone_players[i];
}

static int zone_line(void)
{
    if (s_zone_lines >= ZONE_LINE_LIMIT) return 0;
    ++s_zone_lines;
    return 1;
}

void mm3_zone_status(uint32_t player, uint32_t points)
{
    uint32_t head, node;
    unsigned walked = 0, k;
    int count = 0, inside = 0, gate, printed = 0, saved_errno;
    float centre[3] = { 0 }, distance = 0.0f, radius = 0.0f;
    unsigned long saved_error;
    fenv_t saved_fp;
    ZonePlayer *p;

    if (!zone_address(player, 0x14C) || !zone_address(points, 7u * 12u)) return;
    head = MEM32(player + 0x48);
    if (!zone_address(head, 12)) return;
    if (MEM32(head) == head && !zone_player(player, 0)) return;
    saved_errno = errno;
    saved_error = GetLastError();
    fegetenv(&saved_fp);
    for (node = MEM32(head); node != head && walked < ZONE_LIST_LIMIT; node = MEM32(node), ++walked) {
        uint32_t part;
        float c[3], r, d = INFINITY;
        if (!zone_address(node, 12)) break;
        part = MEM32(node + 8) + 0x18u;
        if (!MEM32(node + 8) || !zone_address(part, 0x72) || !MEM8(part + 0x71)) continue;
        ++count;
        c[0] = MEMF(part + 0x48); c[1] = MEMF(part + 0x4C); c[2] = MEMF(part + 0x50);
        r = MEMF(part + 0x5C);
        for (k = 0; k != 7; ++k) {
            float dx = MEMF(points + k * 12u) - c[0];
            float dy = MEMF(points + k * 12u + 4u) - c[1];
            float dz = MEMF(points + k * 12u + 8u) - c[2];
            float e = sqrtf(dx * dx + dy * dy + dz * dz);
            if (e < d) d = e;
        }
        if (d <= r && !inside) {
            inside = 1;
            memcpy(centre, c, sizeof centre);
            distance = d;
            radius = r;
        }
    }
    gate = MEM8(player + 0x14A) != 0;
    p = zone_player(player, count != 0);
    if (p) {
        uint64_t now = MM3_ZONE_CLOCK();
        if (count != p->count) {
            if (zone_line()) {
                MM3_ZONE_LOG("[MM3_ZONE] player %08X: %d zone%s enabled (was %d), zone check %s\n",
                             (unsigned)player, count, count == 1 ? "" : "s", p->count, gate ? "on" : "off");
                printed = 1;
            }
            p->inside_since = now;
            p->stuck_reported = 0;
        } else if (count && p->gate >= 0 && gate != p->gate && zone_line()) {
            MM3_ZONE_LOG("[MM3_ZONE] player %08X: zone check %s with %d zone%s enabled\n",
                         (unsigned)player, gate ? "on" : "off", count, count == 1 ? "" : "s");
            printed = 1;
        }
        if (inside && !p->inside) {
            p->inside_since = now;
            p->stuck_reported = 0;
            if (zone_line()) {
                MM3_ZONE_LOG("[MM3_ZONE] player %08X: car inside a zone at (%.1f, %.1f, %.1f), %.1f m from its "
                             "centre (radius %.1f m), zone check %s\n", (unsigned)player, centre[0], centre[1],
                             centre[2], distance, radius, gate ? "on" : "off");
                printed = 1;
            }
        } else if (inside && gate && !p->stuck_reported && now - p->inside_since >= ZONE_STUCK_MS) {
            p->stuck_reported = 1;
            if (zone_line()) {
                MM3_ZONE_LOG("[MM3_ZONE] player %08X: still inside a zone after %u s with the zone check on; "
                             "the zone was not disabled\n", (unsigned)player, ZONE_STUCK_MS / 1000u);
                printed = 1;
            }
        }
        p->count = count;
        p->inside = inside;
        p->gate = gate;
        if (printed) fflush(stderr);
    }
    fesetenv(&saved_fp);
    SetLastError(saved_error);
    errno = saved_errno;
}
