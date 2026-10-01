// A self-contained listening page for a rendered pack: per prompt, two looping options that switch in sync,
// the score fields from scoresheet.csv, autosave in the browser, and a CSV export in the scoresheet's format.
// It works opened straight from disk (file://); the data is embedded, the audio sits next to it.

export interface PlayerEntry {
  folder: string;
  promptId: string;
  prompt: string;
  fields: Record<string, string>;
  /** The sound template's family, shown so the listener knows what kit to expect. */
  sound: string;
}

const esc = (s: string) => s.replace(/[&<>"']/g, (c) => `&#${c.charCodeAt(0)};`);

export function playerHtml(packName: string, entries: PlayerEntry[]): string {
  const data = JSON.stringify(entries).replace(/</g, "\\u003c");
  return `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Flowstate listening: ${esc(packName)}</title>
<style>
:root { --bg: #111317; --panel: #1a1d23; --line: #2c313a; --text: #e8eaee; --muted: #9aa1ad; --accent: #7cc4ff; --on: #23415a; }
* { box-sizing: border-box; }
body { margin: 0; background: var(--bg); color: var(--text); font: 15px/1.45 system-ui, sans-serif; }
main { max-width: 760px; margin: 0 auto; padding: 20px 16px 48px; }
h1 { font-size: 18px; margin: 0 0 4px; }
.sub { color: var(--muted); margin: 0 0 16px; }
.nav { display: flex; gap: 8px; align-items: center; margin-bottom: 12px; flex-wrap: wrap; }
.panel { background: var(--panel); border: 1px solid var(--line); border-radius: 10px; padding: 16px; }
.prompt { font-size: 16px; margin: 0 0 8px; }
.meta { color: var(--muted); font-size: 13px; margin: 0 0 14px; }
button, select, input, textarea { font: inherit; color: var(--text); background: #222730; border: 1px solid var(--line); border-radius: 8px; }
button { padding: 8px 14px; cursor: pointer; }
button:focus-visible, select:focus-visible, input:focus-visible, textarea:focus-visible { outline: 2px solid var(--accent); outline-offset: 2px; }
.options { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; margin-bottom: 10px; }
.options button { padding: 18px; font-size: 16px; }
.options button[aria-pressed="true"] { background: var(--on); border-color: var(--accent); }
.transport { display: flex; gap: 8px; align-items: center; margin-bottom: 16px; }
.time { color: var(--muted); font-variant-numeric: tabular-nums; }
.grid { display: grid; grid-template-columns: auto 1fr 1fr; gap: 8px 12px; align-items: center; }
.grid .h { color: var(--muted); font-size: 13px; }
select { padding: 6px 8px; width: 100%; }
textarea { width: 100%; padding: 8px; min-height: 56px; margin-top: 10px; }
.row { display: flex; gap: 8px; margin-top: 14px; flex-wrap: wrap; align-items: center; }
.done { color: var(--muted); font-size: 13px; }
.keys { color: var(--muted); font-size: 13px; margin-top: 16px; }
kbd { border: 1px solid var(--line); border-radius: 4px; padding: 0 5px; font-size: 12px; }
@media (max-width: 520px) { .options { grid-template-columns: 1fr; } }
</style>
</head>
<body>
<main>
  <h1>Listening: ${esc(packName)}</h1>
  <p class="sub">Two options per prompt, from different generators, in a random order. Same sounds for both; judge the notes.</p>
  <div class="nav">
    <button id="prev" aria-label="Previous prompt">◀ Prev</button>
    <label for="jump" class="done">Prompt</label>
    <select id="jump" style="width:auto"></select>
    <button id="next" aria-label="Next prompt">Next ▶</button>
    <span class="done" id="progress"></span>
  </div>
  <section class="panel" aria-live="polite">
    <p class="prompt" id="prompt"></p>
    <p class="meta" id="meta"></p>
    <div class="options">
      <button id="opt1" aria-pressed="false">Option 1</button>
      <button id="opt2" aria-pressed="false">Option 2</button>
    </div>
    <div class="transport">
      <button id="play" aria-label="Play or pause">Play</button>
      <button id="restart" aria-label="Back to the start">⟲ Start</button>
      <span class="time" id="time">0:00</span>
    </div>
    <div class="grid">
      <span></span><span class="h">Option 1</span><span class="h">Option 2</span>
      <label for="m1">Musicality (1-5)</label><select id="m1"></select><select id="m2" aria-label="Musicality, option 2"></select>
      <label for="f1">Fits the prompt (1-5)</label><select id="f1"></select><select id="f2" aria-label="Fits the prompt, option 2"></select>
    </div>
    <div class="row">
      <label for="pref">Preferred</label>
      <select id="pref" style="width:auto"><option value="">-</option><option value="1">Option 1</option><option value="2">Option 2</option><option value="tie">Tie</option></select>
    </div>
    <label for="notes" class="done" style="display:block;margin-top:12px">Notes (optional)</label>
    <textarea id="notes"></textarea>
  </section>
  <div class="row">
    <button id="export">Save scoresheet.csv</button>
    <span class="done">Scores are kept in this browser as you go. Save the CSV when done and put it in the pack folder.</span>
  </div>
  <p class="keys"><kbd>1</kbd> <kbd>2</kbd> switch option (keeps the position) · <kbd>Enter</kbd> play/pause · <kbd>R</kbd> restart · <kbd>N</kbd> <kbd>P</kbd> next/previous prompt</p>
</main>
<audio id="a1" loop preload="auto"></audio>
<audio id="a2" loop preload="auto"></audio>
<script>
const ENTRIES = ${data};
const STORE = "flowstate-listening-${esc(packName)}";
const $ = (id) => document.getElementById(id);
let scores = {};
try { scores = JSON.parse(localStorage.getItem(STORE) || "{}"); } catch (e) { scores = {}; }
const save = () => { try { localStorage.setItem(STORE, JSON.stringify(scores)); } catch (e) {} };
let index = 0, active = 1;
const audio = { 1: $("a1"), 2: $("a2") };

for (const id of ["m1", "m2", "f1", "f2"]) {
  $(id).innerHTML = '<option value="">-</option>' + [1, 2, 3, 4, 5].map((n) => '<option>' + n + '</option>').join("");
}
ENTRIES.forEach((e, i) => { const o = document.createElement("option"); o.value = i; o.textContent = (i + 1) + ". " + e.promptId; $("jump").appendChild(o); });

function entryScore(e) { return scores[e.promptId] || (scores[e.promptId] = { preferred: "", m1: "", m2: "", f1: "", f2: "", notes: "" }); }
function progress() {
  const done = ENTRIES.filter((e) => scores[e.promptId] && scores[e.promptId].preferred).length;
  $("progress").textContent = done + " of " + ENTRIES.length + " scored";
}
function show(i) {
  index = (i + ENTRIES.length) % ENTRIES.length;
  const e = ENTRIES[index];
  for (const n of [1, 2]) { audio[n].pause(); audio[n].src = e.folder + "/option-" + n + ".mp3"; }
  $("prompt").textContent = e.prompt;
  const f = e.fields;
  $("meta").textContent = [f.tempo, f.meter, f.key, f.length, f.style].filter(Boolean).join(" · ") + " · sounds: " + e.sound;
  $("jump").value = String(index);
  const s = entryScore(e);
  $("m1").value = s.m1; $("m2").value = s.m2; $("f1").value = s.f1; $("f2").value = s.f2; $("pref").value = s.preferred; $("notes").value = s.notes;
  select(1, false);
  $("play").textContent = "Play";
  progress();
}
function select(n, keepPlaying = true) {
  const from = audio[active], to = audio[n];
  const playing = keepPlaying && !from.paused;
  if (n !== active) { try { to.currentTime = from.currentTime; } catch (e) {} from.pause(); }
  active = n;
  $("opt1").setAttribute("aria-pressed", String(n === 1));
  $("opt2").setAttribute("aria-pressed", String(n === 2));
  if (playing) to.play();
}
function toggle() {
  const a = audio[active];
  if (a.paused) { a.play(); $("play").textContent = "Pause"; } else { a.pause(); $("play").textContent = "Play"; }
}
function bind(id, key) {
  $(id).addEventListener("change", () => { entryScore(ENTRIES[index])[key] = $(id).value; save(); progress(); });
}
bind("m1", "m1"); bind("m2", "m2"); bind("f1", "f1"); bind("f2", "f2"); bind("pref", "preferred");
$("notes").addEventListener("input", () => { entryScore(ENTRIES[index]).notes = $("notes").value; save(); });
$("opt1").onclick = () => select(1); $("opt2").onclick = () => select(2);
$("play").onclick = toggle;
$("restart").onclick = () => { for (const n of [1, 2]) audio[n].currentTime = 0; };
$("prev").onclick = () => show(index - 1); $("next").onclick = () => show(index + 1);
$("jump").onchange = () => show(Number($("jump").value));
setInterval(() => { const t = audio[active].currentTime || 0; $("time").textContent = Math.floor(t / 60) + ":" + String(Math.floor(t % 60)).padStart(2, "0"); }, 200);
document.addEventListener("keydown", (ev) => {
  if (ev.target.tagName === "TEXTAREA" || ev.target.tagName === "SELECT") return;
  if (ev.key === "1") select(1); else if (ev.key === "2") select(2);
  else if (ev.key === "Enter" && ev.target === document.body) { ev.preventDefault(); toggle(); }
  else if (ev.key === "r" || ev.key === "R") $("restart").click();
  else if (ev.key === "n" || ev.key === "N") show(index + 1);
  else if (ev.key === "p" || ev.key === "P") show(index - 1);
});
$("export").onclick = () => {
  const q = (s) => /[",\\n]/.test(s) ? '"' + s.replace(/"/g, '""') + '"' : s;
  const rows = ["prompt_id,preferred,musicality_1,musicality_2,fits_prompt_1,fits_prompt_2,notes"];
  for (const e of ENTRIES) { const s = entryScore(e); rows.push([e.promptId, s.preferred, s.m1, s.m2, s.f1, s.f2, q(s.notes || "")].join(",")); }
  const a = document.createElement("a");
  a.href = URL.createObjectURL(new Blob([rows.join("\\n") + "\\n"], { type: "text/csv" }));
  a.download = "scoresheet.csv";
  a.click();
};
show(0);
</script>
</body>
</html>
`;
}
