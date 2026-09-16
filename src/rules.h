#ifndef TURMITE_RULES_H
#define TURMITE_RULES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TURMITE_COLORS 6
#define TURMITE_MAX_STATES 4
#define TURMITE_MAX_RULES 64

/* A rule is a tiny table-driven state machine. The current internal state and
 * tape color select one RuleAction: write a color, turn/set heading, enter a
 * new state, and optionally halt. */

/* Turn codes mirror the supplied browser rule language. F/R/L/B are relative,
 * H holds position, and N/E/S/W set an absolute heading. */
typedef enum {
    TURN_F = 0,
    TURN_R,
    TURN_L,
    TURN_B,
    TURN_H,
    TURN_N,
    TURN_E,
    TURN_S,
    TURN_W
} TurnCode;

typedef struct {
    uint8_t write_color;
    TurnCode turn;
    uint8_t next_state;
    bool halt;
} RuleAction;

typedef struct {
    const char *id;
    const char *name;
    uint8_t states;
    uint8_t colors;
    RuleAction table[TURMITE_MAX_STATES][TURMITE_COLORS];
} TurmiteRule;

size_t rules_count(void);
const TurmiteRule *rules_get(size_t index);
const TurmiteRule *rules_pick(size_t index);
size_t rules_index_of(const TurmiteRule *rule);
const char *turn_name(TurnCode turn);

#endif
