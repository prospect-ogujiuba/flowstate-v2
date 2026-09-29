// Flowstate Spike page. No build step, no framework.
// Talks to C++ through JUCE's interop library (served as juce_interop.js from the JUCE checkout).

import { getNativeFunction } from "./juce_interop.js";
import { PianoRoll } from "./pianoroll.js";

const native = {
  getInitialState: getNativeFunction("getInitialState"),
  loadClip: getNativeFunction("loadClip"),
  loadSample: getNativeFunction("loadSample"),
  startDrag: getNativeFunction("startDrag"),
  ping: getNativeFunction("ping"),
  setPreview: getNativeFunction("setPreview"),
  setInternalPlay: getNativeFunction("setInternalPlay"),
  submitText: getNativeFunction("submitText"),
  releaseFocus: getNativeFunction("releaseFocus"),
};

const $ = (id) => document.getElementById(id);
const roll = new PianoRoll($("roll"));
const backend = window.__JUCE__.backend;
let clip = null;
let preview = true;
let internalPlaying = false;

function log(msg) {
  $("log").textContent = msg;
}

function showClip(c) {
  clip = c;
  if (!c) return;
  $("clip-name").textContent = c.name;
  const notes = c.parts.reduce((n, p) => n + p.notes.length, 0);
  $("clip-info").textContent =
    `${c.bars} bars · ${c.meter[0]}/${c.meter[1]} · ${c.tempo} bpm (file) · ${c.parts.length} parts · ${notes} notes`;
  roll.setClip(c);
  renderPartHandles(c);
}

function setPreviewUi(on) {
  preview = on;
  const b = $("preview");
  b.setAttribute("aria-pressed", String(on));
  b.textContent = `Preview sound: ${on ? "on" : "off"}`;
}

// ---- status from C++ (~30 Hz, emitted by a juce::Timer on the message thread) ----------------
backend.addEventListener("status", (s) => {
  const t = $("st-transport");
  t.textContent = s.host ? (s.playing ? "playing" : "stopped") : (s.internal ? "internal ▶" : "no host transport");
  t.classList.toggle("on", s.playing);
  $("st-bpm").textContent = s.bpm.toFixed(2);
  $("st-ppq").textContent = s.ppq.toFixed(3);
  $("st-sig").textContent = `${s.sig[0]}/${s.sig[1]}`;
  const beatsPerBar = s.sig[0] * 4 / s.sig[1];
  if (s.ppq >= 0) {
    const bar = Math.floor(s.ppq / beatsPerBar) + 1;
    const beat = Math.floor((s.ppq % beatsPerBar) / (4 / s.sig[1])) + 1;
    $("st-pos").textContent = `${bar}.${beat}`;
  } else {
    $("st-pos").textContent = "pre-roll";
  }
  $("st-loop").textContent = s.looping ? `loop ${s.loopStart.toFixed(2)}–${s.loopEnd.toFixed(2)}` : "loop off";
  $("st-active").textContent = s.active;
  $("st-jumps").textContent = s.jumps;
  $("internal-play").hidden = s.host;
  roll.setPlayhead(s.ppq, s.playing);
});

backend.addEventListener("clip", (c) => showClip(c));
backend.addEventListener("error", (e) => log(`error: ${e.message}`));

// ---- controls -------------------------------------------------------------------------------
// Buttons must never keep focus: a focused <button> turns Space into a click instead of letting
// it reach the DAW. preventDefault on mousedown stops the focus change; the click still fires.
for (const b of document.querySelectorAll("button")) {
  b.addEventListener("mousedown", (e) => e.preventDefault());
}

$("load-clip").addEventListener("click", async () => {
  log("choosing clip…");
  const r = await native.loadClip();
  if (r.cancelled) log("load cancelled");
  else if (!r.ok) log(`load failed: ${r.error}`);
  else log(`loaded ${r.clip.name}`);
});

$("load-sample").addEventListener("click", async () => {
  const r = await native.loadSample();
  log(r.ok ? "loaded bundled sample" : `sample failed: ${r.error}`);
});

$("preview").addEventListener("click", async () => {
  const r = await native.setPreview(!preview);
  setPreviewUi(r.preview);
});

$("internal-play").addEventListener("click", async () => {
  internalPlaying = !internalPlaying;
  await native.setInternalPlay(internalPlaying);
  $("internal-play").textContent = internalPlaying ? "Stop (internal)" : "Play (internal)";
});

$("text-form").addEventListener("submit", async (e) => {
  e.preventDefault();
  const text = $("text-input").value;
  const r = await native.submitText(text);
  log(`C++ received ${r.length} chars: "${r.text}"`);
});

// ---- bridge latency: sequential JS -> C++ -> JS round trips ----------------------------------
$("ping").addEventListener("click", async () => {
  const N = 50;
  const samples = [];
  await native.ping(0); // warm-up
  const t0 = performance.now();
  for (let i = 0; i < N; i++) {
    const s = performance.now();
    await native.ping(s);
    samples.push(performance.now() - s);
  }
  const total = performance.now() - t0;
  samples.sort((a, b) => a - b);
  const median = samples[Math.floor(N / 2)];
  const p95 = samples[Math.floor(N * 0.95)];
  // performance.now() is coarsened in some WebViews; the mean over N is the robust number.
  $("ping-result").textContent =
    `mean ${(total / N).toFixed(2)} ms · median ${median.toFixed(2)} · p95 ${p95.toFixed(2)} · max ${samples[N - 1].toFixed(2)}`;
  log(`bridge round trip x${N}: ${(total / N).toFixed(2)} ms mean (pass < 5 ms)`);
});

// ---- drag-out ---------------------------------------------------------------------------------
// The native drag must start while the mouse button is still down, so it is requested on
// pointerdown. C++ writes a temp .mid and calls performExternalDragDropOfFiles.
// "Drag all" writes one MIDI track per part; each part handle drags just that part.
function attachDrag(el, partIndex) {
  el.addEventListener("pointerdown", async (e) => {
    if (e.button !== 0) return;
    e.preventDefault(); // no text selection / WebView-internal drag
    el.classList.add("active");
    try {
      const r = await native.startDrag(partIndex);
      log(r.ok ? `drag started: ${r.path}` : `drag failed: ${r.error}`);
    } finally {
      el.classList.remove("active");
    }
  });
  el.addEventListener("dragstart", (e) => e.preventDefault());
}

attachDrag($("drag-handle"), -1);

function renderPartHandles(c) {
  const box = $("drag-handles");
  box.querySelectorAll(".part-handle").forEach((n) => n.remove());
  (c?.parts ?? []).forEach((p, i) => {
    if (!p.notes.length) return;
    const el = document.createElement("div");
    el.className = "drag-handle part-handle";
    el.setAttribute("role", "button");
    el.setAttribute("draggable", "false");
    const label = p.name || p.role || p.id;
    el.setAttribute("aria-label", `Drag ${label} to a track`);
    el.textContent = `⠿ ${label}`;
    attachDrag(el, i);
    box.appendChild(el);
  });
}

// ---- keyboard: typing vs. DAW shortcuts --------------------------------------------------------
// Rules (see README "Keyboard"):
//  * While the text field has focus, it consumes keys normally, so the WebView reports them as
//    handled and the host does not act on them.
//  * With no editable element focused, the page never calls preventDefault and never scrolls, so
//    the WebView reports the key as unhandled (WKWebView passes it up the responder chain).
//    It also asks C++ to hand keyboard focus back to the host window, which is what makes
//    Space work on WebView2, where unhandled keys are not forwarded.
//  * Esc in the text field blurs it and hands focus back.
const isEditable = (el) =>
  !!el && (el.tagName === "INPUT" || el.tagName === "TEXTAREA" || el.isContentEditable);

document.addEventListener("keydown", (e) => {
  if (isEditable(document.activeElement)) {
    if (e.key === "Escape") {
      document.activeElement.blur();
      native.releaseFocus("escape");
    }
    return;
  }
  if (e.code === "Space" || e.key === " ") native.releaseFocus("space");
});

$("text-input").addEventListener("blur", () => native.releaseFocus("blur"));

// ---- start --------------------------------------------------------------------------------------
(async () => {
  try {
    const s = await native.getInitialState();
    $("variant").textContent = s.variant;
    $("preview").hidden = s.variant !== "instrument";
    setPreviewUi(s.preview);
    showClip(s.clip);
    if (s.error) log(s.error);
    else log(`ready · JUCE ${s.juceVersion} · ${s.backend}`);
  } catch (err) {
    log(`init failed: ${err}`);
  }
})();
