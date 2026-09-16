#include "ant.h"
#include <stdio.h>
#include <stddef.h>
int main(void) {
    printf("sizeof(Ant)=%zu\n", sizeof(Ant));
    printf("sizeof(AntColony)=%zu\n", sizeof(AntColony));
    printf("offsetof(flags)=%zu x=%zu y=%zu rule=%zu tokens_fp=%zu token_rate=%zu capacity=%zu weight=%zu instructions=%zu last_token_us=%zu rng_state=%zu\n",
           offsetof(Ant, flags), offsetof(Ant, x), offsetof(Ant, y), offsetof(Ant, rule),
           offsetof(Ant, tokens_fp), offsetof(Ant, token_rate), offsetof(Ant, token_capacity_fp),
           offsetof(Ant, weight), offsetof(Ant, instructions), offsetof(Ant, last_token_us), offsetof(Ant, rng_state));
}
