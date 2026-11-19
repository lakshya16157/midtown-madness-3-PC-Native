#ifndef MM3_TREE_WATCH_H
#define MM3_TREE_WATCH_H

#include <stdint.h>

/* Tree watch (src/mm3_tree_watch.c): why a tree that was on screen vanished.
 * sub_0016AEDB (mm3_tree_diagnostics.c) opens one visit per street prop;
 * the culling leaves report into the open visit. Files that tests #include
 * reach these through weak links, so a test without this file still links. */
typedef struct MM3TreeVisit {
    struct MM3TreeVisit *previous;
    uint32_t object, owner, camera, transform;   /* camera: guest address of 3 floats */
    uint32_t billboards_before;
    uint8_t tracked, owner_off, frustum_reject, size_reject;
    uint8_t billboard_tested, billboard_only, model_tested, model_found;
} MM3TreeVisit;

/* 1 when the watch wants props classified (MM3_TREE_WATCH, default on). */
int mm3_tree_watch_wanted(void);
/* Every prop draw, tree or not, so a nested draw never reports into a tree's visit. */
void mm3_tree_watch_begin(MM3TreeVisit *visit, uint32_t object, uint32_t camera, uint32_t transform, int is_tree);
void mm3_tree_watch_finish(MM3TreeVisit *visit);
/* sub_001C7B15 / sub_001A67BA results, by return address. */
void mm3_tree_watch_cull(uint32_t caller, uint32_t rejected);

#endif
