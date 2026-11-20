/* The choices the launchers, the in-game Display Settings (F10) and the
 * in-game Graphics menu all list, and the labels the launcher and F10 show
 * for them, so every menu offers the same values under the same names (40: a
 * Steam Deck screen set to 40 Hz). A custom value from the settings file
 * still shows in either, added to the list. */
#ifndef MM3_DISPLAY_CHOICES_H
#define MM3_DISPLAY_CHOICES_H

#include <stdio.h>
#include <stdlib.h>

static const int mm3_fps_choices[] = {15, 30, 40, 45, 60, 90, 120};
/* Windowed sizes; the monitor's own size is added when missing. */
static const int mm3_window_sizes[][2] = {
    {800, 600}, {960, 540}, {1024, 768}, {1280, 720}, {1280, 800}, {1366, 768}, {1600, 900},
    {1920, 1080}, {1920, 1200}, {2560, 1440}, {2560, 1600}, {3840, 2160}};
/* Internal resolution, as a multiple of the Xbox's 480 lines, smallest first; 0 is Automatic and
 * stays at the top. The menus name each by its vertical lines (480p, 720p, 1080p, 1440p, 2160p ...). */
static const float mm3_scale_choices[] = {0.f, .5f, .75f, 1.f, 1.5f, 2.f, 2.25f, 3.f, 4.5f};
static const float mm3_distance_choices[] = {1.f, 1.5f, 2.f, 3.f, 4.f};
static const char *const mm3_aspect_choices[] = {"auto", "4:3", "16:9", "16:10", "21:9"};
static const int mm3_msaa_choices[] = {1, 2, 4, 8};
static const int mm3_aniso_choices[] = {1, 2, 4, 8, 16};
/* Indexed by the setting's value (fullscreen 0/1, minimap_improved 0/1). */
static const char *const mm3_mode_names[] = {"Windowed", "Fullscreen"};
static const char *const mm3_minimap_names[] = {"Retail", "Improved perspective"};

#define MM3_CHOICE_COUNT(list) ((int)(sizeof(list) / sizeof((list)[0])))

static inline void mm3_fps_label(int fps, char *label, size_t size)
{
    snprintf(label, size, "%d", fps);
}

static inline void mm3_window_label(int width, int height, char *label, size_t size)
{
    snprintf(label, size, "%d x %d", width, height);
}

static inline int mm3_scale_lines(float scale)
{
    return (int)(480.f * scale + .5f);
}

/* Whether the list offers this scale; one from the settings file that it does not is "(custom)". */
static inline int mm3_scale_is_listed(float scale)
{
    int i;
    for (i = 0; i < MM3_CHOICE_COUNT(mm3_scale_choices); ++i)
        if ((int)(mm3_scale_choices[i] * 1000.f + .5f) == (int)(scale * 1000.f + .5f)) return 1;
    return 0;
}

/* The shape the renderer accepts (it clamps the same way). */
static inline float mm3_aspect_clamp(float aspect)
{
    return aspect < .5f ? .5f : aspect > 4.f ? 4.f : aspect;
}

/* An aspect setting as the game reads it: "16:9" or a plain number. 0 for "auto" or anything else, then
 * the window's (or fullscreen screen's) own shape is the one in use. */
static inline float mm3_aspect_value(const char *text)
{
    char *end;
    double width, height;
    if (!text) return 0.f;
    width = strtod(text, &end);
    if (end == text || width <= 0) return 0.f;
    if (*end == ':') {
        height = strtod(end + 1, &end);
        if (height <= 0) return 0.f;
        width /= height;
    }
    return mm3_aspect_clamp((float)width);
}

/* "1440p (QHD) - 2560 x 1440": the vertical lines, the common name where there is one, and the size they
 * make at this aspect ratio (width / height; 0 leaves the size out). Automatic has no size of its own: it
 * follows the window. */
static inline void mm3_scale_label_aspect(float scale, float aspect, int listed, char *label, size_t size)
{
    int lines;
    const char *name;
    if (!scale) {
        snprintf(label, size, "Automatic + supersampling");
        return;
    }
    lines = mm3_scale_lines(scale);
    name = !listed ? " (custom)" : lines == 480 ? " (Xbox)" : lines == 720 ? " (HD)" : lines == 1080 ? " (Full HD)" :
           lines == 1440 ? " (QHD)" : lines == 2160 ? " (4K)" : "";
    if (aspect > 0.f) snprintf(label, size, "%dp%s - %d x %d", lines, name, (int)((float)lines * aspect + .5f), lines);
    else snprintf(label, size, "%dp%s", lines, name);
}

/* Without a shape: the lines and the name only. */
static inline void mm3_scale_label(float scale, char *label, size_t size)
{
    mm3_scale_label_aspect(scale, 0.f, 1, label, size);
}

static inline void mm3_custom_scale_label(float scale, char *label, size_t size)
{
    mm3_scale_label_aspect(scale, 0.f, 0, label, size);
}

static inline void mm3_distance_label(float distance, char *label, size_t size)
{
    if (distance == 1.f) snprintf(label, size, "1x (Xbox)");
    else snprintf(label, size, "%.3gx", (double)distance);
}

static inline void mm3_msaa_label(int samples, char *label, size_t size)
{
    if (samples <= 1) snprintf(label, size, "Off");
    else snprintf(label, size, "%dx MSAA", samples);
}

/* A scale or distance from the settings file that no list offers. */
static inline void mm3_custom_label(float value, char *label, size_t size)
{
    snprintf(label, size, "Custom %.3gx", (double)value);
}

static inline void mm3_aniso_label(int level, char *label, size_t size)
{
    snprintf(label, size, "%dx", level);
}

#endif
