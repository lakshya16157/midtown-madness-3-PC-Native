#ifndef MM3_TREE_AUDIO_H
#define MM3_TREE_AUDIO_H
#include <stdint.h>

void mm3_tree_audio_begin(void);
void mm3_tree_audio_select(uint32_t metadata, uint32_t city, uint32_t id, uint32_t car);
void mm3_tree_audio_finish(int converted, int contacting, const float position[3], float speed);

#endif
