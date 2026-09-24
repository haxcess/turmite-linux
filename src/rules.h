#ifndef TURMITE_RULES_H
#define TURMITE_RULES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "colors.h"
#define TURMITE_MAX_STATES 4
#define TURMITE_MAX_RULES 64
#define RULE_INDEX_RUNTIME UINT16_MAX

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
    uint8_t turn; /* TurnCode; byte storage keeps each action four bytes. */
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

/* Private per-ant RNG state, never the shared universe generator. */
uint8_t rules_random_complexity(uint32_t *rng, uint8_t maximum);
void rules_generate(TurmiteRule *out, uint32_t *rng);
/* Exactly one action field changes; table dimensions are retained. */
void rules_mutate(TurmiteRule *rule, uint32_t *rng);

#endif
