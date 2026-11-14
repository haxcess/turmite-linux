(() => {
  'use strict';
  const $ = id => document.getElementById(id);
  const canvas = $('universe'), ctx = canvas.getContext('2d', {alpha: false});
  const tape = document.createElement('canvas'), tapeContext = tape.getContext('2d', {alpha: false});
  const help = $('help'), helpToggle = $('help-toggle');
  let world, pixels, palette, heads, fullRedraw = true;
  let paused = matchMedia('(prefers-reduced-motion: reduce)').matches;
  let rate = 120000, previous = 0, statsAt = 0, measuredAt = 0, measuredSteps = 0, throughput = 0;
  let helpTimer, noticeTimer;

  function notice(message) {
    $('notice').textContent = message;
    $('notice').classList.add('visible');
    clearTimeout(noticeTimer);
    noticeTimer = setTimeout(() => $('notice').classList.remove('visible'), 2600);
  }

  function showHelp(show) {
    clearTimeout(helpTimer);
    if (!show && help.contains(document.activeElement)) helpToggle.focus();
    help.classList.toggle('dismissed', !show);
    help.inert = !show;
    helpToggle.setAttribute('aria-expanded', String(show));
    if (show) helpTimer = setTimeout(() => showHelp(false), 12000);
  }

  // five nearby hues and one accent.
  // Cache RGB tape colors and bright HSL read-head colors outside the hot loop.
  function theme() {
    const base = Math.floor(Math.random() * 360);
    document.documentElement.style.setProperty('--hue', base);
    const hues = Array.from({length: 6}, (_, i) => base + (i === 5 ? 60 : -60) + Math.floor(Math.random() * 180));
    const sample = document.createElement('canvas').getContext('2d');
    palette = hues.map((hue, i) => {
      sample.fillStyle = `hsl(${hue} 38% ${i === 0 ? 3 : 28 + i * 5}%)`;
      sample.fillRect(0, 0, 1, 1);
      return sample.getImageData(0, 0, 1, 1).data.slice(0, 3);
    });
    heads = hues.map(hue => `hsl(${hue} 95% 70%)`);
    fullRedraw = true;
  }

  function reset() {
    const population = world?.ants.length || 8;
    const mutation = world?.collisionMutation || false;
    // About one cell per two CSS pixels, with a strict memory/work bound.
    const scale = Math.max(2, Math.sqrt(innerWidth * innerHeight / 524288));
    const width = Math.max(1, Math.floor(innerWidth / scale));
    const height = Math.max(1, Math.floor(innerHeight / scale));
    world = new TurmiteUniverse(width, height, TURMITE_RULES);
    world.collisionMutation = mutation;
    for (let i = 0; i < population; i++) world.add();
    tape.width = width; tape.height = height;
    pixels = tapeContext.createImageData(width, height);
    fullRedraw = true;
    measuredAt = performance.now(); measuredSteps = throughput = 0;
  }

  function resize() {
    // Resize only the presentation; existing tape and ants survive window changes.
    const scale = Math.min(devicePixelRatio || 1, 2, Math.sqrt(4000000 / (innerWidth * innerHeight)));
    canvas.width = Math.max(1, Math.floor(innerWidth * scale));
    canvas.height = Math.max(1, Math.floor(innerHeight * scale));
    ctx.imageSmoothingEnabled = false;
    fullRedraw = true;
  }

  function paintCell(index) {
    const rgb = palette[world.cells[index]], at = index * 4;
    pixels.data[at] = rgb[0]; pixels.data[at + 1] = rgb[1]; pixels.data[at + 2] = rgb[2]; pixels.data[at + 3] = 255;
  }

  function render() {
    const changed = fullRedraw || world.dirtyCount > 0;
    if (fullRedraw) for (let i = 0; i < world.cells.length; i++) paintCell(i);
    for (let i = 0; i < world.dirtyCount; i++) {
      const index = world.dirty[i];
      if (!fullRedraw) paintCell(index);
      world.marked[index] = 0;
    }
    world.dirtyCount = 0;
    fullRedraw = false;
    if (changed) tapeContext.putImageData(pixels, 0, 0);
    ctx.drawImage(tape, 0, 0, canvas.width, canvas.height);
    const sx = canvas.width / world.width, sy = canvas.height / world.height;
    for (const ant of world.ants) {
      ctx.fillStyle = ctx.shadowColor = heads[ant.offset];
      ctx.shadowBlur = 9;
      ctx.fillRect(ant.x * sx, ant.y * sy, Math.max(2, sx), Math.max(2, sy));
    }
    ctx.shadowBlur = 0;
  }

  const actions = {
    pause() { paused = !paused; notice(paused ? 'Paused · Space to resume' : 'Universe running'); },
    reset() { reset(); notice('Universe reseeded'); },
    theme() { theme(); notice('Color theme refreshed'); },
    slower() { rate = Math.max(1000, rate / 2); notice(`Target ${(rate / 1000).toFixed(0)}k steps / second`); },
    faster() { rate = Math.min(1920000, rate * 2); notice(`Target ${(rate / 1000).toFixed(0)}k steps / second`); },
    add() { notice(world.add() ? `${world.ants.length} turmites` : 'Population limit reached'); },
    remove() { notice(world.remove() ? `${world.ants.length} turmites` : 'One turmite remains'); },
    mutate() { world.collisionMutation = !world.collisionMutation; notice(`Collision mutation ${world.collisionMutation ? 'on' : 'off'}`); },
    hud() { $('stats').hidden = !$('stats').hidden; },
    async fullscreen() {
      try {
        if (document.fullscreenElement) await document.exitFullscreen();
        else if (document.documentElement.requestFullscreen) await document.documentElement.requestFullscreen();
        else notice('Fullscreen is unavailable in this browser');
      } catch { notice('Fullscreen is unavailable in this browser'); }
    }
  };
  helpToggle.addEventListener('click', () => showHelp(help.classList.contains('dismissed')));
  $('close-help').addEventListener('click', () => showHelp(false));
  for (const button of document.querySelectorAll('[data-action]')) button.addEventListener('click', () => {
    actions[button.dataset.action]();
    showHelp(true);
  });
  help.addEventListener('pointerdown', () => showHelp(true));
  help.addEventListener('focusin', () => showHelp(true));
  addEventListener('keydown', event => {
    if (event.ctrlKey || event.metaKey || event.altKey || event.repeat) return;
    if (event.key === 'Escape') { showHelp(false); return; }
    if (event.key === '?') { event.preventDefault(); showHelp(help.classList.contains('dismissed')); return; }
    if (event.target.closest('input, select, textarea') || (event.key === ' ' && event.target.closest('button'))) return;
    const action = {' ': 'pause', r: 'reset', c: 'theme', '[': 'slower', ']': 'faster',
      '-': 'remove', '+': 'add', '=': 'add', m: 'mutate', h: 'hud', f: 'fullscreen'}[event.key.toLowerCase()];
    if (action) { event.preventDefault(); actions[action](); }
  });
  canvas.addEventListener('pointerdown', event => {
    if (event.button !== 0) return;
    const rect = canvas.getBoundingClientRect();
    const x = Math.min(world.width - 1, Math.max(0, Math.floor((event.clientX - rect.left) / rect.width * world.width)));
    const y = Math.min(world.height - 1, Math.max(0, Math.floor((event.clientY - rect.top) / rect.height * world.height)));
    notice(world.add(x, y) ? `${world.ants.length} turmites · planted` : 'Population limit reached');
  });
  addEventListener('resize', resize);
  document.addEventListener('visibilitychange', () => { previous = 0; measuredAt = performance.now(); measuredSteps = world.steps; });

  function frame(now) {
    const elapsed = previous ? Math.min(50, now - previous) : 0;
    previous = now;
    if (!document.hidden) {
      if (!paused) {
        const deadline = performance.now() + 5;
        let budget = Math.floor(rate * elapsed / 1000);
        while (budget > 0 && performance.now() < deadline) {
          const chunk = Math.min(256, budget);
          world.run(chunk);
          budget -= chunk;
        }
      }
      render();
      if (now - measuredAt >= 1000) {
        throughput = (world.steps - measuredSteps) * 1000 / (now - measuredAt);
        measuredAt = now; measuredSteps = world.steps;
      }
      if (now - statsAt >= 250) {
        $('stats').textContent = `${paused ? 'PAUSED' : 'LIVE'} · ${world.width} × ${world.height} · ${world.ants.length}/32 turmites\n${Math.round(throughput / 1000)}k steps/s · ${world.steps.toLocaleString()} steps\n${world.collisions} collisions · ${world.mutations} mutations · mutation ${world.collisionMutation ? 'on' : 'off'}`;
        statsAt = now;
      }
    }
    requestAnimationFrame(frame);
  }
  theme(); reset(); resize(); showHelp(true);
  if (paused) notice('Reduced motion · paused · Space to start');
  requestAnimationFrame(frame);
})();
