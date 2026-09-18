#include "rules.h"

#include "rng.h"

/* Compact constructors keep the static rule catalogue readable. Each table row
 * is one internal state; each column is the color currently under the ant. */
#define A(w,t,n) { (uint8_t)(w), (t), (uint8_t)(n), false }
#define HLT(w,t,n) { (uint8_t)(w), (t), (uint8_t)(n), true }
#define NOOP(c) { (uint8_t)(c), TURN_F, 0, false }

/* Built-in gene pool. Rules may use fewer than six colors or four states; the
 * interpreter only indexes the active rectangle declared by each rule. */
static const TurmiteRule RULES[] = {
    {
        "langtons", "Langton's Ant", 1, 2,
        {
            { A(1, TURN_R, 0), A(0, TURN_L, 0) }
        }
    },
    {
        "symmetrical", "Symmetrical", 1, 6,
        {
            { A(1,TURN_R,0), A(2,TURN_R,0), A(3,TURN_L,0), A(4,TURN_L,0), A(5,TURN_R,0), A(0,TURN_R,0) }
        }
    },
    {
        "snowflake", "Snowflake", 3, 2,
        {
            { A(1,TURN_L,1), A(1,TURN_R,0) },
            { A(1,TURN_B,1), A(1,TURN_B,2) },
            { A(0,TURN_F,2), A(0,TURN_B,0) }
        }
    },
    {
        "archimedes", "Archimedes Spiral", 1, 6,
        {
            { A(1,TURN_L,0), A(2,TURN_R,0), A(3,TURN_R,0), A(4,TURN_R,0), A(5,TURN_R,0), A(0,TURN_L,0) }
        }
    },
    {
        "logarithmic", "Logarithmic Spiral", 1, 6,
        {
            { A(1,TURN_R,0), A(2,TURN_L,0), A(3,TURN_L,0), A(4,TURN_L,0), A(5,TURN_L,0), A(0,TURN_R,0) }
        }
    },
    {
        "squarefiller", "Square Filler", 1, 6,
        {
            { A(1,TURN_L,0), A(2,TURN_R,0), A(3,TURN_R,0), A(4,TURN_R,0), A(5,TURN_R,0), A(0,TURN_R,0) }
        }
    },
    {
        "builders", "Builders", 2, 2,
        {
            { A(1,TURN_L,1), A(1,TURN_R,1) },
            { A(1,TURN_R,1), A(0,TURN_R,0) }
        }
    },
    {
        "critterhighway", "Critter Highway", 1, 6,
        {
            { A(1,TURN_H,0), A(2,TURN_R,0), A(3,TURN_S,0), A(4,TURN_H,0), A(5,TURN_N,0), A(0,TURN_L,0) }
        }
    },
    {
        "dotgrid", "Dotgrid", 3, 3,
        {
            { A(1,TURN_R,1), A(0,TURN_L,2), A(1,TURN_B,0), NOOP(0), NOOP(0), NOOP(0) },
            { A(0,TURN_F,0), A(1,TURN_R,1), NOOP(0), NOOP(0), NOOP(0), NOOP(0) },
            { A(0,TURN_L,1), A(1,TURN_R,2), NOOP(0), NOOP(0), NOOP(0), NOOP(0) }
        }
    },
    {
        "pipedream", "Pipedream", 1, 6,
        {
            { A(1,TURN_F,0), A(2,TURN_B,0), A(3,TURN_W,0), A(4,TURN_L,0), A(5,TURN_W,0), A(0,TURN_F,0) }
        }
    },
    {
        "scaffold", "Scaffold", 1, 6,
        {
            { A(1,TURN_F,0), A(2,TURN_B,0), A(3,TURN_L,0), A(4,TURN_N,0), A(5,TURN_E,0), A(0,TURN_S,0) }
        }
    },
    {
        "transistor", "Transistor", 1, 6,
        {
            { A(1,TURN_W,0), A(2,TURN_B,0), A(3,TURN_N,0), A(4,TURN_R,0), A(5,TURN_S,0), A(0,TURN_L,0) }
        }
    },
    {
        "steppedpyramid", "Stepped Pyramid", 2, 2,
        {
            { A(0,TURN_R,1), A(0,TURN_L,0) },
            { A(1,TURN_L,1), A(1,TURN_F,0) }
        }
    },
    {
        "snowflakeish", "Snowflake-ish", 2, 2,
        {
            { A(0,TURN_R,1), A(1,TURN_R,1) },
            { A(1,TURN_L,1), A(1,TURN_L,0) }
        }
    },
    {
        "goldenrect", "Golden Rectangle", 2, 2,
        {
            { A(1,TURN_L,1), A(1,TURN_L,1) },
            { A(1,TURN_R,1), A(0,TURN_F,0) }
        }
    },
    {
        "coiledrope", "Coiled Rope", 2, 2,
        {
            { A(1,TURN_F,1), A(1,TURN_L,0) },
            { A(1,TURN_R,1), A(0,TURN_F,0) }
        }
    },
    {
        "wormtrails", "Worm Trails", 2, 2,
        {
            { A(1,TURN_R,1), A(1,TURN_L,1) },
            { A(1,TURN_R,1), A(0,TURN_R,0) }
        }
    },
    {
        "mazelike", "Maze-like Growth", 3, 2,
        {
            { A(1,TURN_L,1), A(1,TURN_L,1) },
            { A(1,TURN_F,0), A(0,TURN_F,2) },
            { A(0,TURN_L,1), A(1,TURN_F,1) }
        }
    },
    /* Lab exports promoted to the immutable startup catalogue. */
    {
        "red_walker", "Red Walker", 3, 4,
        {
        { A(2, TURN_B, 0), A(0, TURN_L, 0), A(3, TURN_R, 2), A(2, TURN_L, 2) },
        { A(1, TURN_L, 0), A(0, TURN_B, 1), A(2, TURN_N, 2), A(3, TURN_F, 1) },
        { A(3, TURN_B, 2), A(2, TURN_R, 0), A(2, TURN_F, 0), A(1, TURN_B, 1) }
        }
    },
    {
        "mutant_bcff52d4", "Mutant bcff52d4 walker", 3, 2,
        {
        { A(0, TURN_R, 1), A(0, TURN_L, 2) },
        { A(1, TURN_R, 2), A(0, TURN_F, 2) },
        { A(1, TURN_L, 0), A(0, TURN_B, 1) }
        }
    },
    {
        "mutant_f6438bee", "Mutant f6438bee", 3, 3,
        {
        { A(1, TURN_L, 1), A(2, TURN_F, 2), A(2, TURN_F, 0) },
        { A(2, TURN_B, 0), A(0, TURN_L, 0), A(0, TURN_R, 1) },
        { A(2, TURN_F, 2), A(2, TURN_B, 1), A(1, TURN_F, 1) }
        }
    }
};

size_t rules_count(void)
{
    return sizeof(RULES) / sizeof(RULES[0]);
}

const TurmiteRule *rules_get(size_t index)
{
    if (index >= rules_count()) {
        return NULL;
    }
    return &RULES[index];
}

/* Modulo selection is convenient for RNG output and mutation without a branch
 * at every caller. */
const TurmiteRule *rules_pick(size_t index)
{
    return rules_get(index % rules_count());
}

const char *turn_name(TurnCode turn)
{
    switch (turn) {
        case TURN_F: return "F";
        case TURN_R: return "R";
        case TURN_L: return "L";
        case TURN_B: return "B";
        case TURN_H: return "H";
        case TURN_N: return "N";
        case TURN_E: return "E";
        case TURN_S: return "S";
        case TURN_W: return "W";
        default: return "?";
    }
}

/* Rules are static objects, so pointer identity is enough to recover the small
 * catalogue index stored in ant/debug metadata. */
size_t rules_index_of(const TurmiteRule *rule)
{
    if (!rule) return RULE_INDEX_RUNTIME;
    for (size_t i = 0; i < rules_count(); ++i) {
        if (rules_get(i) == rule) return i;
    }
    return RULE_INDEX_RUNTIME;
}


static uint32_t rule_random(uint32_t *state, uint32_t limit)
{
    *state = lfsr32_advance(*state);
    return *state % limit;
}

/* Weights halve for each added unit: states 8:4:2:1, colors 32:16:8:4:2:1.
 * Bounded inverse-CDF sampling avoids retries and favors simpler machines. */
uint8_t rules_random_complexity(uint32_t *rng, uint8_t maximum)
{
    if (maximum < 1 || maximum > TURMITE_COLORS) return 1;
    uint32_t draw = rule_random(rng, (UINT32_C(1) << maximum) - 1u);
    for (uint8_t value = 1; value < maximum; ++value) {
        uint32_t weight = UINT32_C(1) << (maximum - value);
        if (draw < weight) return value;
        draw -= weight;
    }
    return maximum;
}

void rules_generate(TurmiteRule *out, uint32_t *rng)
{
    *out = (TurmiteRule){ .id = "runtime", .name = "Runtime rule" };
    out->states = rules_random_complexity(rng, TURMITE_MAX_STATES);
    out->colors = rules_random_complexity(rng, TURMITE_COLORS);
    for (uint8_t s = 0; s < out->states; ++s) {
        for (uint8_t c = 0; c < out->colors; ++c) {
            out->table[s][c] = (RuleAction){
                .write_color = (uint8_t)rule_random(rng, out->colors),
                .turn = (TurnCode)rule_random(rng, TURN_W + 1u),
                .next_state = (uint8_t)rule_random(rng, out->states),
                .halt = rule_random(rng, 64u) == 0
            };
        }
    }
}

void rules_mutate(TurmiteRule *rule, uint32_t *rng)
{
    uint8_t state = (uint8_t)rule_random(rng, rule->states);
    uint8_t color = (uint8_t)rule_random(rng, rule->colors);
    RuleAction *a = &rule->table[state][color];
    /* A rare HALT toggle makes rebirth reachable from the non-halting library.
     * Other mutations choose a field with at least two possible values. */
    if (rule_random(rng, 16u) == 0) {
        a->halt = !a->halt;
    } else {
        unsigned fields[3] = {0, 0, 0}, count = 1; /* turn is always mutable */
        if (rule->colors > 1) fields[count++] = 1;
        if (rule->states > 1) fields[count++] = 2;
        switch (fields[rule_random(rng, count)]) {
            case 0: a->turn = (TurnCode)((a->turn + 1u + rule_random(rng, TURN_W)) % (TURN_W + 1u)); break;
            case 1: a->write_color = (uint8_t)((a->write_color + 1u + rule_random(rng, rule->colors - 1u)) % rule->colors); break;
            case 2: a->next_state = (uint8_t)((a->next_state + 1u + rule_random(rng, rule->states - 1u)) % rule->states); break;
        }
    }
    rule->id = "runtime";
    rule->name = "Runtime rule";
}
