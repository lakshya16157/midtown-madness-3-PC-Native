#ifndef MM3_ROUTE_DIAGNOSTICS_H
#define MM3_ROUTE_DIAGNOSTICS_H
#include <stdint.h>
/* Read-only observers. Enable before launch with MM3_ROUTE_DIAGNOSTICS=1. */
void mm3_route_enqueue(uint32_t scheduler, uint32_t node, uint32_t graph,
                       uint32_t optional_graph, uint32_t frame);
void mm3_route_consume(uint32_t record, uint32_t worker, uint32_t stack);
/* 1: retrieve before erase; 2: cancel find result; 3: cancel before erase. */
void mm3_route_lifetime(unsigned event, uint32_t scheduler, uint32_t node,
                        uint32_t key, uint32_t frame,
                        uint32_t iterator_index, uint32_t iterator_container);
#endif
