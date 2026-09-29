// Placeholder UI for the plugin shell (P1-6): proves the bridge end to end. The Studio screen
// (P1-9) and the design system (P1-8) replace this directory with the real TypeScript UI.
// Protocol: docs/bridge-spec.md. No domain logic here; the plugin owns all state.

import { getNativeFunction } from "./juce_interop.js";

const PROTOCOL = "flowstate.bridge.v0";
const native = getNativeFunction("bridge");
const $ = (id) => document.getElementById(id);
let session = null;

async function send(command) {
  const reply = JSON.parse(await native(JSON.stringify(command)));
  if (!reply.ok && reply.error) notice(reply.error.message);
  if (reply.session) render(reply.session);
  return reply;
}

function notice(text) {
  $("notice").textContent = text;
}

function render(s) {
  session = s;
  const c = s.context;
  $("ctx-key").textContent = `${c.tonic} ${c.mode.replace("_", " ")}`;
  $("ctx-time").textContent = `${Math.round(c.tempo * 10) / 10} bpm · ${c.meterNumerator}/${c.meterDenominator}`;
  $("ctx-bars").textContent = `${c.bars} bars`;

  const parts = $("parts");
  parts.replaceChildren();
  for (const p of s.clip?.parts ?? []) {
    const li = document.createElement("li");
    li.textContent = `${p.name || p.partId} · ${p.role} · ${p.notes.length} notes`;
    parts.append(li);
  }
  $("empty").hidden = (s.clip?.parts.length ?? 0) > 0;

  const nodes = $("nodes");
  nodes.replaceChildren();
  for (const n of s.nodes) {
    const li = document.createElement("li");
    const b = document.createElement("button");
    b.type = "button";
    b.textContent = `${n.title || n.id} (${n.kind})`;
    b.setAttribute("aria-current", String(n.id === s.currentNodeId));
    b.addEventListener("click", () => send({ type: "selectNode", nodeId: n.id }));
    li.append(b);
    nodes.append(li);
  }
  $("undo").disabled = !s.canUndo;
  $("redo").disabled = !s.canRedo;
  $("drag").disabled = !s.clip;
}

window.__JUCE__.backend.addEventListener("bridge", (json) => {
  const event = JSON.parse(json);
  if (event.type === "session") render(event.session);
  else if (event.type === "transport") {
    const t = event.transport;
    $("transport").textContent = t.playing ? `bar ${t.bar} · beat ${t.beat.toFixed(1)}` : "stopped";
  } else if (event.type === "notice") notice(event.message);
});

$("undo").addEventListener("click", () => send({ type: "undo" }));
$("redo").addEventListener("click", () => send({ type: "redo" }));
$("drag").addEventListener("pointerdown", (e) => {
  e.preventDefault();
  send({ type: "startDrag", nodeId: null, partIds: null, splitDrums: false });
});
$("prompt").addEventListener("submit", (e) => {
  e.preventDefault();
  const prompt = $("prompt-input").value.trim();
  send({ type: "generate", prompt, roles: null, count: 1, capture: null });
});

// Space with nothing focused, and Escape in the prompt, hand focus back to the host (spike P0-7).
document.addEventListener("keydown", (e) => {
  const typing = document.activeElement?.tagName === "INPUT";
  if ((e.code === "Space" && !typing) || (e.key === "Escape" && typing)) {
    e.preventDefault();
    document.activeElement?.blur();
    send({ type: "releaseFocus", reason: e.code === "Space" ? "space" : "escape" });
  }
});
$("prompt-input").addEventListener("blur", () => send({ type: "releaseFocus", reason: "blur" }));

send({ type: "hello", protocol: PROTOCOL });
