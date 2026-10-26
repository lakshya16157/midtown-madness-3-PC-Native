#ifndef MM3_RENDER_DISTANCE_H
#define MM3_RENDER_DISTANCE_H

/* Object, pedestrian and shadow distances from RECOMP_DRAW_DISTANCE. */
float mm3_draw_distance_scale(void);
/* City visibility/camera range is independent of object and pedestrian LODs.
 * An unset view setting inherits the legacy draw setting. Settings are cached
 * for a run; native resident-cell limits still bound the effective city/far
 * range, and facade detail stays within the allocated 2x distance budget. */
float mm3_view_distance_scale(void);
float mm3_facade_distance_scale(void);
/* Native city loader seam; records its actual resident-cell range limit. */
void mm3_city_streaming_limit(unsigned int renderer);
/* How much farther the city is drawn than its authored range (at least 1):
 * the depth scale that puts a sky fog ramp back at its authored distance. */
float mm3_city_haze_scale(void);
#define MM3_FOG_RAMP_KNEE 0.78f

#endif
