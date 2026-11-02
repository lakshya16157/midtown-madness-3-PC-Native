#ifndef MM3_RENDER_COMMANDS_H
#define MM3_RENDER_COMMANDS_H
#include "render_fog.h"
/* MM3 extended-distance fog, ordered in its native NO_OPERATION stream. The
 * packet is the runtime's NV2A_HOST_FOG_* (nv2a_host_commands.h), written by
 * the fog hook in draw_distance.c. */
#define MM3_FOG_HEADER NV2A_HOST_FOG_HEADER
#define MM3_FOG_TAG NV2A_HOST_FOG_TAG
#define MM3_FOG_WORDS NV2A_HOST_FOG_WORDS
void mm3_render_commands_init(void);
#endif
