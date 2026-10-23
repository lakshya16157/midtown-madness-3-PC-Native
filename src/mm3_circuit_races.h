#ifndef MM3_CIRCUIT_RACES_H
#define MM3_CIRCUIT_RACES_H

#include <stddef.h>
#include <stdint.h>

enum { CIRCUIT_FIRST_RACE = 20, CIRCUIT_RACES = 10 };

/* Text for the circuit races' list names and info boxes (from
 * game_files/Data/mm3_circuits.ini), the Circuit menu entry and the lap
 * messages, or NULL for every other locale key. The pointer stays valid until
 * the next call. */
const char *mm3_circuit_label(const char *key);

/* One mm3_circuits.ini line, "<city 1|2> <race 20-29> <printable name>" with
 * an optional "<TAB><printable info>": 1 with the fields set (info empty when
 * absent), else 0. */
int mm3_circuit_parse_line(const char *line, unsigned *city, unsigned *race,
                           char *name, size_t size, char *info, size_t info_size);

/* The single player Circuit button (1) or another mode (0): race setup lists
 * only the circuits, or every race but them. */
void mm3_circuit_set_mode(int on);

/* Guest addresses of the names "singlePlayer/screenCircuitCitySelect" and
 * "singlePlayer/screenCircuitRaceSetup", which the menu path after a circuit
 * race uses in place of the Checkpoint city select and race setup. */
void mm3_circuit_set_screens(uint32_t city_select, uint32_t race_setup);

#endif
