/* Bounded, opt-in observation of the retail asynchronous route query lifetime.
 * No guest calls or writes. A shared fixed ring connects producer/consumer
 * threads without assuming that the game's queue is serviced on its producer. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <errno.h>
#include <fenv.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <xmmintrin.h>
#include "mm3_recomp_types.h"
#include "mm3_route_diagnostics.h"

#ifndef MM3_ROUTE_LOG
#define MM3_ROUTE_LOG(...) fprintf(stderr, __VA_ARGS__)
#endif
#define ROUTE_RING 256u
#define ROUTE_RECENT 16u
typedef struct RouteEvent {
    uint64_t sequence, qpc, prior_enqueue;
    uint32_t thread, kind, scheduler, node, record, key;
    uint32_t graph, default_graph, optional_graph, vtable, target;
    uint32_t words[8], callers[3], queue_count, queue_offset;
    uint32_t iterator_index, iterator_container, worker;
    uint32_t queue_node_occurrences, queue_scan_count;
    int graph_read, target_read;
} RouteEvent;
typedef struct RouteContext {
    int error_number, fp_valid;
    DWORD last_error;
    unsigned mxcsr;
    fenv_t fp;
} RouteContext;
static volatile LONG route_enabled = -1;
static SRWLOCK route_lock = SRWLOCK_INIT;
static RouteEvent route_enqueues[ROUTE_RING], route_events[ROUTE_RING];
static uint64_t route_sequence;
static unsigned enqueue_position, event_position;
static unsigned baseline_reports, bad_reports, duplicate_reports;

static int enabled(void)
{
    LONG cached = route_enabled;
    if (cached < 0) {
        int saved_errno = errno;
        DWORD saved_error = GetLastError();
        char value[8];
        DWORD length = GetEnvironmentVariableA("MM3_ROUTE_DIAGNOSTICS", value, sizeof value);
        cached = length == 1 && value[0] == '1';
        InterlockedCompareExchange(&route_enabled, cached, -1);
        errno = saved_errno;
        SetLastError(saved_error);
    }
    return cached != 0;
}

static RouteContext begin_observation(void)
{
    RouteContext saved;
    saved.error_number = errno;
    saved.last_error = GetLastError();
    saved.mxcsr = _mm_getcsr();
    saved.fp_valid = feholdexcept(&saved.fp) == 0;
    return saved;
}

static void end_observation(const RouteContext *saved)
{
    if (saved->fp_valid) fesetenv(&saved->fp);
    _mm_setcsr(saved->mxcsr);
    errno = saved->error_number;
    SetLastError(saved->last_error);
}

static int read_guest(uint32_t address, void *destination, size_t bytes)
{
    SIZE_T done = 0;
    if (address < 0x10000u || address >= 0x08000000u || bytes > 0x08000000u-address)
        return 0;
    return ReadProcessMemory(GetCurrentProcess(), (const void *)XBOX_PTR(address),
                             destination, bytes, &done) && done == bytes;
}

static uint32_t word(uint32_t address)
{
    uint32_t value = 0;
    read_guest(address, &value, sizeof value);
    return value;
}

static void capture_graph(RouteEvent *event)
{
    event->graph_read = read_guest(event->graph, event->words, sizeof event->words);
    event->vtable = word(event->graph);
    if (event->vtable <= 0xFFFFFFFBu)
        event->target_read = read_guest(event->vtable + 4u, &event->target, sizeof event->target);
}

static void capture(RouteEvent *event, uint32_t frame)
{
    LARGE_INTEGER qpc;
    event->thread = GetCurrentThreadId();
    if (QueryPerformanceCounter(&qpc)) event->qpc = (uint64_t)qpc.QuadPart;
    if (event->scheduler) {
        event->default_graph = word(event->scheduler + 0x24u);
        event->queue_count = word(event->scheduler + 0x20u);
        event->queue_offset = word(event->scheduler + 0x1Cu);
    }
    for (unsigned i = 0; frame && i < 3; ++i) {
        uint32_t pair[2];
        if (!read_guest(frame, pair, sizeof pair)) break;
        event->callers[i] = pair[1];
        if (pair[0] <= frame) break;
        frame = pair[0];
    }
    capture_graph(event);
}

static void scan_queue(RouteEvent *event)
{
    uint32_t map = word(event->scheduler + 0x14u);
    uint32_t size = word(event->scheduler + 0x18u);
    if (!size || size > 0x100000u || event->queue_offset >= size*4u ||
        event->queue_count > size*4u) return;
    uint32_t limit = event->queue_count < 128u ? event->queue_count : 128u;
    for (uint32_t i = 0; i < limit; ++i) {
        uint32_t index = event->queue_offset+i, block_index = index/4u;
        if (block_index >= size) block_index -= size;
        uint32_t block = word(map+block_index*4u), value = 0;
        if (!block || !read_guest(block+(index%4u)*4u, &value, sizeof value)) break;
        ++event->queue_scan_count;
        if (value == event->node) ++event->queue_node_occurrences;
    }
}

static void log_event(const char *label, const RouteEvent *event)
{
    MM3_ROUTE_LOG("[%s] sequence=%llu qpc=%llu prior_enqueue=%llu thread=%u kind=%u scheduler=%08X "
        "node=%08X record=%08X key=%08X graph=%08X default=%08X optional=%08X "
        "vtable=%08X target=%08X graph_read=%d target_read=%d "
        "caller=%08X producer=%08X parent=%08X queue_count=%u queue_offset=%u "
        "queue_node_occurrences=%u queue_scan_count=%u "
        "iterator_index=%08X iterator_container=%08X worker=%08X "
        "graph_words=%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X\n",
        label, (unsigned long long)event->sequence, (unsigned long long)event->qpc,
        (unsigned long long)event->prior_enqueue,
        event->thread, event->kind, event->scheduler, event->node, event->record,
        event->key, event->graph, event->default_graph, event->optional_graph,
        event->vtable, event->target, event->graph_read, event->target_read,
        event->callers[0], event->callers[1], event->callers[2], event->queue_count,
        event->queue_offset, event->queue_node_occurrences, event->queue_scan_count,
        event->iterator_index, event->iterator_container,
        event->worker, event->words[0], event->words[1], event->words[2],
        event->words[3], event->words[4], event->words[5], event->words[6], event->words[7]);
}

void mm3_route_enqueue(uint32_t scheduler, uint32_t node, uint32_t graph,
                       uint32_t optional_graph, uint32_t frame)
{
    RouteContext saved;
    RouteEvent event = {0}, prior = {0}, recent[ROUTE_RECENT];
    unsigned recent_count = 0;
    int report, duplicate_report = 0;
    if (!enabled()) return;
    saved = begin_observation();
    event.kind = 0; event.scheduler = scheduler; event.node = node;
    event.record = node + 0x10u; event.graph = graph;
    event.optional_graph = optional_graph;
    event.key = word(node + 0xCu);
    capture(&event, frame);
    scan_queue(&event);
    AcquireSRWLockExclusive(&route_lock);
    for (unsigned i = 0; i < ROUTE_RING; ++i)
        if (route_enqueues[i].node == node && route_enqueues[i].sequence > prior.sequence)
            prior = route_enqueues[i];
    event.prior_enqueue = prior.sequence;
    event.sequence = ++route_sequence;
    if (event.queue_node_occurrences > 1 && duplicate_reports < 8) {
        ++duplicate_reports; duplicate_report = 1;
        unsigned count = event_position < ROUTE_RECENT ? event_position : ROUTE_RECENT;
        for (unsigned i = 0; i < count; ++i)
            recent[recent_count++] = route_events[(event_position-count+i) % ROUTE_RING];
    }
    route_enqueues[enqueue_position++ % ROUTE_RING] = event;
    route_events[event_position++ % ROUTE_RING] = event;
    report = baseline_reports < 8;
    if (report) ++baseline_reports;
    ReleaseSRWLockExclusive(&route_lock);
    if (report) log_event("ROUTE_ENQUEUE", &event);
    if (duplicate_report) {
        log_event("ROUTE_DUPLICATE", &event);
        if (prior.sequence) log_event("ROUTE_ENQUEUE_PRIOR", &prior);
        for (unsigned i = 0; i < recent_count; ++i) log_event("ROUTE_RECENT", &recent[i]);
    }
    end_observation(&saved);
}

void mm3_route_lifetime(unsigned kind, uint32_t scheduler, uint32_t node,
                        uint32_t key, uint32_t frame,
                        uint32_t iterator_index, uint32_t iterator_container)
{
    RouteContext saved;
    RouteEvent event = {0};
    if (!enabled()) return;
    saved = begin_observation();
    event.kind = kind; event.scheduler = scheduler; event.node = node;
    event.record = node + 0x10u; event.key = key;
    event.graph = word(node + 0x40u);
    event.iterator_index = iterator_index; event.iterator_container = iterator_container;
    capture(&event, frame);
    AcquireSRWLockExclusive(&route_lock);
    event.sequence = ++route_sequence;
    route_events[event_position++ % ROUTE_RING] = event;
    ReleaseSRWLockExclusive(&route_lock);
    end_observation(&saved);
}

void mm3_route_consume(uint32_t record, uint32_t worker, uint32_t stack)
{
    RouteContext saved;
    RouteEvent event = {0}, match = {0}, recent[ROUTE_RECENT];
    unsigned recent_count = 0;
    int bad, report = 0;
    if (!enabled()) return;
    saved = begin_observation();
    event.kind = 4; event.record = record; event.node = record - 0x10u;
    event.worker = worker; event.graph = word(record + 0x30u);
    event.key = word(event.node + 0xCu);
    event.callers[0] = word(stack + 12u); /* return below three native saved registers */
    capture(&event, 0);
    AcquireSRWLockExclusive(&route_lock);
    for (unsigned i = 0; i < ROUTE_RING; ++i)
        if (route_enqueues[i].node == event.node && route_enqueues[i].sequence > match.sequence)
            match = route_enqueues[i];
    event.scheduler = match.scheduler;
    if (event.scheduler) {
        event.default_graph = word(event.scheduler + 0x24u);
        event.queue_count = word(event.scheduler + 0x20u);
        event.queue_offset = word(event.scheduler + 0x1Cu);
    }
    event.optional_graph = match.optional_graph;
    event.sequence = ++route_sequence;
    bad = !event.target_read || !event.target || !RECOMP_ICALL_IS_CODE(event.target) ||
          (match.sequence && (match.graph != event.graph || match.vtable != event.vtable));
    if (bad && bad_reports < 8) {
        ++bad_reports; report = 1;
        unsigned count = event_position < ROUTE_RING ? event_position : ROUTE_RING;
        if (count > ROUTE_RECENT) count = ROUTE_RECENT;
        for (unsigned i = 0; i < count; ++i)
            recent[recent_count++] = route_events[(event_position-count+i) % ROUTE_RING];
    }
    route_events[event_position++ % ROUTE_RING] = event;
    ReleaseSRWLockExclusive(&route_lock);
    if (report) {
        log_event("ROUTE_BAD", &event);
        if (match.sequence) log_event("ROUTE_ENQUEUE_MATCH", &match);
        for (unsigned i = 0; i < recent_count; ++i) log_event("ROUTE_RECENT", &recent[i]);
    }
    end_observation(&saved);
}
