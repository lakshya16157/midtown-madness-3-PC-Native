#ifndef MM3_VIEW_EYE_H
#define MM3_VIEW_EYE_H

#include <math.h>
#include <stdint.h>

/* The live view (0x3C5588) keeps its world-to-camera matrix at +0x1F24 in
 * rows of 16 bytes (row-vector convention: three rotation rows, then the
 * translation row at +0x1F54). Native F3F93 derives the camera's world
 * position from it every frame, +0x1EA8 = -(row i . translation), and builds
 * the five culling planes at +0x1E54 with distance -(normal . position), so
 * the four side planes pass through the camera. The translation row itself is
 * not a position: in play it lay 300m-2km from the driver's car, and glows
 * lifted toward it flew off the cars. Accept +0x1EA8 when the rotation rows
 * are orthonormal and it lies on the side planes. Include after
 * recomp_types.h. */
static inline int mm3_view_eye(float eye[3])
{
    uint32_t view = MEM32(0x003C5588u), i, j, k, plane;
    float tolerance;
    if (view < 0x10000u || view > 0x08000000u - 0x1F60u || MEM32(view + 0x1EA4) != 5u) return 0;
    for (i = 0; i != 3; ++i) {
        eye[i] = MEMF(view + 0x1EA8 + i * 4);
        if (!isfinite(eye[i])) return 0;
    }
    for (i = 0; i != 3; ++i)
        for (j = 0; j != 3; ++j) {
            float dot = 0.0f;
            for (k = 0; k != 3; ++k)
                dot += MEMF(view + 0x1F24 + i * 16 + k * 4) * MEMF(view + 0x1F24 + j * 16 + k * 4);
            if (!isfinite(dot) || fabsf(dot - (i == j ? 1.0f : 0.0f)) > 0.01f) return 0;
        }
    tolerance = 0.1f + 1e-4f * (fabsf(eye[0]) + fabsf(eye[1]) + fabsf(eye[2]));
    for (plane = view + 0x1E54, i = 0; i != 4; ++i, plane += 16) {
        float distance = MEMF(plane) * eye[0] + MEMF(plane + 4) * eye[1] +
                         MEMF(plane + 8) * eye[2] + MEMF(plane + 12);
        if (!isfinite(distance) || fabsf(distance) > tolerance) return 0;
    }
    return 1;
}

#endif
