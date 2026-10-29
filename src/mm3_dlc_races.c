#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp_types.h"
#include "mm3_mod_flags.h"
#include "mm3_dlc_races.h"

/* The downloadable Blitz and Checkpoint races (content packages 2 and 3:
 * races 11-20 of each mode and city) came without names, descriptions or
 * route pictures of their own: their scripts are titled "Untitled",
 * "Parktown" or "Going downtown" from the template, their icons are
 * byte-for-byte copies of races 1-10, and the retail locale only has the
 * placeholders SID_BLITZ_<city>_11.. ("Blitz race 11") and no info text
 * (SID_B/C<city>R<race>QB stop at race 10). The menu therefore showed a
 * repeated icon, a numbered name and an empty description box.
 *
 * Name each one, and describe it from what its own files set: checkpoints,
 * the time limit (Blitz), season, time of day and weather. Names are the
 * same in every language, like the cars'; descriptions follow the language
 * at 0x3C5648 (0 English, 1 French, 2 German, 3 Italian, 4 Spanish). Latin-1
 * text: the lookup widens each byte to UTF-16. MM3_DLC_RACE_INFO=0 keeps the
 * retail placeholders. */
typedef struct {
    unsigned char city, checkpoint, race, gates;
    unsigned short seconds;
    unsigned char season, time, weather;
    const char *name;
} DlcRace;

enum { SPRING, SUMMER, FALL, WINTER };
enum { MORNING, NOON, EVENING, NIGHT };
enum { CLEAR, RAIN, SNOW };

/* race = the menu's number (file 10 is race 11). */
static const DlcRace dlc_races[] = {
    /* Paris Blitz */
    {1, 0, 11, 14, 125, SUMMER, EVENING, RAIN, "Summer Storm Dash"},
    {1, 0, 12, 15, 85, SPRING, NOON, CLEAR, "Lunch Hour Sprint"},
    {1, 0, 13, 14, 185, WINTER, EVENING, SNOW, "Snowfall at Dusk"},
    {1, 0, 14, 13, 120, SUMMER, NOON, CLEAR, "High Noon Hustle"},
    {1, 0, 15, 26, 245, SUMMER, NIGHT, CLEAR, "Paris by Night"},
    {1, 0, 16, 25, 180, FALL, NIGHT, RAIN, "Midnight Downpour"},
    {1, 0, 17, 17, 130, SPRING, EVENING, CLEAR, "Golden Hour Run"},
    {1, 0, 18, 24, 150, SPRING, MORNING, CLEAR, "Morning Rush"},
    {1, 0, 19, 20, 130, WINTER, NOON, CLEAR, "Frosty Errand"},
    {1, 0, 20, 6, 195, WINTER, NIGHT, SNOW, "Blizzard Crawl"},
    /* Paris Checkpoint */
    {1, 1, 11, 26, 0, SPRING, MORNING, CLEAR, "Going Downtown"},
    {1, 1, 12, 16, 0, SPRING, NIGHT, CLEAR, "Moonlit Chase"},
    {1, 1, 13, 13, 0, FALL, EVENING, RAIN, "Autumn Rain Race"},
    {1, 1, 14, 20, 0, SPRING, EVENING, CLEAR, "Boulevard Dusk"},
    {1, 1, 15, 17, 0, WINTER, EVENING, CLEAR, "Winter Twilight"},
    {1, 1, 16, 16, 0, SPRING, NIGHT, RAIN, "Night Rain Race"},
    {1, 1, 17, 29, 0, SPRING, EVENING, CLEAR, "The Long Way Round"},
    {1, 1, 18, 23, 0, FALL, MORNING, CLEAR, "Autumn Morning Race"},
    {1, 1, 19, 29, 0, SUMMER, NOON, CLEAR, "Summer Marathon"},
    {1, 1, 20, 10, 0, WINTER, NIGHT, SNOW, "Snow Night Sprint"},
    /* Washington Blitz */
    {2, 0, 11, 15, 120, SUMMER, MORNING, CLEAR, "Rush Hour"},
    {2, 0, 12, 14, 110, FALL, EVENING, RAIN, "Rainy Commute"},
    {2, 0, 13, 14, 70, WINTER, NOON, SNOW, "Snow Day Sprint"},
    {2, 0, 14, 9, 90, SPRING, MORNING, CLEAR, "Cherry Blossom Run"},
    {2, 0, 15, 14, 135, FALL, EVENING, CLEAR, "Autumn Sunset Run"},
    {2, 0, 16, 19, 150, SUMMER, NIGHT, CLEAR, "Capital Nights"},
    {2, 0, 17, 17, 175, SUMMER, MORNING, RAIN, "Summer Showers"},
    {2, 0, 18, 21, 200, WINTER, NOON, CLEAR, "Cold Front"},
    {2, 0, 19, 9, 280, SUMMER, EVENING, CLEAR, "Long Evening Drive"},
    {2, 0, 20, 54, 760, SUMMER, NOON, CLEAR, "Monument Marathon"},
    /* Washington Checkpoint */
    {2, 1, 11, 15, 0, FALL, EVENING, CLEAR, "Parktown"},
    {2, 1, 12, 10, 0, SPRING, MORNING, CLEAR, "Spring Morning Race"},
    {2, 1, 13, 19, 0, SUMMER, NOON, RAIN, "Summer Rain Race"},
    {2, 1, 14, 26, 0, SUMMER, NIGHT, CLEAR, "Capital After Dark"},
    {2, 1, 15, 10, 0, SUMMER, EVENING, CLEAR, "Evening Sprint"},
    {2, 1, 16, 8, 0, WINTER, MORNING, CLEAR, "Frosty Morning Race"},
    {2, 1, 17, 13, 0, SUMMER, NIGHT, RAIN, "Storm Night Race"},
    {2, 1, 18, 19, 0, FALL, MORNING, CLEAR, "Autumn Leaves Race"},
    {2, 1, 19, 13, 0, WINTER, NOON, SNOW, "Snowbound"},
    {2, 1, 20, 27, 0, SPRING, EVENING, CLEAR, "Long Spring Evening"}
};

/* [language][season], [language][time], [language][weather] */
static const char *const seasons[5][4] = {
    {"Spring", "Summer", "Autumn", "Winter"},
    {"Printemps", "\xC9t\xE9", "Automne", "Hiver"},
    {"Fr\xFChling", "Sommer", "Herbst", "Winter"},
    {"Primavera", "Estate", "Autunno", "Inverno"},
    {"Primavera", "Verano", "Oto\xF1o", "Invierno"}
};
static const char *const times[5][4] = {
    {"morning", "midday", "evening", "night"},
    {"matin", "midi", "soir", "nuit"},
    {"Morgen", "Mittag", "Abend", "Nacht"},
    {"mattina", "mezzogiorno", "sera", "notte"},
    {"ma\xF1\x61na", "mediod\xED\x61", "tarde", "noche"}
};
static const char *const weathers[5][3] = {
    {"clear skies", "rain", "snow"},
    {"temps clair", "pluie", "neige"},
    {"klarer Himmel", "Regen", "Schnee"},
    {"cielo sereno", "pioggia", "neve"},
    {"cielo despejado", "lluvia", "nieve"}
};
/* Blitz: checkpoints, minutes, seconds. Checkpoint: checkpoints. */
static const char *const blitz_text[5] = {
    "Downloaded blitz: reach all %u checkpoints within %u:%02u.",
    "Course \xE9\x63lair t\xE9l\xE9\x63harg\xE9\x65 : passez les %u points de contr\xF4le en %u:%02u.",
    "Download-Blitzrennen: alle %u Checkpoints in %u:%02u erreichen.",
    "Corsa lampo scaricata: raggiungi tutti i %u checkpoint entro %u:%02u.",
    "Carrera rel\xE1mpago descargada: alcanza los %u puntos de control en %u:%02u."
};
static const char *const checkpoint_text[5] = {
    "Downloaded checkpoint race: %u checkpoints against rival drivers.",
    "Course t\xE9l\xE9\x63harg\xE9\x65 : %u points de contr\xF4le contre des pilotes rivaux.",
    "Download-Checkpointrennen: %u Checkpoints gegen rivalisierende Fahrer.",
    "Corsa a checkpoint scaricata: %u checkpoint contro piloti rivali.",
    "Carrera de puntos de control descargada: %u puntos contra pilotos rivales."
};

static const DlcRace *dlc_race(unsigned city, int checkpoint, unsigned race)
{
    unsigned i;
    for (i = 0; i < sizeof dlc_races / sizeof dlc_races[0]; ++i)
        if (dlc_races[i].city == city && dlc_races[i].checkpoint == checkpoint && dlc_races[i].race == race)
            return &dlc_races[i];
    return NULL;
}

static int number(const char *text, const char *end_text, unsigned *value, const char **rest)
{
    char *end;
    unsigned long n = strtoul(text, &end, 10);
    if (end == text || (end_text && strncmp(end, end_text, strlen(end_text)))) return 0;
    *value = (unsigned)n;
    if (rest) *rest = end;
    return 1;
}

/* Route pictures. The race list (0x103EEA) names a race's picture
 * "Races/<City>/<Mode>/" + "%02d" of its file number + ".tga" in a string
 * builder whose text is at [ebp-0x2C], then loads it, falling back to
 * Races/NoIcon.tga. For the downloadable races that picture is the DLC's
 * copy of race 1-10's. When the installer put that race's own route into
 * Data_dvd.zip (tools/races/dlc_icons.py; listed in Data/mm3_dlc_icons.ini),
 * finish the name with "_mm3.tga" instead, so it loads that one; the DLC
 * itself is never touched. */
extern void sub_002430FD_original(void);
#define ICON_NAME_RETURN 0x00103F59u
#define ICON_EXTENSION 0x0039CA6Cu   /* ".tga" */
#define DLC_LIST_MAX 256
typedef struct {
    const char *path;
    char keys[DLC_LIST_MAX][40];
    unsigned count;
    int loaded;
} DlcList;
static DlcList s_icons = {"game_files/Data/mm3_dlc_icons.ini"};

static int listed(DlcList *list, const char *key)
{
    char line[96];
    FILE *file;
    unsigned i;
    if (!list->loaded) {
        list->loaded = 1;
        if ((file = fopen(list->path, "rb")) != NULL) {
            while (fgets(line, sizeof line, file) && list->count < DLC_LIST_MAX) {
                size_t length = strcspn(line, "\r\n");
                line[length] = 0;
                if (line[0] && line[0] != '#' && length < sizeof list->keys[0])
                    memcpy(list->keys[list->count++], line, length + 1);
            }
            fclose(file);
        }
    }
    for (i = 0; i < list->count; ++i)
        if (!strcmp(list->keys[i], key)) return 1;
    return 0;
}

/* Finish the name with "_mm3.tga" instead of the ".tga" it was given
 * (sub_002430FD: [esp+4] the string, [esp+8] the text to append; RET 8). */
static void append_mm3_tga(uint32_t entry)
{
    static const char suffix[] = "_mm3.tga";
    uint32_t text = (entry - 64u) & ~3u, i;
    for (i = 0; i < sizeof suffix; ++i) MEM8(text + i) = (uint8_t)suffix[i];
    g_esp = text;
    PUSH32(g_esp, text);
    PUSH32(g_esp, MEM32(entry + 4u));
    PUSH32(g_esp, MEM32(entry));
    sub_002430FD_original();
    g_esp = entry + 12u;
}

/* Loading screens. Before a race loads, sub_001E8184 writes its screen's
 * name, "+LoadingScreens/<Language>/<City>/<Mode>/Loading" + its file
 * number, to an output stream at [esp+4] and finishes it with ".tga"
 * (0x3AFFB4), returning to 0x1E82AC. The stream's first member points to
 * its buffer (vtable 0x384FD8), whose write (sub_00018CA9) appends to a
 * std::string at +4. Retail has no screen for the downloadable races (files
 * 10-19), so the game showed the car's picture. When the installer made the
 * race one from the retail screens and the city map
 * (tools/races/dlc_loading.py; listed in Data/mm3_dlc_loading.ini), finish
 * the name with "_mm3.tga". */
#define LOADING_NAME_RETURN 0x001E82ACu
#define LOADING_EXTENSION 0x003AFFB4u   /* ".tga" */
#define LOADING_STREAM_BUFFER 0x00384FD8u
static DlcList s_loading = {"game_files/Data/mm3_dlc_loading.ini"};

/* An MSVC std::string's text: in place below 16 characters, else at [+4]. */
static int std_string(uint32_t object, char *out, size_t size)
{
    uint32_t length, text, i;
    if (object < 0x10000u || object >= 0x08000000u - 0x1Cu) return 0;
    length = MEM32(object + 0x14u);
    text = MEM32(object + 0x18u) < 16u ? object + 4u : MEM32(object + 4u);
    if (length >= size || text < 0x10000u || text >= 0x08000000u - size) return 0;
    for (i = 0; i < length; ++i) out[i] = (char)MEM8(text + i);
    out[length] = 0;
    return 1;
}

static int stream_text(uint32_t stream, char *out, size_t size)
{
    uint32_t buffer;
    if (stream < 0x10000u || stream >= 0x08000000u - 4u) return 0;
    buffer = MEM32(stream);
    if (buffer < 0x10000u || buffer >= 0x08000000u - 0x20u || MEM32(buffer) != LOADING_STREAM_BUFFER) return 0;
    return std_string(buffer + 4u, out, size);
}

static int loading_listed(const char *name)
{
    static const char prefix[] = "+LoadingScreens/";
    char key[48];
    const char *number = strrchr(name, '/');
    size_t head;
    /* "<Language>/<City>/<Mode>/Loading<NN>" -> "<Language>/<City>/<Mode>/<NN>" */
    if (strncmp(name, prefix, sizeof prefix - 1) || !number || strncmp(number, "/Loading", 8) ||
        strlen(number + 8) != 2)
        return 0;
    head = (size_t)(number + 1 - (name + sizeof prefix - 1));
    if (head + 3 > sizeof key) return 0;
    memcpy(key, name + sizeof prefix - 1, head);
    memcpy(key + head, number + 8, 3);
    return listed(&s_loading, key);
}

void sub_002430FD(void)
{
    uint32_t entry = g_esp, i;
    char name[96];
    if (MEM32(entry) == ICON_NAME_RETURN && MEM32(entry + 8u) == ICON_EXTENSION && mm3_mod_dlc_race_info()) {
        uint32_t buffer = MEM32(g_ebp - 0x2Cu);
        if (buffer >= 0x10000u && buffer < 0x08000000u - 48u) {
            for (i = 0; i < 47u && (name[i] = (char)MEM8(buffer + i)) != 0; ++i) {}
            name[i] = 0;
            /* "Races/<City>/<Mode>/<NN>": NN 10-19 are the downloadable races. */
            if (i < 47u && !strncmp(name, "Races/", 6) && i > 9 && name[i - 2] == '1' &&
                name[i - 3] == '/' && listed(&s_icons, name + 6)) {
                append_mm3_tga(entry);
                return;
            }
        }
    }
    if (MEM32(entry) == LOADING_NAME_RETURN && MEM32(entry + 8u) == LOADING_EXTENSION &&
        mm3_mod_dlc_race_info() && stream_text(MEM32(entry + 4u), name, sizeof name) && loading_listed(name)) {
        append_mm3_tga(entry);
        return;
    }
    sub_002430FD_original();
}

const char *mm3_dlc_race_label(const char *key, unsigned language)
{
    static char text[200];
    const DlcRace *race;
    const char *rest;
    unsigned city, number_value, lang = language <= 4 ? language : 0;
    int checkpoint;
    if (!key || !mm3_mod_dlc_race_info()) return NULL;
    /* Names: SID_BLITZ_<city>_<race>, SID_CHKPT_<city>_<race>. */
    if (!strncmp(key, "SID_BLITZ_", 10) || !strncmp(key, "SID_CHKPT_", 10)) {
        checkpoint = key[4] == 'C';
        if (!number(key + 10, "_", &city, &rest) || !number(rest + 1, NULL, &number_value, &rest) || *rest)
            return NULL;
        race = dlc_race(city, checkpoint, number_value);
        return race ? race->name : NULL;
    }
    /* Descriptions: SID_B<city>R<race>QB, SID_C<city>R<race>QB. */
    if ((key[4] == 'B' || key[4] == 'C') && !strncmp(key, "SID_", 4) && key[5] >= '1' && key[5] <= '2' &&
        key[6] == 'R') {
        checkpoint = key[4] == 'C';
        city = (unsigned)(key[5] - '0');
        if (!number(key + 7, "QB", &number_value, &rest) || strcmp(rest, "QB")) return NULL;
        race = dlc_race(city, checkpoint, number_value);
        if (!race) return NULL;
        if (checkpoint)
            snprintf(text, sizeof text, checkpoint_text[lang], (unsigned)race->gates);
        else
            snprintf(text, sizeof text, blitz_text[lang], (unsigned)race->gates,
                     race->seconds / 60u, race->seconds % 60u);
        snprintf(text + strlen(text), sizeof text - strlen(text), " %s, %s, %s.",
                 seasons[lang][race->season], times[lang][race->time], weathers[lang][race->weather]);
        return text;
    }
    return NULL;
}
