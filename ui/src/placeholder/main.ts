// Placeholder UI for the plugin shell (P1-6): proves the bridge end to end. The Studio screen
// (P1-9) replaces it. Protocol: docs/bridge-spec.md. No domain logic here; the plugin owns all state.

import type { Command, Session } from "@flowstate/schema";
import { onEvent, PROTOCOL, send as sendRaw } from "../host/bridge.ts";
import { installHostKeys } from "../host/keys.ts";
import "./style.css";

const $ = <T extends HTMLElement = HTMLElement>(id: string) => document.getElementById(id) as T;

async function send(command: Command) {
  const reply = await sendRaw(command);
  if (!reply.ok && reply.error) notice(reply.error.message);
  if (reply.session) render(reply.session);
  return reply;
}

function notice(text: string) {
  $("notice").textContent = text;
}

function render(s: Session) {
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
  $<HTMLButtonElement>("undo").disabled = !s.canUndo;
  $<HTMLButtonElement>("redo").disabled = !s.canRedo;
  $<HTMLButtonElement>("drag").disabled = !s.clip;
}

onEvent((event) => {
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
  void send({ type: "startDrag", nodeId: null, partIds: null, splitDrums: false });
});
$("prompt").addEventListener("submit", (e) => {
  e.preventDefault();
  const prompt = $<HTMLInputElement>("prompt-input").value.trim();
  void send({ type: "generate", prompt, roles: null, count: 1, capture: null });
});

// Space with nothing typed into, and Escape in the prompt, hand focus back to the host (spike P0-7).
installHostKeys((reason) => void send({ type: "releaseFocus", reason }));
$("prompt-input").addEventListener("blur", () => void send({ type: "releaseFocus", reason: "blur" }));

void send({ type: "hello", protocol: PROTOCOL });
