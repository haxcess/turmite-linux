/* Emit a compiler smoke test for generated C entries, including HALT/escaping. */
if (typeof require !== "undefined") require("../tools/rule-engine.js");
const candidates = Array.from({length:24}, (_,i) => RuleLab.generate(1+i%4,1+i%6,i+1,true));
candidates[0].table[0][0].halt = true;
candidates[0].name = 'Quotes " backslash \\ trigraph ??/';
globalThis.RULE_EXPORT_C = `#include "rules.h"
#include <assert.h>
#define A(w,t,n) {w,t,n,false}
#define HLT(w,t,n) {w,t,n,true}
static const TurmiteRule generated[] = {
${candidates.map(r => RuleLab.exportC(r)).join("\n")}
};
int main(void) {
    assert(generated[0].table[0][0].halt);
    for (size_t i=0;i<sizeof(generated)/sizeof(generated[0]);++i) {
        const TurmiteRule *r=&generated[i];
        assert(r->states>=1 && r->states<=TURMITE_MAX_STATES);
        assert(r->colors>=1 && r->colors<=TURMITE_COLORS);
        for(unsigned s=0;s<r->states;++s) for(unsigned c=0;c<r->colors;++c) {
            assert(r->table[s][c].write_color<r->colors);
            assert(r->table[s][c].next_state<r->states);
        }
    }
}
`;
if (typeof process !== "undefined") process.stdout.write(globalThis.RULE_EXPORT_C);
