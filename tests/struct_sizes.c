#include "ant.h"
#include <stdio.h>
#include <stddef.h>
int main(void) {
    printf("sizeof(Ant)=%zu\n", sizeof(Ant));
    printf("sizeof(AntStats)=%zu\n", sizeof(AntStats));
    printf("sizeof(AntColony)=%zu\n", sizeof(AntColony));
    printf("positions_bytes=%zu\n", sizeof(((AntColony *)0)->positions));
    printf("offsetof(Ant: flags=%zu heading=%zu state=%zu rule_index=%zu rule=%zu tokens_fp=%zu token_rate=%zu capacity=%zu weight=%zu rng_state=%zu)\n",
           offsetof(Ant, flags), offsetof(Ant, heading), offsetof(Ant, state), offsetof(Ant, rule_index),
           offsetof(Ant, rule), offsetof(Ant, tokens_fp), offsetof(Ant, token_rate),
           offsetof(Ant, token_capacity_fp), offsetof(Ant, weight), offsetof(Ant, rng_state));
}
