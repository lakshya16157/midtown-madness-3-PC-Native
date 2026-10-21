#ifndef MM3_COLLISION_PROFILE_H
#define MM3_COLLISION_PROFILE_H

#include <stdint.h>

typedef struct MM3CollisionSample {
    int64_t started;
    uint32_t car, contacts_before;
} MM3CollisionSample;

typedef struct MM3CollisionProfileSnapshot {
    int enabled;
    uint64_t calls, contact_events, solid_points, effects;
} MM3CollisionProfileSnapshot;

extern int mm3_collision_profile_on;
void mm3_collision_profile_init(void);
MM3CollisionSample mm3_collision_profile_begin(uint32_t car, uint32_t contacts);
void mm3_collision_profile_end(MM3CollisionSample sample, uint32_t contacts);
void mm3_collision_profile_effect(void);
void mm3_collision_profile_snapshot(MM3CollisionProfileSnapshot *snapshot);

#endif
