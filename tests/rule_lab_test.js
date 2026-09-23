/* Run with node after make rule-catalog and generating rule-traces.js. */
(function () {
  "use strict";
  if (typeof require !== "undefined") {
    require("../tools/rule-engine.js"); require("../tools/rule-catalog.js"); require("./rule-traces.js");
  }
  const a = globalThis.RuleLab;
  function assert(test, message) { if (!test) throw Error(message); }
  function reject(fn) { let threw=false; try { fn(); } catch { threw=true; } assert(threw,"Invalid input accepted"); }
  for (const r of globalThis.TURMITE_RULES) a.validate(r);
  for (let seed=1;seed<=100;seed++) {
    const r=a.generate(1+seed%4,1+seed%6,seed,true);
    assert(JSON.stringify(r)===JSON.stringify(a.generate(r.states,r.colors,seed,true)),"Seed reproducibility");
    assert(!r.table.flat().some(x=>x.halt),"Generator introduced HALT");
    assert(a.exportC(r).includes("TURN_"),"C export");
    assert(JSON.stringify(a.mutate(r,seed,true).table)!==JSON.stringify(r.table),"Mutation did nothing");
  }
  const fixture=t=>({id:"test",name:"Test",states:1,colors:2,table:[[{w:1,t,n:0,halt:false},{w:1,t,n:0,halt:false}]]});
  for (const [t,x,y,h] of [["F",1,0,0],["R",2,1,1],["L",0,1,3],["B",1,2,2],["H",1,1,0],["N",1,0,0],["E",2,1,1],["S",1,2,2],["W",0,1,3]]) {
    const sim=new a.Simulation(fixture(t),3,3);sim.step();
    assert(sim.x===x && sim.y===y && sim.heading===h && sim.cells[4]===1,"Turn "+t);
  }
  let sim=new a.Simulation(fixture("F"),3,3);sim.step(2);assert(sim.y===2,"Wrap");
  const halt=fixture("R");halt.table[0][0].halt=true;sim=new a.Simulation(halt,3,3);sim.step(10);
  assert(sim.steps===1 && sim.halted && sim.x===1 && sim.y===1 && sim.heading===1 && sim.cells[4]===1,"HALT order");
  sim=new a.Simulation(fixture("H"),3,3);sim.step(50);assert(sim.moves===0 && sim.changes===1 && sim.quiet===49,"Hold counters");
  sim=new a.Simulation(fixture("L"),3,3);sim.cells[4]=5;sim.nonzero=1;sim.step();
  assert(sim.cells[4]===1 && sim.x===0 && sim.y===1 && sim.state===0,"Unsupported-color fallback");
  reject(()=>a.generate(5,6,1,false)); reject(()=>a.generate(2,7,1,false)); reject(()=>a.generate(2,6,0,false));
  reject(()=>a.validate({...fixture("L"),id:undefined})); reject(()=>a.validate({...fixture("L"),name:"line\nbreak"}));
  reject(()=>a.validate({...fixture("L"),table:[]}));
  const escaped=fixture("L"); escaped.name='Quotes " and backslash \\ and ??/';
  assert(a.exportC(escaped).includes('\\?\\?/'),"C trigraph escaping");
  for(const [i,s,h,x,y,ns,nh,hash,nonzero,executed] of globalThis.TURMITE_TRACES) {
    const sim=new a.Simulation(globalThis.TURMITE_RULES[i],64,48,s,h);sim.step(10000);
    let actual=2166136261;for(const c of sim.cells)actual=Math.imul(actual^c,16777619)>>>0;
    assert(sim.x===x && sim.y===y && sim.state===ns && sim.heading===nh && actual===hash && sim.nonzero===nonzero && sim.steps===executed,"C trace mismatch: "+i+" state "+s+" heading "+h);
  }
  globalThis.RULE_LAB_TEST_RESULT = `Rule lab passed: 100 generated tables, input/turn/hold/HALT/fallback checks, ${globalThis.TURMITE_TRACES.length} C interpreter traces.`;
  if(typeof console!=="undefined") console.log(globalThis.RULE_LAB_TEST_RESULT);
})();
