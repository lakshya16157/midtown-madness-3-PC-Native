#ifndef MM3_LIGHT_QUEUE_H
#define MM3_LIGHT_QUEUE_H

#include <stdint.h>

/* The native local-light manager (pointer at 0x003C5D8C) holds 256 inline
 * records of 0x54 bytes at +0x10, a 256-entry record pointer array at
 * +0x5410 (kept sorted point-before-cone) and the queued count at +0x5810.
 * See mm3_light_queue.c for the PC-sized queue. */
#define MM3_LIGHT_QUEUE_RETAIL 256u
#define MM3_LIGHT_QUEUE_MAXIMUM 1024u

/* Record pointer array and capacity of `queue`: the extended array for the
 * manager this module enlarged, else the retail +0x5410 array and 256. */
uint32_t mm3_light_queue_slots(uint32_t queue);
uint32_t mm3_light_queue_capacity(uint32_t queue);

/* Manager construction (static initializer 0x254FDD): allocation size, the
 * allocation result, then the constructed manager in 0x003C5D8C. */
uint32_t mm3_light_queue_alloc_size(void);
void mm3_light_queue_allocated(uint32_t address);
void mm3_light_queue_attach(void);

/* Frame budget: the consumer (0x230970) passes each frame's queue before
 * drawing it, and only the lights that show are drawn (mm3_light_queue.c).
 * Car lighting tags its records (1 car, 2 the driver's car; untagged records
 * are world lights) and counts records it could not queue (pool/point 0,
 * beam 1) for the usage report. */
void mm3_light_queue_frame(uint32_t queue);
void mm3_light_queue_tag(uint32_t record, unsigned light_class);
/* Producers call this while the scene camera is current (street lamps, car
 * lights); the frame budget ranks lights from the first trusted eye. */
void mm3_light_queue_camera(void);
/* The most recent trusted camera position, for diagnostics. */
int mm3_light_queue_last_eye(float eye[3]);
void mm3_light_queue_refused(unsigned beam);
/* Diagnostic: car lighting marks a point on the road ahead of the driver's
 * car while its headlights are switched off in the evening or at night; the
 * usage report then says which drawn lights cover that point. */
void mm3_light_queue_probe(const float point[3]);

#endif
