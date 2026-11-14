/* Optional regression checks; not loaded by the app. */
globalThis.runTurmiteTests = () => {
  let checks = 0;
  function assert(ok, message) { checks++; if (!ok) throw new Error(message); }
  let seed = 123456;
  const random = () => { seed ^= seed << 13; seed ^= seed >>> 17; seed ^= seed << 5; return (seed >>> 0) / 4294967296; };
  function single(turn, halt = false) {
    const rule = {states: 1, colors: 2, table: [[{w: 1, t: turn, n: 0, halt}, {w: 0, t: turn, n: 0, halt}]]};
    const u = new TurmiteUniverse(5, 5, [rule], random);
    u.add(2, 2);
    Object.assign(u.ants[0], {state: 0, heading: 0, offset: 0});
    return u;
  }
  for (const [turn, x, y, heading] of [['F',2,1,0], ['R',3,2,1], ['L',1,2,3], ['B',2,3,2],
    ['H',2,2,0], ['N',2,1,0], ['E',3,2,1], ['S',2,3,2], ['W',1,2,3]]) {
    const u = single(turn), a = u.ants[0];
    u.step(a);
    assert(a.x === x && a.y === y && a.heading === heading && u.cells[12] === 1, `${turn} interpreter`);
  }
  const langton = new TurmiteUniverse(9, 9, [TURMITE_RULES[0]], random);
  langton.add(4, 4);
  Object.assign(langton.ants[0], {state: 0, heading: 0, offset: 0});
  for (let i = 0; i < 4; i++) langton.step(langton.ants[0]);
  assert(langton.ants[0].x === 4 && langton.ants[0].y === 4 && langton.cells.reduce((a,b) => a+b) === 4, 'Langton first four steps');
  const wrap = single('N');
  for (let i = 0; i < 3; i++) wrap.step(wrap.ants[0]);
  assert(wrap.ants[0].y === 4, 'Toroidal wrapping');
  const offset = single('H');
  offset.ants[0].offset = 4; offset.cells[12] = 1;
  offset.step(offset.ants[0]);
  assert(offset.cells[12] === 4, 'Offset and out-of-range local color clamp');
  const halt = single('F', true);
  halt.step(halt.ants[0]);
  assert(halt.rebirths === 1 && halt.ants[0].y === 2 && halt.cells[12] === 1, 'HALT writes and rebirths without moving');
  const collision = single('E');
  collision.add(3, 2); collision.collisionMutation = true;
  const original = JSON.stringify(collision.ants[0].rule);
  collision.step(collision.ants[0]);
  assert(collision.collisions === 1 && collision.mutations === 1 && collision.ants[0].x === 2, 'Collision policy');
  assert(original !== JSON.stringify(collision.ants[0].rule), 'Collision edits private rule');
  const u = new TurmiteUniverse(32, 24, TURMITE_RULES, random);
  for (let i = 0; i < 40; i++) u.add();
  assert(u.ants.length === 32, 'Population bound');
  const catalogBefore = JSON.stringify(TURMITE_RULES);
  u.collisionMutation = true;
  for (let batch = 0; batch < 400; batch++) {
    u.run(256);
    const occupied = new Set();
    for (const ant of u.ants) {
      const index = ant.y * u.width + ant.x;
      assert(!occupied.has(index) && u.occupancy[index] === ant.id, 'Unique occupancy');
      assert(ant.state < ant.rule.states && ant.heading < 4, 'Valid machine state');
      occupied.add(index);
    }
  }
  assert(u.cells.every(c => c < 6) && u.dirtyCount <= u.cells.length, 'Bounded tape and dirty list');
  assert(JSON.stringify(TURMITE_RULES) === catalogBefore, 'Catalog remains immutable');
  while (u.remove()) {}
  assert(u.ants.length === 1 && u.occupancy.filter(Boolean).length === 1, 'Removal releases occupancy');
  const tiny = new TurmiteUniverse(1, 1, TURMITE_RULES, random);
  assert(tiny.add() && !tiny.add(), 'Full tiny world terminates');
  tiny.run(100);
  assert(tiny.occupancy[0] === tiny.ants[0].id, 'Self-wrap retains occupancy');
  return checks;
};
