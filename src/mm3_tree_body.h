#ifndef MM3_TREE_BODY_H
#define MM3_TREE_BODY_H
#include <stdint.h>
int mm3_tree_body_create(uint32_t city, uint32_t id, uint32_t metadata,
    const float velocity[3], float position[3]);
int mm3_tree_body_geometry_allocation(void);
void mm3_tree_body_confirm(int retired);
#endif
