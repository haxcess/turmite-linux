/* Browser adaptation: sequential weighted batches, one shared six-color tape.
 * No DOM dependencies; the simulation can be exercised independently. */
(() => {
  'use strict';
  const turns = 'FRLBHNESW';
  const dx = [0, 1, 0, -1], dy = [-1, 0, 1, 0];
  const clone = rule => ({...rule, table: rule.table.map(row => row.map(a => ({...a})))});

  class Universe {
    constructor(width, height, catalog, random = Math.random) {
      if (!Number.isInteger(width) || !Number.isInteger(height) || width < 1 || height < 1 || width * height > 524288)
        throw new RangeError('Universe must contain 1–524288 cells.');
      this.width = width;
      this.height = height;
      this.catalog = catalog;
      this.random = random;
      this.cells = new Uint8Array(width * height);
      this.occupancy = new Uint8Array(this.cells.length);
      this.dirty = new Uint32Array(this.cells.length);
      this.marked = new Uint8Array(this.cells.length);
      this.dirtyCount = 0;
      this.ants = [];
      this.steps = this.collisions = this.mutations = this.rebirths = 0;
      this.cursor = this.remaining = 0;
      this.collisionMutation = false;
    }

    pick(n) { return Math.floor(this.random() * n); }

    add(x = this.pick(this.width), y = this.pick(this.height)) {
      if (this.ants.length >= 32 || this.ants.length >= this.cells.length) return false;
      let position = y * this.width + x;
      // Bounded linear probe also works when a tiny world is nearly full.
      while (this.occupancy[position]) position = (position + 1) % this.cells.length;
      let id = 1;
      while (this.ants.some(a => a.id === id)) id++;
      const rule = clone(this.catalog[this.pick(this.catalog.length)]);
      const ant = {id, x: position % this.width, y: Math.floor(position / this.width),
        rule, state: this.pick(rule.states), heading: this.pick(4), offset: this.pick(6),
        weight: 1 + this.pick(4), cooldown: 0};
      this.occupancy[position] = id;
      this.ants.push(ant);
      return true;
    }

    remove() {
      if (this.ants.length <= 1) return false;
      const ant = this.ants.pop();
      this.occupancy[ant.y * this.width + ant.x] = 0;
      this.cursor %= this.ants.length;
      this.remaining = 0;
      return true;
    }

    mutate(ant) {
      const rule = ant.rule, action = rule.table[this.pick(rule.states)][this.pick(rule.colors)];
      if (this.pick(16) === 0) action.halt = !action.halt;
      else {
        const fields = ['t'];
        if (rule.colors > 1) fields.push('w');
        if (rule.states > 1) fields.push('n');
        const field = fields[this.pick(fields.length)];
        if (field === 't') action.t = turns[(turns.indexOf(action.t) + 1 + this.pick(8)) % 9];
        else {
          const limit = field === 'w' ? rule.colors : rule.states;
          action[field] = (action[field] + 1 + this.pick(limit - 1)) % limit;
        }
      }
      this.mutations++;
    }

    complexity(max) {
      let draw = this.pick((1 << max) - 1);
      for (let value = 1; value < max; value++) {
        const weight = 1 << (max - value);
        if (draw < weight) return value;
        draw -= weight;
      }
      return max;
    }

    rebirth(ant) {
      const states = this.complexity(4), colors = this.complexity(6);
      ant.rule = {states, colors, table: Array.from({length: states}, () =>
        Array.from({length: colors}, () => ({w: this.pick(colors), t: turns[this.pick(9)],
          n: this.pick(states), halt: this.pick(64) === 0})))};
      ant.state = this.pick(states);
      ant.heading = this.pick(4);
      ant.offset = this.pick(6);
      ant.weight = 1 + this.pick(4);
      this.rebirths++;
    }

    step(ant) {
      const index = ant.y * this.width + ant.x;
      const local = (this.cells[index] + 6 - ant.offset) % 6;
      const action = ant.rule.table[ant.state][Math.min(local, ant.rule.colors - 1)];
      const color = (action.w + ant.offset) % 6;
      if (this.cells[index] !== color) {
        this.cells[index] = color;
        if (!this.marked[index]) {
          this.marked[index] = 1;
          this.dirty[this.dirtyCount++] = index;
        }
      }
      ant.state = action.n;
      const absolute = 'NESW'.indexOf(action.t);
      if (absolute >= 0) ant.heading = absolute;
      else if (action.t === 'R') ant.heading = (ant.heading + 1) & 3;
      else if (action.t === 'L') ant.heading = (ant.heading + 3) & 3;
      else if (action.t === 'B') ant.heading = (ant.heading + 2) & 3;
      this.steps++;
      if (action.halt) { this.rebirth(ant); return; }
      if (action.t === 'H') return;
      const x = (ant.x + dx[ant.heading] + this.width) % this.width;
      const y = (ant.y + dy[ant.heading] + this.height) % this.height;
      const next = y * this.width + x;
      if (this.occupancy[next] && this.occupancy[next] !== ant.id) {
        this.collisions++;
        if (this.collisionMutation) this.mutate(ant);
        ant.cooldown = 2;
        return; // Resident keeps the cell; attacker stays at its old position.
      }
      this.occupancy[index] = 0;
      this.occupancy[next] = ant.id;
      ant.x = x;
      ant.y = y;
    }

    run(budget) {
      if (!this.ants.length) return;
      for (let i = 0; i < budget; i++) {
        const ant = this.ants[this.cursor];
        if (!this.remaining) this.remaining = ant.weight * 16;
        if (ant.cooldown) { ant.cooldown--; this.remaining = 1; }
        else this.step(ant);
        if (--this.remaining === 0 || ant.cooldown) {
          this.remaining = 0;
          this.cursor = (this.cursor + 1) % this.ants.length;
        }
      }
    }
  }
  globalThis.TurmiteUniverse = Universe;
})();
