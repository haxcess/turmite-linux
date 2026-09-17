(function () {
  "use strict";
  const $ = id => document.getElementById(id), api = globalThis.RuleLab;
  if (!api || !globalThis.TURMITE_RULES) { $("message").textContent = "Missing tools scripts. Keep rule-lab.html beside the tools directory; run make rule-catalog if needed."; return; }
  const catalog = globalThis.TURMITE_RULES, palette = globalThis.TURMITE_PALETTE;
  let rule, sim, running = false, pixels;
  const context = $("canvas").getContext("2d");
  function message(text = "") { $("message").textContent = text; }
  function stop() { running = false; $("run").textContent = "Run"; }
  function guard(action) { return () => { try { action(); } catch (e) { stop(); message(e.message); } }; }
  function integer(id, min, max) { const n = Number($(id).value); if (!Number.isInteger(n) || n < min || n > max) throw Error(`${id}: choose an integer from ${min} to ${max}.`); return n; }
  function reset() {
    stop();
    const next = new api.Simulation(rule, integer("width",16,1024), integer("height",16,1024), integer("startstate",0,rule.states-1), integer("heading",0,3));
    sim = next; $("canvas").width = sim.width; $("canvas").height = sim.height;
    pixels = context.createImageData(sim.width, sim.height); draw();
  }
  function exportRule() {
    rule.id = $("ruleid").value; rule.name = $("rulename").value;
    $("coutput").value = ""; $("coutput").value = api.exportC(rule);
    return rule;
  }
  function select(values, value, label, onChange) {
    const el = document.createElement("select"); el.setAttribute("aria-label",label);
    for (const item of values) { const option = document.createElement("option"); option.value = item; option.textContent = item; el.append(option); }
    el.value = value; el.onchange = guard(() => onChange(el.value)); return el;
  }
  function load(next) {
    api.validate(next); rule = api.clone(next); stop();
    $("ruleid").value = rule.id; $("rulename").value = rule.name;
    $("states").value = rule.states; $("colors").value = rule.colors;
    $("startstate").value = 0; $("startstate").max = rule.states - 1;
    const body = $("actions"); body.replaceChildren();
    rule.table.forEach((row,s) => row.forEach((a,c) => {
      const tr = document.createElement("tr");
      for (const value of [s,c]) { const td = document.createElement("td"); td.textContent = value; tr.append(td); }
      for (const [key, values] of [["w",Array.from({length:rule.colors},(_,i)=>i)],["t",api.turns],["n",Array.from({length:rule.states},(_,i)=>i)]]) {
        const td = document.createElement("td");
        td.append(select(values,a[key],`State ${s} color ${c} ${key}`, value => { stop(); a[key] = key === "t" ? value : Number(value); exportRule(); reset(); message(); })); tr.append(td);
      }
      const td = document.createElement("td"), check = document.createElement("input"); check.type = "checkbox"; check.checked = a.halt;
      check.setAttribute("aria-label",`State ${s} color ${c} halt`);
      check.onchange = guard(() => { stop(); a.halt = check.checked; exportRule(); reset(); }); td.append(check); tr.append(td); body.append(tr);
    }));
    exportRule(); reset(); message();
  }
  function draw() {
    for (let i = 0; i < sim.cells.length; i++) {
      const rgb = palette[sim.cells[i]]; pixels.data[4*i] = rgb >>> 16; pixels.data[4*i+1] = (rgb >>> 8) & 255; pixels.data[4*i+2] = rgb & 255; pixels.data[4*i+3] = 255;
    }
    context.putImageData(pixels,0,0);
    if ($("marker").checked) { context.fillStyle = "#ffffff"; context.fillRect(sim.x-3,sim.y,7,1); context.fillRect(sim.x,sim.y-3,1,7); }
    $("status").textContent = `${sim.halted ? "HALTED" : running ? "RUNNING" : "PAUSED"} · ${sim.steps.toLocaleString()} instructions\nPosition ${sim.x}, ${sim.y} · state ${sim.state} · heading ${"NESW"[sim.heading]}\nMoves ${sim.moves.toLocaleString()} · writes that changed color ${sim.changes.toLocaleString()}\nNon-background cells ${sim.nonzero.toLocaleString()} · visited ${sim.visited.toLocaleString()}\nInstructions since last color change: ${sim.quiet.toLocaleString()}`;
  }
  function step(count) { stop(); sim.step(count); draw(); }
  function download(text, filename) {
    const url = URL.createObjectURL(new Blob([text], {type:"text/plain;charset=utf-8"}));
    const a = document.createElement("a"); a.href = url; a.download = filename; a.click(); setTimeout(() => URL.revokeObjectURL(url),1000);
  }
  catalog.forEach((r,i) => { const option = document.createElement("option"); option.value = i; option.textContent = r.name; $("preset").append(option); });
  $("load").onclick = guard(() => load(catalog[Number($("preset").value)]));
  $("generate").onclick = guard(() => {
    const next = api.generate(integer("states",1,4),integer("colors",1,6),integer("seed",1,0xffffffff),$("extended").checked);
    load(next);
    if (catalog.some(r => JSON.stringify(r.table) === JSON.stringify(next.table))) message("This table matches a catalogue rule. Try another seed for a different candidate.");
  });
  $("newseed").onclick = guard(() => { $("seed").value = crypto.getRandomValues(new Uint32Array(1))[0] || 1; message("New seed ready. Generate or mutate to use it."); });
  $("mutate").onclick = guard(() => load(api.mutate(exportRule(),integer("seed",1,0xffffffff),$("extended").checked)));
  for (const id of ["ruleid","rulename"]) $(id).oninput = guard(() => { exportRule(); message(); });
  $("reset").onclick = guard(() => { reset(); message(); });
  for (const id of ["width","height","startstate","heading"]) $(id).onchange = guard(reset);
  $("step").onclick = guard(() => step(1)); $("batch").onclick = guard(() => step(1000));
  $("run").onclick = guard(() => { if (sim.halted) { message("HALT reached. Reset to run again."); return; } running = !running; $("run").textContent = running ? "Pause" : "Run"; draw(); });
  $("marker").onchange = guard(draw);
  function selectC() { $("coutput").focus(); $("coutput").select(); message("C selected. Press Ctrl+C to copy."); }
  $("copy").onclick = guard(() => { const text = api.exportC(exportRule()); if (!navigator.clipboard) selectC(); else navigator.clipboard.writeText(text).then(() => message("C copied."), selectC); });
  $("download").onclick = guard(() => download(api.exportC(exportRule()),rule.id+".inc"));
  $("jsonexport").onclick = guard(() => { $("json").value = JSON.stringify(exportRule(),null,2); message("Rule JSON ready below."); });
  $("jsonimport").onclick = guard(() => load(api.validate(JSON.parse($("json").value))));
  function tick() {
    if (running) guard(() => {
      const count = integer("speed",1,100000), end = performance.now()+8; let done=0;
      do { const batch=Math.min(256,count-done); sim.step(batch); done+=batch; } while(done<count && !sim.halted && performance.now()<end);
      if(sim.halted)stop(); draw();
    })();
    requestAnimationFrame(tick);
  }
  guard(() => load(catalog[0]))(); requestAnimationFrame(tick);
})();
