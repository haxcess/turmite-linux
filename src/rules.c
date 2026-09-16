#include "rules.h"

#define A(w,t,n) { (uint8_t)(w), (t), (uint8_t)(n), false }
#define HLT(w,t,n) { (uint8_t)(w), (t), (uint8_t)(n), true }
#define NOOP(c) { (uint8_t)(c), TURN_F, 0, false }

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
    // {
    //     "busybeaver3", "Busy Beaver (3-state)", 3, 2,
    //     {
    //         { A(1,TURN_E,1), A(1,TURN_W,2) },
    //         { A(1,TURN_W,0), A(1,TURN_E,1) },
    //         { A(1,TURN_W,1), HLT(1,TURN_E,0) }
    //     }
    // },
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
size_t rules_index_of(const TurmiteRule *rule)
{
    if (!rule) return 0;
    for (size_t i = 0; i < rules_count(); ++i) {
        if (rules_get(i) == rule) return i;
    }
    return 0;
}

