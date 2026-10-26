#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp_types.h"
#include "mm3_circuit_races.h"

/* Circuit races (tools/races) are Checkpoint races 20-29 of each city, after
 * the retail 00-09 and the DLC 10-19. The race list names a race
 * SID_CHKPT_<city>_<race + 1> and its info box SID_C<city>R<race + 1>QB (city 1
 * Paris, 2 Washington); the retail locale stops at race 20 and info 10.
 * tools/car-mods/prepare.py writes the installed circuits to
 * game_files/Data/mm3_circuits.ini, one per line:
 * "<city 1|2> <race 20-29> <name>[<TAB><info>]". Lap messages are
 * SID_MM3_LAP_<lap>_<laps> and SID_MM3_LAP_FINAL, shown by
 * Scripts/logic_circuit.lua.
 *
 * The single player Circuit button (tools/native-menu/generate.py) calls
 * circuitBegin and the other modes circuitEnd; its race list starts at race 20.
 * The race setup update below then lists the circuits, all unlocked, in
 * Circuit mode and leaves them out of Checkpoint. */
#define CIRCUIT_NAME_LENGTH 48
#define CIRCUIT_INFO_LENGTH 304
static char circuit_names[2][CIRCUIT_RACES][CIRCUIT_NAME_LENGTH];
static char circuit_info[2][CIRCUIT_RACES][CIRCUIT_INFO_LENGTH];
static int circuit_names_loaded;
static int circuit_mode;
static uint32_t circuit_box;    /* the selected circuit's box in the picture strip */

static int printable(const char *text, size_t length)
{
    for (size_t i = 0; i < length; ++i)
        if ((unsigned char)text[i] < 0x20 || (unsigned char)text[i] > 0x7E) return 0;
    return 1;
}

int mm3_circuit_parse_line(const char *line, unsigned *city, unsigned *race,
                           char *name, size_t size, char *info, size_t info_size)
{
    char *end;
    const char *text = NULL;
    unsigned long c, r;
    size_t length, info_length = 0;
    c = strtoul(line, &end, 10);
    if (end == line || *end != ' ' || c < 1 || c > 2) return 0;
    line = end + 1;
    r = strtoul(line, &end, 10);
    if (end == line || *end != ' ' || r < CIRCUIT_FIRST_RACE || r >= CIRCUIT_FIRST_RACE + CIRCUIT_RACES) return 0;
    line = end + 1;
    length = strcspn(line, "\t\r\n");
    if (!length || length >= size || !printable(line, length)) return 0;
    if (line[length] == '\t') {
        text = line + length + 1;
        info_length = strcspn(text, "\r\n");
        if (!info_length || info_length >= info_size || !printable(text, info_length)) return 0;
    }
    memcpy(name, line, length);
    name[length] = 0;
    if (info_size) {
        if (text) memcpy(info, text, info_length);
        info[info_length] = 0;
    }
    *city = (unsigned)c;
    *race = (unsigned)r;
    return 1;
}

static void circuit_load_names(void)
{
    char line[400], name[CIRCUIT_NAME_LENGTH], info[CIRCUIT_INFO_LENGTH];
    unsigned city, race;
    FILE *file;
    circuit_names_loaded = 1;
    file = fopen("game_files/Data/mm3_circuits.ini", "rb");
    if (!file) return;
    while (fgets(line, sizeof line, file))
        if (line[0] != '#' && mm3_circuit_parse_line(line, &city, &race, name, sizeof name, info, sizeof info)) {
            memcpy(circuit_names[city - 1][race - CIRCUIT_FIRST_RACE], name, sizeof name);
            memcpy(circuit_info[city - 1][race - CIRCUIT_FIRST_RACE], info, sizeof info);
        }
    fclose(file);
}

/* The game's language (0x3C5648: 0 English, 1 French, 2 German, 3 Italian,
 * 4 Spanish); others read English. Latin-1: the lookup widens each byte. */
static unsigned circuit_language(void)
{
    unsigned language = MEM32(0x003C5648u);
    return language <= 4 ? language : 0;
}

static const char *const circuit_button[5] = {"Circuit", "Circuit", "Rundkurs", "Circuito", "Circuito"};
static const char *const circuit_about[5] = {
    "Race laps around closed-off city circuits. The opponents get tougher with every circuit.",
    "Faites des tours de circuits ferm\xE9s en ville. Les adversaires sont plus coriaces \xE0 chaque circuit.",
    "Fahre Runden auf abgesperrten Stadtkursen. Mit jedem Rundkurs werden die Gegner st\xE4rker.",
    "Corri giri su circuiti cittadini chiusi. Gli avversari diventano pi\xF9 forti a ogni circuito.",
    "Da vueltas en circuitos urbanos cerrados. Los rivales son m\xE1s duros en cada circuito."
};
static const char *const lap_text[5] = {"LAP %lu OF %lu", "TOUR %lu SUR %lu", "RUNDE %lu VON %lu",
                                        "GIRO %lu DI %lu", "VUELTA %lu DE %lu"};
static const char *const final_lap[5] = {"FINAL LAP", "DERNIER TOUR", "LETZTE RUNDE", "ULTIMO GIRO",
                                         "\xDALTIMA VUELTA"};
static const char *const missed_text[5] = {
    "CHECKPOINT %lu MISSED - GO BACK", "POINT DE CONTR\xD4LE %lu MANQU\xC9 - FAITES DEMI-TOUR",
    "CHECKPOINT %lu VERPASST - ZUR\xDC\x43K", "CHECKPOINT %lu MANCATO - TORNA INDIETRO",
    "PUNTO DE CONTROL %lu PERDIDO - VUELVE ATR\xC1S"};

const char *mm3_circuit_label(const char *key)
{
    static char text[64];
    char *end;
    unsigned long city, race, lap, laps;
    if (!key) return NULL;
    if (!strncmp(key, "SID_CHKPT_", 10)) {
        city = strtoul(key + 10, &end, 10);
        if (end == key + 10 || *end != '_' || city < 1 || city > 2) return NULL;
        race = strtoul(end + 1, &end, 10);
        if (*end || race < CIRCUIT_FIRST_RACE + 1 || race > CIRCUIT_FIRST_RACE + CIRCUIT_RACES) return NULL;
        if (!circuit_names_loaded) circuit_load_names();
        key = circuit_names[city - 1][race - CIRCUIT_FIRST_RACE - 1];
        return *key ? key : NULL;
    }
    if (!strncmp(key, "SID_C", 5) && (key[5] == '1' || key[5] == '2') && key[6] == 'R') {
        city = (unsigned long)(key[5] - '0');
        race = strtoul(key + 7, &end, 10);
        if (end == key + 7 || strcmp(end, "QB") ||
            race < CIRCUIT_FIRST_RACE + 1 || race > CIRCUIT_FIRST_RACE + CIRCUIT_RACES) return NULL;
        if (!circuit_names_loaded) circuit_load_names();
        key = circuit_info[city - 1][race - CIRCUIT_FIRST_RACE - 1];
        return *key ? key : NULL;
    }
    if (!strcmp(key, "SID_MM3_CIRCUIT")) return circuit_button[circuit_language()];
    if (!strcmp(key, "SID_MM3_OFT_SRT_CIRCUIT")) return circuit_about[circuit_language()];
    if (!strcmp(key, "SID_MM3_LAP_FINAL")) return final_lap[circuit_language()];
    if (!strncmp(key, "SID_MM3_LAP_", 12)) {
        lap = strtoul(key + 12, &end, 10);
        if (end == key + 12 || *end != '_') return NULL;
        laps = strtoul(end + 1, &end, 10);
        if (*end || lap < 1 || laps < lap || laps > 9) return NULL;
        snprintf(text, sizeof text, lap_text[circuit_language()], lap, laps);
        return text;
    }
    /* A gate passed out of turn (logic_circuit.lua circuitMissed): the
     * checkpoint the course still wants, counted from 1. */
    if (!strncmp(key, "SID_MM3_MISSED_", 15)) {
        lap = strtoul(key + 15, &end, 10);
        if (end == key + 15 || *end || lap < 1 || lap > 99) return NULL;
        snprintf(text, sizeof text, missed_text[circuit_language()], lap);
        return text;
    }
    return NULL;
}

void mm3_circuit_set_mode(int on)
{
    circuit_mode = on != 0;
}

/* Circuits are numbered from race 20 on; how many a city has. */
static uint32_t circuit_count(uint32_t city)
{
    uint32_t count = 0, i;
    if (!circuit_names_loaded) circuit_load_names();
    for (i = 0; i < CIRCUIT_RACES; ++i)
        if (circuit_names[city][i][0]) count = i + 1;
    return count;
}

/* sub_001238B7 runs every frame of the frontend's menu state with the
 * frontend in ESI. While the city is the one it last showed (0x3B8578) it
 * keeps the selected race, varCheckpointRace (+0x14C), as the city's
 * remembered race (+0x110 + 4 * city); on a new city it picks the first race
 * not yet won. It sets varCheckpointRaces (+0x150) to the races shown,
 * min(installed, unlocked) - every installed race once retail race 09 is won,
 * at most ten before - and the selection to the remembered race below that
 * count. The city is the game state's (sub_001E4A45's singleton at
 * 0x3C5CB4) +0x150. */
#define GAME_STATE_SINGLETON 0x003C5CB4u
#define GAME_STATE_CITY 0x150u
#define FRONTEND_SHOWN_CITY 0x003B8578u
#define FRONTEND_CHECKPOINT_RACE 0x14Cu
#define FRONTEND_CHECKPOINT_RACES 0x150u
#define FRONTEND_CITY_CHECKPOINT_RACE 0x110u
#define RETAIL_UNLOCK_LIMIT 10u

extern void sub_001238B7_original(void);

static int guest_object(uint32_t address, uint32_t size)
{
    return address >= 0x10000u && address < 0x08000000u - size;
}

void sub_001238B7(void)
{
    uint32_t frontend = g_esi, state, city, circuits, races, race, slot, selected = 0;
    int same_city = 0;
    state = MEM32(GAME_STATE_SINGLETON);
    if (guest_object(frontend, 0x200u) && guest_object(state, 0x200u)) {
        selected = MEM32(frontend + FRONTEND_CHECKPOINT_RACE);
        same_city = MEM32(state + GAME_STATE_CITY) == MEM32(FRONTEND_SHOWN_CITY);
    }
    sub_001238B7_original();
    if (!guest_object(frontend, 0x200u)) return;
    state = MEM32(GAME_STATE_SINGLETON);
    if (!guest_object(state, 0x200u)) return;
    city = MEM32(state + GAME_STATE_CITY);
    if (city > 1 || !(circuits = circuit_count(city))) return;
    races = MEM32(frontend + FRONTEND_CHECKPOINT_RACES);
    slot = frontend + FRONTEND_CITY_CHECKPOINT_RACE + 4u * city;
    race = MEM32(slot);
    if (circuit_mode) {
        /* Every circuit is open; the retail count may have cut the choice short. */
        races = CIRCUIT_FIRST_RACE + circuits;
        if (same_city && selected >= CIRCUIT_FIRST_RACE && selected < races) race = selected;
        if (race < CIRCUIT_FIRST_RACE || race >= races) race = CIRCUIT_FIRST_RACE;
        circuit_box = race - CIRCUIT_FIRST_RACE;
    } else {
        /* A count above the retail limit is every installed race. */
        if (races > RETAIL_UNLOCK_LIMIT && races > circuits) races -= circuits;
        if (race >= races) race = races ? races - 1u : 0u;
    }
    MEM32(frontend + FRONTEND_CHECKPOINT_RACES) = races;
    MEM32(slot) = race;
    MEM32(frontend + FRONTEND_CHECKPOINT_RACE) = race;
}

/* The race picture strip (MmxRaceListNode, node in EBX) keeps the city's
 * pictures in a map of race number -> texture: head node at +0xC, nodes
 * {left, parent, right, race, texture}, the head standing for every leaf.
 * sub_00103BE1 draws them in order, the n-th in box n, highlighting box Race;
 * sub_001038D0 draws one box, with its box number (on entry at +0x18) for the
 * medal lookup. The Circuit strip's Race is the selection less 20
 * (tools/native-menu/generate.py): in Circuit mode the walk starts at the
 * first circuit and each box looks up its own race. The strip scrolls one box
 * a frame towards its Race (first box shown at +0x2C, twelve boxes wide); a
 * Race read below 20 for a while would scroll every box off screen, so the
 * scroll starts each frame around the selected circuit. */
#define STRIP_PICTURES 0xCu
#define STRIP_SCROLL 0x2Cu
#define STRIP_BOXES 12u
#define PICTURE_RACE 0xCu
#define BOX_NUMBER_ARGUMENT 0x18u

extern void sub_00103BE1_original(void);
extern void sub_001038D0_original(void);

void sub_00103BE1(void)
{
    uint32_t node = g_ebx, head = 0, first = 0, start, n;
    unsigned depth;
    if (circuit_mode && guest_object(node, 0x48u) && guest_object(head = MEM32(node + STRIP_PICTURES), 0x18u)) {
        /* The first picture of a circuit: the lower bound of race 20. */
        start = head;
        for (n = MEM32(head + 4u), depth = 0; n != head && guest_object(n, 0x18u) && depth < 64u; ++depth)
            if ((int32_t)MEM32(n + PICTURE_RACE) >= CIRCUIT_FIRST_RACE) { start = n; n = MEM32(n); }
            else n = MEM32(n + 8u);
        if (start != head) {
            uint32_t scroll = MEM32(node + STRIP_SCROLL);
            first = MEM32(head);
            MEM32(head) = start;
            if (scroll > circuit_box) scroll = circuit_box;
            if (circuit_box >= scroll + STRIP_BOXES) scroll = circuit_box - (STRIP_BOXES - 1u);
            MEM32(node + STRIP_SCROLL) = scroll;
        }
    }
    sub_00103BE1_original();
    if (first) MEM32(head) = first;
}

void sub_001038D0(void)
{
    if (circuit_mode) MEM32(g_esp + BOX_NUMBER_ARGUMENT) += CIRCUIT_FIRST_RACE;
    sub_001038D0_original();
}

static uint32_t circuit_city_screen, circuit_setup_screen;

void mm3_circuit_set_screens(uint32_t city_select, uint32_t race_setup)
{
    circuit_city_screen = city_select;
    circuit_setup_screen = race_setup;
}

/* Back from a race, sub_00120AE8 rebuilds the menu path of the mode played:
 * for single player it pushes Single Player Type, City Select and Race Setup
 * (the names behind 0x3A1744, 0x3A171C and 0x3A1774), each through
 * sub_00120A9A(frontend, name, root), which looks the screen up by name. A
 * circuit race comes back to the Circuit city select and race setup instead,
 * whose on-show keeps Circuit mode on. */
#define PATH_CITY_SELECT_NAME 0x003A171Cu
#define PATH_RACE_SETUP_NAME 0x003A1774u
#define PATH_CITY_SELECT_RETURN 0x00120BB0u
#define PATH_RACE_SETUP_RETURN 0x00120CEBu

extern void sub_00120A9A_original(void);

void sub_00120A9A(void)
{
    if (circuit_mode && circuit_city_screen && circuit_setup_screen) {
        uint32_t back = MEM32(g_esp), name = MEM32(g_esp + 8u);
        if (back == PATH_CITY_SELECT_RETURN && name == MEM32(PATH_CITY_SELECT_NAME))
            MEM32(g_esp + 8u) = circuit_city_screen;
        else if (back == PATH_RACE_SETUP_RETURN && name == MEM32(PATH_RACE_SETUP_NAME))
            MEM32(g_esp + 8u) = circuit_setup_screen;
    }
    sub_00120A9A_original();
}
