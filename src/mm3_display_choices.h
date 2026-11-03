/* The choices both the launcher and the in-game Display Settings (F10) list,
 * so the two offer the same values. A custom value from the settings file
 * still shows in either, added to the list. */
#ifndef MM3_DISPLAY_CHOICES_H
#define MM3_DISPLAY_CHOICES_H

static const int mm3_fps_choices[] = {15, 30, 45, 60, 90, 120};
/* Windowed sizes; the monitor's own size is added when missing. */
static const int mm3_window_sizes[][2] = {
    {800, 600}, {960, 540}, {1024, 768}, {1280, 720}, {1280, 800}, {1366, 768}, {1600, 900},
    {1920, 1080}, {1920, 1200}, {2560, 1440}, {2560, 1600}, {3840, 2160}};
/* Internal resolution, as a multiple of the Xbox's 480 lines; 0 is Automatic. */
static const float mm3_scale_choices[] = {0.f, .5f, .75f, 1.f, 1.5f, 2.f, 2.5f, 3.f, 4.f, 8.f};
static const float mm3_distance_choices[] = {1.f, 1.5f, 2.f, 3.f, 4.f};
static const char *const mm3_aspect_choices[] = {"auto", "4:3", "16:9", "16:10", "21:9"};

#endif
