#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "recomp_types.h"
#include "mm3_light_queue.h"

/* Diagnostic watch on the native volume builder (0x13068: the record in EAX;
 * argument 2 the output polygon of 0x40-byte screen vertices, 8 the camera's
 * near plane, 9 its offset, 10 and 12 two camera matrices; it returns the
 * vertex count, 0 when the light is not drawn). Each frame's drawn lights are
 * kept for three frames and the middle frame is compared with its
 * neighbours: a light whose record jumps for one frame, one that vanishes or
 * appears for one frame, one whose record holds still while its screen
 * polygon jumps, and camera inputs that jump, are logged with what changed
 * ([MM3_LIGHT_JUMP], at most 120 lines a run and a count every minute). Push
 * order is a sorted insert, so a light is found again by type, size and the
 * nearest position, not by its record. On with MM3_LIGHT_WATCH=1 only: it
 * matches every light against both neighbouring frames on the game thread,
 * the limit at long view distances, and its question is answered (the
 * volumes are built correctly). Steam launches had it on until 2.0. */
extern void sub_00013068_original(void);

#define WATCH_LIGHTS 384u
#define WATCH_CAMERA 37u            /* near plane 4, offset 1, two 4x4 matrices */
#define WATCH_LOG_LINES 120u
typedef struct {
    uint32_t type, color, count, cls;
    float pos[3], fwd[3], size[3];
    float box[4];                   /* the polygon's bounds, clamped near the screen */
} WatchLight;
typedef struct {
    uint32_t frame, n;
    int camera_seen;
    float camera[WATCH_CAMERA], eye[3];
    double ms;                      /* QPC milliseconds, as the frame CSV and [ODD_FRAME] */
    WatchLight light[WATCH_LIGHTS];
} WatchFrame;
static WatchFrame s_watch[4];
static uint32_t s_watch_seq;        /* frames started; s_watch[seq % 4] is being filled */
static int s_watch_on = -1;
static unsigned s_watch_lines;
static struct { unsigned moved, turned, vanished, appeared, polygon, hidden, camera, frames; } s_watch_count;
static clock_t s_watch_report;

static int light_watch_enabled(void)
{
    if (s_watch_on < 0) {
        const char *setting = getenv("MM3_LIGHT_WATCH");
        s_watch_on = setting && *setting && strcmp(setting, "0") != 0;
        mm3_light_queue_watch_classes(s_watch_on);
        if (s_watch_on)
            fprintf(stderr, "[MM3_LIGHT_JUMP] watching light volumes for one-frame jumps "
                    "(MM3_LIGHT_WATCH=0 disables)\n");
    }
    return s_watch_on;
}

static float watch_distance(const float a[3], const float b[3])
{
    float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return sqrtf(x * x + y * y + z * z);
}

static int watch_same_kind(const WatchLight *a, const WatchLight *b)
{
    unsigned k;
    if (a->type != b->type || a->cls != b->cls) return 0;
    for (k = 0; k != 3; ++k)
        if (fabsf(a->size[k] - b->size[k]) > 0.05f * fmaxf(1.0f, fabsf(a->size[k]))) return 0;
    return 1;
}

/* The nearest light of the same kind; among lights at the same place (a
 * beacon's back-to-back spots), the one aimed the same way. */
static const WatchLight *watch_match(const WatchFrame *f, const WatchLight *l, float *distance)
{
    const WatchLight *best = NULL;
    float best_distance = FLT_MAX, best_dot = -2.0f;
    uint32_t i;
    for (i = 0; i != f->n; ++i) {
        const WatchLight *c = &f->light[i];
        float d, dot;
        if (!watch_same_kind(c, l)) continue;
        d = watch_distance(c->pos, l->pos);
        dot = c->fwd[0] * l->fwd[0] + c->fwd[1] * l->fwd[1] + c->fwd[2] * l->fwd[2];
        if (d < best_distance - 0.05f || (d <= best_distance + 0.05f && dot > best_dot)) {
            if (d < best_distance) best_distance = d;
            best_dot = dot;
            best = c;
        }
    }
    *distance = best ? watch_distance(best->pos, l->pos) : FLT_MAX;
    return best;
}

static const char *watch_class_name(uint32_t cls)
{
    return cls == 2u ? "the driver's car" : cls == 1u ? "a car" : "the world";
}

static void watch_light_text(char *text, size_t size, const WatchLight *l)
{
    snprintf(text, size, "%s %06X %.1fx%.1fx%.1f m of %s at (%.2f %.2f %.2f) aim (%.2f %.2f %.2f), "
             "%u vertices, box %.0f,%.0f-%.0f,%.0f", l->type == 1u ? "spot" : "point", (unsigned)l->color,
             l->size[0], l->size[1], l->size[2], watch_class_name(l->cls), l->pos[0], l->pos[1], l->pos[2],
             l->fwd[0], l->fwd[1], l->fwd[2], (unsigned)l->count, l->box[0], l->box[1], l->box[2], l->box[3]);
}

/* At most three lines a frame and a share of the run's lines per kind, so a
 * burst (a cutscene's first frames) cannot use up the run. */
static unsigned s_watch_frame_lines, s_watch_kind_lines[8];
static uint32_t s_watch_lines_frame;

static void watch_log(const WatchFrame *m, unsigned kind, const char *what, const WatchLight *before,
                      const WatchLight *during, const WatchLight *after)
{
    char a[320], b[320], c[320];
    if (s_watch_lines_frame != m->frame) { s_watch_lines_frame = m->frame; s_watch_frame_lines = 0; }
    if (s_watch_lines >= WATCH_LOG_LINES || s_watch_frame_lines >= 3u || kind >= 8u ||
        s_watch_kind_lines[kind] >= WATCH_LOG_LINES / 4u) return;
    ++s_watch_lines;
    ++s_watch_frame_lines;
    ++s_watch_kind_lines[kind];
    if (before) watch_light_text(a, sizeof a, before); else snprintf(a, sizeof a, "none");
    if (during) watch_light_text(b, sizeof b, during); else snprintf(b, sizeof b, "none");
    if (after) watch_light_text(c, sizeof c, after); else snprintf(c, sizeof c, "none");
    fprintf(stderr, "[MM3_LIGHT_JUMP] %s for one frame (light frame %u, qpc_ms=%.1f, eye %.1f %.1f %.1f): before %s | "
            "then %s | after %s\n", what, (unsigned)m->frame, m->ms, m->eye[0], m->eye[1], m->eye[2], a, b, c);
}

/* The middle of three consecutive frames against its neighbours. */
static void light_watch_judge(const WatchFrame *p, const WatchFrame *m, const WatchFrame *n)
{
    uint32_t i, k;
    ++s_watch_count.frames;
    if (p->camera_seen && m->camera_seen && n->camera_seen) {
        int worst = -1;
        float worst_excess = 0.0f;
        for (k = 0; k != WATCH_CAMERA; ++k) {
            float mean = 0.5f * (p->camera[k] + n->camera[k]), spread = 0.5f * fabsf(n->camera[k] - p->camera[k]);
            float excess = fabsf(m->camera[k] - mean) - (4.0f * spread + 0.01f * (1.0f + fabsf(mean)));
            if (excess > worst_excess) { worst_excess = excess; worst = (int)k; }
        }
        if (worst >= 0) {
            ++s_watch_count.camera;
            if (s_watch_lines < WATCH_LOG_LINES) {
                ++s_watch_lines;
                fprintf(stderr, "[MM3_LIGHT_JUMP] camera input %d (%s) jumped for one frame (light frame %u): "
                        "%.4f | %.4f | %.4f; near plane %.3f %.3f %.3f %.3f offset %.3f\n", worst,
                        worst < 4 ? "near plane" : worst == 4 ? "near offset" : worst < 21 ? "matrix 10" : "matrix 12",
                        (unsigned)m->frame, p->camera[worst], m->camera[worst], n->camera[worst],
                        m->camera[0], m->camera[1], m->camera[2], m->camera[3], m->camera[4]);
            }
        }
    }
    for (i = 0; i != m->n; ++i) {
        const WatchLight *l = &m->light[i], *before, *after;
        float d_before, d_after, d_span, mid[3], off;
        before = watch_match(p, l, &d_before);
        after = watch_match(n, l, &d_after);
        if (!before && !after) {
            ++s_watch_count.appeared;
            if (l->count) watch_log(m, 5u, "a light appeared", NULL, l, NULL);
            continue;
        }
        if (!before || !after) continue;
        d_span = watch_distance(before->pos, after->pos);
        for (k = 0; k != 3; ++k) mid[k] = 0.5f * (before->pos[k] + after->pos[k]);
        off = watch_distance(l->pos, mid);
        /* Away from both neighbours, which agree: not a car moving on, nor
         * traffic placed elsewhere (a step that stays). */
        if (off > 0.5f + 0.25f * d_span && fminf(d_before, d_after) > 0.5f + 0.5f * d_span) {
            ++s_watch_count.moved;
            watch_log(m, 0u, "a light's record moved", before, l, after);
            continue;
        }
        if (l->type == 1u) {
            float dot_before = l->fwd[0] * before->fwd[0] + l->fwd[1] * before->fwd[1] + l->fwd[2] * before->fwd[2];
            float dot_span = after->fwd[0] * before->fwd[0] + after->fwd[1] * before->fwd[1] + after->fwd[2] * before->fwd[2];
            if (dot_span > 0.995f && dot_before < 0.97f) {
                ++s_watch_count.turned;
                watch_log(m, 1u, "a spot's aim turned", before, l, after);
                continue;
            }
        }
        if (before->count && after->count && !l->count) {
            ++s_watch_count.hidden;
            watch_log(m, 2u, "a light's volume was not drawn", before, l, after);
            continue;
        }
        if (before->count && after->count && l->count) {
            for (k = 0; k != 4; ++k) {
                float mean = 0.5f * (before->box[k] + after->box[k]), spread = 0.5f * fabsf(after->box[k] - before->box[k]);
                if (fabsf(l->box[k] - mean) > 4.0f * spread + 12.0f) break;
            }
            if (k != 4) {
                ++s_watch_count.polygon;
                watch_log(m, 3u, "a steady light's polygon jumped", before, l, after);
            }
        }
    }
    for (i = 0; i != p->n; ++i) {
        const WatchLight *l = &p->light[i], *after, *during;
        float d_after, d_during;
        if (!l->count) continue;
        after = watch_match(n, l, &d_after);
        if (!after || !after->count || d_after > 0.5f + (l->cls ? 8.0f : 0.0f)) continue;
        during = watch_match(m, l, &d_during);
        if (!during || d_during > 1.0f + d_after) {
            ++s_watch_count.vanished;
            watch_log(m, 4u, "a light vanished", l, during && d_during < 20.0f ? during : NULL, after);
        }
    }
}

static void light_watch_report(void)
{
    clock_t now = clock();
    if (!s_watch_report) { s_watch_report = now; return; }
    if (now - s_watch_report < 60 * CLOCKS_PER_SEC) return;
    fprintf(stderr, "[MM3_LIGHT_JUMP] last 60 s: %u light frames judged; one-frame changes: %u records moved, "
            "%u spots turned, %u lights vanished, %u appeared, %u volumes not drawn, %u steady lights' polygons "
            "jumped, %u camera inputs jumped\n", s_watch_count.frames, s_watch_count.moved, s_watch_count.turned,
            s_watch_count.vanished, s_watch_count.appeared, s_watch_count.hidden, s_watch_count.polygon,
            s_watch_count.camera);
    memset(&s_watch_count, 0, sizeof s_watch_count);
    s_watch_report = now;
}

static WatchFrame *light_watch_frame(void)
{
    WatchFrame *f = &s_watch[s_watch_seq % 4u];
    if (s_watch_seq && f->frame == mm3_light_queue_frame_number()) return f;
    ++s_watch_seq;
    if (s_watch_seq >= 4u) {
        const WatchFrame *p = &s_watch[(s_watch_seq - 3u) % 4u], *m = &s_watch[(s_watch_seq - 2u) % 4u];
        const WatchFrame *n = &s_watch[(s_watch_seq - 1u) % 4u];
        if (m->frame == p->frame + 1u && n->frame == m->frame + 1u) light_watch_judge(p, m, n);
        light_watch_report();
    }
    f = &s_watch[s_watch_seq % 4u];
    f->frame = mm3_light_queue_frame_number();
    f->n = 0;
    f->camera_seen = 0;
    {
        LARGE_INTEGER now, freq;
        f->ms = QueryPerformanceCounter(&now) && QueryPerformanceFrequency(&freq) && freq.QuadPart ?
                (double)now.QuadPart * 1000.0 / (double)freq.QuadPart : 0.0;
    }
    if (!mm3_light_queue_last_eye(f->eye)) memset(f->eye, 0, sizeof f->eye);
    return f;
}

static int watch_address(uint32_t address, uint32_t size)
{
    return address >= 0x10000u && address <= 0x08000000u - size;
}

/* A light drawn last frame that the builder rejected now: what its frame (still
 * below the caller's stack, entry ESP `args`) held. The builder's EBP is
 * args - 0x24: corner depths past the near plane at EBP-0x20, the in-front
 * flag at EBP+0x1F, removed corners at EBP+8, the vertex count (after the
 * outline) at EBP+0x18, clipped corners at EBP-0xE0 and projected points at
 * EBP-0x260 (12 bytes each); the screen bounds go to arguments 3, 4, 6, 7. */
static unsigned s_watch_dumps;

static void light_watch_dump(const WatchLight *l, uint32_t args, uint32_t plane, uint32_t offset_bits,
                             const float eye[3])
{
    uint32_t ebp = args - 0x24u, k, count;
    float offset;
    if (s_watch_dumps >= 8u || !watch_address(args - 0x290u, 0x2F0u) || !watch_address(plane, 16)) return;
    ++s_watch_dumps;
    memcpy(&offset, &offset_bits, sizeof offset);
    count = MEM32(ebp + 0x18u);
    fprintf(stderr, "[MM3_LIGHT_JUMP] rejected %s %06X %.1fx%.1fx%.1f at (%.2f %.2f %.2f) aim (%.2f %.2f %.2f), "
            "eye %.2f %.2f %.2f; near plane %.4f %.4f %.4f %.3f offset %.3f; in front %u, outline %u; depths",
            l->type == 1u ? "spot" : "point", (unsigned)l->color, l->size[0], l->size[1], l->size[2],
            l->pos[0], l->pos[1], l->pos[2], l->fwd[0], l->fwd[1], l->fwd[2], eye[0], eye[1], eye[2],
            MEMF(plane), MEMF(plane + 4u), MEMF(plane + 8u), MEMF(plane + 12u), offset,
            (unsigned)MEM8(ebp + 0x1Fu), (unsigned)count);
    for (k = 0; k != 8; ++k) fprintf(stderr, " %.3f%s", MEMF(ebp - 0x20u + k * 4u), MEM8(ebp + 8u + k) ? "x" : "");
    fprintf(stderr, "; bounds");
    for (k = 0; k != 4; ++k) {
        uint32_t pointer = MEM32(args + (k < 2 ? 12u + k * 4u : 24u + (k - 2u) * 4u));
        fprintf(stderr, " %.4f", watch_address(pointer, 4) ? MEMF(pointer) : -999.0f);
    }
    fprintf(stderr, "; clipped");
    for (k = 0; k != 16; ++k)
        fprintf(stderr, " (%.2f %.2f %.2f)", MEMF(ebp - 0xE0u + k * 12u), MEMF(ebp - 0xDCu + k * 12u),
                MEMF(ebp - 0xD8u + k * 12u));
    fprintf(stderr, "; projected");
    for (k = 0; k != 16; ++k)
        fprintf(stderr, " (%.1f %.1f %.4f)", MEMF(ebp - 0x260u + k * 12u), MEMF(ebp - 0x25Cu + k * 12u),
                MEMF(ebp - 0x258u + k * 12u));
    fputc('\n', stderr);
}

static void light_watch_note(uint32_t record, uint32_t output, uint32_t count, uint32_t plane,
                             uint32_t offset_bits, uint32_t matrix_a, uint32_t matrix_b, uint32_t args)
{
    WatchFrame *f = light_watch_frame();
    WatchLight *l;
    uint32_t k;
    if (!f->camera_seen && watch_address(plane, 16) && watch_address(matrix_a, 64) && watch_address(matrix_b, 64)) {
        float offset;
        memcpy(&offset, &offset_bits, sizeof offset);
        for (k = 0; k != 4; ++k) f->camera[k] = MEMF(plane + k * 4u);
        f->camera[4] = offset;
        for (k = 0; k != 16; ++k) {
            f->camera[5 + k] = MEMF(matrix_a + k * 4u);
            f->camera[21 + k] = MEMF(matrix_b + k * 4u);
        }
        f->camera_seen = 1;
        for (k = 0; k != WATCH_CAMERA; ++k)
            if (!isfinite(f->camera[k])) f->camera_seen = 0;
    }
    if (f->n >= WATCH_LIGHTS || !watch_address(record, 0x54u)) return;
    l = &f->light[f->n++];
    l->type = MEM32(record + 0x50);
    l->color = MEM32(record + 0x4C) & 0x00FFFFFFu;
    l->cls = mm3_light_queue_watch_class(record);
    for (k = 0; k != 3; ++k) {
        l->pos[k] = MEMF(record + 0x30 + k * 4u);
        l->fwd[k] = MEMF(record + 0x20 + k * 4u);
        l->size[k] = MEMF(record + 0x40 + k * 4u);
    }
    l->count = count <= 64u ? count : 0u;
    if (!watch_address(output, 0x40u * 64u)) l->count = 0;
    l->box[0] = l->box[1] = 1e9f;
    l->box[2] = l->box[3] = -1e9f;
    for (k = 0; k != l->count; ++k) {
        float x = MEMF(output + k * 0x40u), y = MEMF(output + k * 0x40u + 4u);
        if (!isfinite(x) || !isfinite(y)) continue;
        x = fminf(fmaxf(x, -64.0f), 704.0f);
        y = fminf(fmaxf(y, -64.0f), 544.0f);
        l->box[0] = fminf(l->box[0], x); l->box[1] = fminf(l->box[1], y);
        l->box[2] = fmaxf(l->box[2], x); l->box[3] = fmaxf(l->box[3], y);
    }
    if (!l->count || l->box[0] > l->box[2]) l->box[0] = l->box[1] = l->box[2] = l->box[3] = 0.0f;
    if (!l->count && s_watch_seq) {
        const WatchFrame *p = &s_watch[(s_watch_seq - 1u) % 4u];
        float d;
        const WatchLight *before = p->frame + 1u == f->frame ? watch_match(p, l, &d) : NULL;
        /* Only lights that covered the screen a frame ago: a light leaving
         * the view at its edge is rejected rightly. */
        if (before && before->count && d < 0.05f && before->box[2] - before->box[0] >= 600.0f &&
            before->box[3] - before->box[1] >= 450.0f)
            light_watch_dump(l, args, plane, offset_bits, f->eye);
    }
}

void sub_00013068(void)
{
    uint32_t record = g_eax, args = g_esp, output = 0, plane = 0, offset = 0, matrix_a = 0, matrix_b = 0;
    int watch = light_watch_enabled();
    if (watch && watch_address(args, 52)) {
        output = MEM32(args + 8);
        plane = MEM32(args + 32);
        offset = MEM32(args + 36);
        matrix_a = MEM32(args + 40);
        matrix_b = MEM32(args + 48);
    }
    sub_00013068_original();
    if (watch) light_watch_note(record, output, g_eax, plane, offset, matrix_a, matrix_b, args);
}
