#ifndef MM3_FRAME_LIMIT_H
#define MM3_FRAME_LIMIT_H

#include <ctype.h>
#include <stdlib.h>

/* The title's simulation and AI are validated at 60 Hz, the default. The cars'
 * physics runs 1/120 s steps, so 120 fps is the most the game follows: above
 * it some frames would move nothing. */
#define MM3_FPS_LIMIT_DEFAULT 60
#define MM3_FPS_LIMIT_MAX 120

static inline int mm3_frame_limit(const char *value)
{
    char *end;
    long requested;
    if (!value || !*value)
        return MM3_FPS_LIMIT_DEFAULT;
    requested = strtol(value, &end, 10);
    if (end == value)
        return MM3_FPS_LIMIT_DEFAULT;
    while (isspace((unsigned char)*end))
        ++end;
    if (*end || requested < 1 || requested > MM3_FPS_LIMIT_MAX)
        return MM3_FPS_LIMIT_DEFAULT;
    return (int)requested;
}

#endif
