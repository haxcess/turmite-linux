/* Single-ant rule semantics; no renderer, token scheduler or collisions. */
(function (root) {
  "use strict";
  const turns = ["F", "R", "L", "B", "H", "N", "E", "S", "W"];
  const clone = value => JSON.parse(JSON.stringify(value));
  function validate(rule) {
    if (!rule || typeof rule.id !== "string" || !/^[a-z][a-z0-9_]{0,63}$/.test(rule.id) ||
        typeof rule.name !== "string" || !/^[\x20-\x7e]{1,80}$/.test(rule.name))
      throw Error("Use a lowercase ID (letters, digits, underscore) and a printable ASCII name (1–80 characters).");
    if (!Number.isInteger(rule.states) || rule.states < 1 || rule.states > 4 ||
        !Number.isInteger(rule.colors) || rule.colors < 1 || rule.colors > 6)
      throw Error("C rules support 1–4 states and 1–6 colors.");
    if (!Array.isArray(rule.table) || rule.table.length !== rule.states) throw Error("Wrong number of state rows.");
    for (const row of rule.table) {
      if (!Array.isArray(row) || row.length !== rule.colors) throw Error("Each row needs an action per active color.");
      for (const a of row) {
        if (!a || !Number.isInteger(a.w) || a.w < 0 || a.w >= rule.colors || !turns.includes(a.t) ||
            !Number.isInteger(a.n) || a.n < 0 || a.n >= rule.states || typeof a.halt !== "boolean")
          throw Error("Invalid write color, turn, next state, or halt flag.");
      }
    }
    return rule;
  }
  function random(seed) {
    if (!Number.isInteger(seed) || seed < 1 || seed > 0xffffffff) throw Error("Seed must be an integer from 1 to 4294967295.");
    let x = seed >>> 0;
    return limit => { x ^= x << 13; x ^= x >>> 17; x ^= x << 5; return (x >>> 0) % limit; };
  }
  function generate(states, colors, seed, extended = false) {
    if (!Number.isInteger(states) || states < 1 || states > 4 || !Number.isInteger(colors) || colors < 1 || colors > 6)
      throw Error("Choose 1–4 states and 1–6 colors.");
    const pick = random(seed), options = extended ? turns : ["F", "R", "L", "B"];
    return validate({id: "novel_" + seed.toString(16), name: "Generated " + seed.toString(16), states, colors,
      table: Array.from({length: states}, () => Array.from({length: colors}, () =>
        ({w: pick(colors), t: options[pick(options.length)], n: pick(states), halt: false})))});
  }
  function mutate(rule, seed, extended = false) {
    const result = clone(validate(rule)), pick = random(seed), options = extended ? turns : ["F", "R", "L", "B"];
    const a = result.table[pick(result.states)][pick(result.colors)];
    const alternatives = options.filter(t => t !== a.t);
    a.t = alternatives[pick(alternatives.length)];
    result.id = "mutant_" + seed.toString(16); result.name = "Mutant " + seed.toString(16);
    return result;
  }
  function exportC(rule) {
    validate(rule);
    // Escape '?' as well to prevent C17 trigraphs changing literal names.
    const literal = s => JSON.stringify(s).replace(/\?/g, "\\?");
    const rows = rule.table.map(row => "        { " + row.map(a =>
      `${a.halt ? "HLT" : "A"}(${a.w}, TURN_${a.t}, ${a.n})`).join(", ") + " }");
    return `    {\n        ${literal(rule.id)}, ${literal(rule.name)}, ${rule.states}, ${rule.colors},\n        {\n${rows.join(",\n")}\n        }\n    },`;
  }
  class Simulation {
    constructor(rule, width = 256, height = 192, state = 0, heading = 0) {
      this.rule = clone(validate(rule));
      if (!Number.isInteger(width) || width < 1 || width > 2048 || !Number.isInteger(height) || height < 1 || height > 2048 ||
          !Number.isInteger(state) || state < 0 || state >= rule.states || !Number.isInteger(heading) || heading < 0 || heading > 3)
        throw Error("Invalid canvas dimensions or starting state/heading.");
      Object.assign(this, {width, height, x: Math.floor(width/2), y: Math.floor(height/2), state, heading,
        steps: 0, moves: 0, changes: 0, nonzero: 0, visited: 1, quiet: 0, halted: false});
      this.cells = new Uint8Array(width * height);
      this.seen = new Uint8Array(width * height); this.seen[this.y * width + this.x] = 1;
    }
    step(count = 1) {
      if (!Number.isInteger(count) || count < 0 || count > 1000000) throw Error("Invalid step count.");
      for (let i = 0; i < count && !this.halted; i++) {
        const index = this.y * this.width + this.x, color = this.cells[index];
        const a = this.rule.table[this.state]?.[color] || {w: color, t: "F", n: this.state, halt: false};
        if (color !== a.w) { this.changes++; this.quiet = 0; } else this.quiet++;
        this.nonzero += Number(a.w !== 0) - Number(color !== 0);
        this.cells[index] = a.w; this.state = a.n;
        const relative = {F: 0, R: 1, B: 2, L: 3, H: 0};
        this.heading = "NESW".includes(a.t) ? "NESW".indexOf(a.t) : (this.heading + relative[a.t]) % 4;
        this.steps++;
        if (a.halt) { this.halted = true; break; }
        if (a.t !== "H") {
          this.x = (this.x + [0, 1, 0, -1][this.heading] + this.width) % this.width;
          this.y = (this.y + [-1, 0, 1, 0][this.heading] + this.height) % this.height;
          this.moves++;
        }
        const next = this.y * this.width + this.x;
        if (!this.seen[next]) { this.seen[next] = 1; this.visited++; }
      }
    }
  }
  root.RuleLab = {validate, generate, mutate, exportC, Simulation, turns, clone};
  if (typeof module !== "undefined") module.exports = root.RuleLab;
})(globalThis);
