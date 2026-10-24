/* Force-included into the unresolved-stub translation unit
 * (src/recomp/gen/recomp_stubs_unresolved.c) unless MM3_FRONTEND_WATCH brings
 * its own observer: each stub reports the first time it runs
 * (src/mm3_unresolved_observe.c). */
#include <stdint.h>
void mm3_unresolved_stub_reached(uint32_t va);
#define RECOMP_UNRESOLVED_STUB_OBSERVE(va) mm3_unresolved_stub_reached(va)
