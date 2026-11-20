#ifndef MM3_WHATS_NEW_H
#define MM3_WHATS_NEW_H

#include <stddef.h>

/* The notes of every version newer than `seen` up to `current` from `notes` (src/whats_new.txt:
 * "## <version>" headings, newest first), each under a "Version <v>" line. An empty or NULL
 * `seen` (a player updating from a version before this window) gives `current`'s notes only.
 * Returns the length written to `out` (0: nothing new). */
size_t mm3_whats_new_text(const char *notes, const char *seen, const char *current, char *out, size_t size);

/* Once after an update: shows those notes in a window, then remembers the version
 * (whats-new-seen.txt beside launcher.ini). Silent on a fresh install, in the Vulkan
 * hand-off child, with MM3_LAUNCHER=0 (scripted runs) or MM3_WHATS_NEW=0. */
void mm3_whats_new(void);

#endif
