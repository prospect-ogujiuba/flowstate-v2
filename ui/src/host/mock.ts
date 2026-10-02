// A stand-in plugin for the browser: the Studio's dev server and its Playwright tests run against
// it (`index.html?mock`). It answers every bridge command the way docs/bridge-spec.md says the
// plugin does, streams canned parts for generations, and records what the UI sent. Its notes are
// hand-written display data, not theory: nothing here is realized or ever reaches the plugin.

import type { Command, PluginEvent, Reply, Session } from "@flowstate/schema";
import type { Bridge } from "./types.ts";

type S = Session;
type Node = S["nodes"][number];
type Clip = NonNullable<S["clip"]>;
type ClipPart = Clip["parts"][number];
type Gap = S["unavailable"][number];
type Entry = NonNullable<Reply["catalog"]>["entries"][number];
type Transport = Extract<PluginEvent, { type: "transport" }>["transport"];
type ErrorCode = NonNullable<Reply["error"]>["code"];

/** What this build can't do yet, with the plugin's reasons (plugin/src/session/Controller.cpp). */
export const PLUGIN_GAPS: Gap[] = [
  { feature: "edit", reason: "Changing an idea by prompt isn't built in the agent service yet. Generate a new idea instead." },
  { feature: "vary", reason: "Vary needs the agent service to write part variations, which isn't built yet." },
  { feature: "addPart", reason: "Adding a part needs the agent service to plan single parts, which isn't built yet." },
  { feature: "reroll", reason: "Re-roll needs the realizer to make seeded choices (voicing, rhythm), which isn't built yet." },
  { feature: "tweak", reason: "Local transforms aren't in core yet." },
  { feature: "editNotes", reason: "Note edits aren't in core yet." },
  { feature: "capture", reason: "\"Use what I just played\" needs the planner to plan around a reference, which isn't built yet." },
  { feature: "density", reason: "The density knob doesn't change playback until core has the transform." },
  { feature: "apiKey", reason: "Key storage isn't available in this build yet (P1-12)." },
];

export type MockOptions = {
  /** Start with one finished idea in the session. */
  idea: boolean;
  /** "plugin": today's gaps; "none": everything is built. */
  gaps: "plugin" | "none";
  byok: boolean;
  captureBars: number;
  /** Milliseconds between streamed events. */
  delay: number;
  /** The host transport is running. */
  playing: boolean;
};

export function mockOptions(params: URLSearchParams): MockOptions {
  return {
    idea: params.get("mock") === "idea",
    gaps: params.get("gaps") === "none" ? "none" : "plugin",
    byok: params.get("byok") !== "0",
    captureBars: Number(params.get("capture") ?? 8),
    delay: Number(params.get("delay") ?? 350),
    playing: params.get("playing") === "1",
  };
}

// ---- Canned clips: ticks at 960 per quarter, 4/4 -------------------------------------------------

const PPQ = 960;
const BAR = PPQ * 4;
const note = (beat: number, beats: number, pitch: number, vel = 96) => ({ tick: Math.round(beat * PPQ), dur: Math.max(1, Math.round(beats * PPQ)), pitch, vel });

function chordsPart(v: number, bars: number): ClipPart {
  const stacks = [[57, 60, 64], [53, 57, 60], [55, 59, 62], [52, 55, 59]];
  const notes = [];
  for (let b = 0; b < bars; b++) {
    const stack = stacks[(b + v) % stacks.length]!;
    const hits = v % 2 === 0 ? [[0, 4]] : [[0, 1.5], [1.5, 2.5]];
    for (const [at, len] of hits) for (const p of stack) notes.push(note(b * 4 + at!, len!, p, 84));
  }
  return { partId: "p-chords", role: "chords", name: "Chords", channel: 1, notes, voices: null };
}

function bassPart(v: number, bars: number): ClipPart {
  const roots = [45, 41, 43, 40];
  const notes = [];
  for (let b = 0; b < bars; b++) {
    const r = roots[(b + v) % roots.length]!;
    notes.push(note(b * 4, 1.5, r), note(b * 4 + 1.5, 0.5, r, 70), note(b * 4 + 2, 1, r), note(b * 4 + 3, 1, r + 7, 80));
  }
  return { partId: "p-bass", role: "bass", name: "Bass", channel: 2, notes, voices: null };
}

function melodyPart(v: number, bars: number): ClipPart {
  const line = [[0.5, 0.5, 76], [1, 0.5, 74], [1.5, 1.5, 72], [3, 1, 69]] as const;
  const notes = [];
  for (let b = 0; b < bars; b++) for (const [at, len, p] of line) notes.push(note(b * 4 + at, len, p + ((b + v) % 3) * 2, 100));
  return { partId: "p-melody", role: "melody", name: "Melody", channel: 3, notes, voices: null };
}

function drumsPart(v: number, bars: number): ClipPart {
  const notes = [];
  for (let b = 0; b < bars; b++) {
    for (const k of v % 2 ? [0, 2.5] : [0, 1.75, 2.5]) notes.push(note(b * 4 + k, 0.25, 36, 110));
    for (const s of [1, 3]) notes.push(note(b * 4 + s, 0.25, 38, 104));
    for (let h = 0; h < 8; h++) notes.push(note(b * 4 + h / 2, 0.25, 42, 70));
  }
  return {
    partId: "p-drums", role: "drums", name: "Drums", channel: 10, notes,
    voices: [
      { voice: "kick", pitch: 36, sublane: "kick" },
      { voice: "snare", pitch: 38, sublane: "snare" },
      { voice: "closed_hat", pitch: 42, sublane: "hats" },
    ],
  };
}

const PART_MAKERS = [chordsPart, drumsPart, bassPart, melodyPart];

const LIBRARY: Entry[] = [
  ["godflow/rnb-07", "Velvet sevenths", ["chords"], ["rnb", "neo-soul"], "A", "minor", 84],
  ["godflow/pop-02", "Bright pop stack", ["chords"], ["pop"], "C", "major", 110],
  ["godflow/hiphop-bass-02", "Dusty walking bass", ["bass"], ["hip-hop"], null, null, 90],
].map(([id, title, roles, genres, tonic, mode, tempo]) => ({
  id: `lib:${id as string}`, origin: "library", title: title as string, roles: roles as Entry["roles"], genres: genres as string[], tags: [], feel: null,
  tonic: tonic as Entry["tonic"], mode: mode as Entry["mode"], keyNote: tonic ? "detected" : "not reliable from this clip",
  tempo: tempo as number, tempoMin: (tempo as number) - 10, tempoMax: (tempo as number) + 10, meterNumerator: 4, meterDenominator: 4, bars: 4,
  energy: 0.5, density: 0.5, complexity: 0.4, groove: "straight",
  credit: {
    packId: "godflow", packTitle: "GodFlow", producer: "GodFlow", text: "MIDI by GodFlow (flowknows) for Flowstate.",
    licenseId: "flowstate-bundled", allowsExport: true, allowsStyleExamples: true,
  },
  nodeId: null, prompt: null, kind: null, createdAtMs: null,
}));

// ---- The mock plugin --------------------------------------------------------------------------

type Stored = { node: Node; clip: Clip; key: [S["context"]["tonic"], S["context"]["mode"]] };

export class MockPlugin implements Bridge {
  readonly sent: Command[] = [];
  delay: number;
  private listeners: ((e: PluginEvent) => void)[] = [];
  private nodes = new Map<string, Stored>();
  private history: string[] = [];
  private redoStack: string[] = [];
  private s: S;
  private seq = 1;
  private timers: ReturnType<typeof setTimeout>[] = [];
  private host: Transport;
  private failNext = new Map<string, { code: ErrorCode; message: string }>();

  constructor(private opts: MockOptions) {
    this.delay = opts.delay;
    this.host = { playing: opts.playing, recording: false, positionPpq: 0, bar: 1, beat: 1, tempo: 92, meterNumerator: 4, meterDenominator: 4, loop: null };
    this.s = {
      protocol: "flowstate.bridge.v0",
      instanceId: "mock",
      override: { tonic: null, mode: null, tempo: null, meterNumerator: null, meterDenominator: null, bars: null, swing: null },
      context: { tonic: "C", mode: "major", keyFrom: "default", tempo: 92, meterNumerator: 4, meterDenominator: 4, timeFrom: "host", bars: 4 },
      captureBars: opts.captureBars,
      nodes: [], currentNodeId: null, canUndo: false, canRedo: false, thread: [], parts: [], clip: null,
      audition: { nodeId: null, loop: null, freeRun: false },
      midiOut: { role: null, channel: null },
      settings: { provider: null, byokEnabled: opts.byok, hasKey: false, previewSynth: true, usage: null, buildId: "mock-dev" },
      generations: [], preview: null,
      unavailable: opts.gaps === "none" ? [] : PLUGIN_GAPS,
    };
    if (opts.idea) {
      const id = this.addNode({ kind: "initial", prompt: "dusty soul loop, lazy swing", title: "Dusty soul loop", parts: this.allParts(0), key: ["A", "minor"] });
      this.s.thread.push(
        { id: "t-0", role: "user", text: "dusty soul loop, lazy swing", nodeId: null, createdAtMs: Date.now() - 60_000 },
        { id: "t-0a", role: "assistant", text: "Dusty soul loop", nodeId: id, createdAtMs: Date.now() - 55_000 },
      );
      this.select(id);
    }
    this.refresh();
    setInterval(() => this.tick(), 1000 / 30);
  }

  // ---- Test hooks ----
  /** Makes the next command of this type fail with this error. */
  fail(type: string, code: ErrorCode, message: string) { this.failNext.set(type, { code, message }); }
  emit(event: PluginEvent) { for (const fn of this.listeners) fn(event); }
  setHost(patch: Partial<Transport>) { this.host = { ...this.host, ...patch }; this.refresh(); this.publish(); }
  session(): S { return structuredClone(this.s); }

  onEvent(fn: (e: PluginEvent) => void) { this.listeners.push(fn); }

  async send(command: Command): Promise<Reply> {
    this.sent.push(structuredClone(command));
    const injected = this.failNext.get(command.type);
    if (injected) {
      this.failNext.delete(command.type);
      return this.error(injected.code, injected.message);
    }
    return this.handle(command);
  }

  private gap(feature: Gap["feature"]) { return this.s.unavailable.find((g) => g.feature === feature)?.reason ?? null; }
  private error(code: ErrorCode, message: string): Reply { return { ok: false, error: { code, message }, requestId: null, session: null, catalog: null }; }
  private ok(changed = true, requestId: string | null = null): Reply {
    if (changed) { this.refresh(); this.publish(); }
    return { ok: true, error: null, requestId, session: changed ? this.session() : null, catalog: null };
  }
  private publish() { const session = this.session(); this.later(() => this.emit({ type: "session", session }), 0); }
  private later(fn: () => void, ms: number) { this.timers.push(setTimeout(fn, ms)); }

  private handle(c: Command): Reply {
    const gapReply = (f: Gap["feature"]) => { const r = this.gap(f); return r ? this.error("unavailable", r) : null; };
    switch (c.type) {
      case "hello": return { ok: true, error: null, requestId: null, session: this.session(), catalog: null };
      case "generate": {
        if (this.s.generations.length) return this.error("busy", "A generation is already running.");
        if (c.capture) { const g = gapReply("capture"); if (g) return g; }
        return this.model("initial", c.prompt, c.count, null);
      }
      case "edit": return gapReply("edit") ?? this.model("edit", c.prompt, 1, c.partIds);
      case "vary": return gapReply("vary") ?? this.model("vary", null, 1, [c.partId]);
      case "addPart": return gapReply("addPart") ?? this.model("edit", c.prompt ?? `Add ${c.role}`, 1, null);
      case "reroll": return gapReply("reroll") ?? this.derive("regenerate", [c.partId]);
      case "tweak": return gapReply("tweak") ?? this.derive("tweak", c.partId ? [c.partId] : null);
      case "editNotes": return gapReply("editNotes") ?? this.derive("touch", [c.partId]);
      case "removePart": {
        const cur = this.current();
        if (!cur || !cur.clip.parts.some((p) => p.partId === c.partId)) return this.error("unknown_part", `No part ${c.partId} in the current idea.`);
        const id = this.addNode({ kind: "tweak", prompt: null, title: cur.node.title, parts: cur.clip.parts.filter((p) => p.partId !== c.partId), key: cur.key, partIds: [c.partId], parentId: cur.node.id });
        this.select(id);
        return this.ok();
      }
      case "cancel": return this.cancel(c.requestId);
      case "selectNode": {
        if (!this.nodes.has(c.nodeId)) return this.error("unknown_node", `No node ${c.nodeId}.`);
        this.select(c.nodeId);
        return this.ok();
      }
      case "undo": {
        if (this.history.length < 2) return this.error("bad_request", "Nothing to undo.");
        this.redoStack.push(this.history.pop()!);
        this.s.currentNodeId = this.history.at(-1)!;
        return this.ok();
      }
      case "redo": {
        const id = this.redoStack.pop();
        if (!id) return this.error("bad_request", "Nothing to redo.");
        this.history.push(id);
        this.s.currentNodeId = id;
        return this.ok();
      }
      case "rateNode": {
        const n = this.nodes.get(c.nodeId);
        if (!n) return this.error("unknown_node", `No node ${c.nodeId}.`);
        n.node.rating = c.rating;
        return this.ok();
      }
      case "setPartState": {
        const i = this.s.parts.findIndex((p) => p.partId === c.state.partId);
        if (i < 0) return this.error("unknown_part", `No part ${c.state.partId} in the current idea.`);
        this.s.parts[i] = c.state;
        return this.ok();
      }
      case "setContextOverride": this.s.override = c.override; return this.ok();
      case "setAudition": {
        if (c.audition.nodeId && !this.nodes.has(c.audition.nodeId)) return this.error("unknown_node", `No node ${c.audition.nodeId}.`);
        this.s.audition = c.audition;
        return this.ok();
      }
      case "setMidiOut": this.s.midiOut = c.midiOut; return this.ok();
      case "setPreviewSynth": this.s.settings.previewSynth = c.enabled; return this.ok();
      case "setProvider": this.s.settings.provider = c.provider; this.s.settings.hasKey = false; return this.ok();
      case "setApiKey": {
        const g = gapReply("apiKey");
        if (g) return g;
        this.s.settings.hasKey = c.key !== null;
        return this.ok();
      }
      case "startDrag": case "exportMidi": {
        const n = c.nodeId ? this.nodes.get(c.nodeId) : this.current();
        if (!n) return c.nodeId ? this.error("unknown_node", `No node ${c.nodeId}.`) : this.error("bad_request", "There is no idea to drag yet.");
        return this.ok(false);
      }
      case "releaseFocus": return this.ok(false);
      case "searchCatalog": {
        const q = c.query;
        const words = q.text.toLowerCase().split(/\s+/).filter(Boolean);
        const all = [...LIBRARY, ...this.aiEntries()].filter((e) =>
          (!q.origins || q.origins.includes(e.origin)) &&
          (!q.roles || e.roles.some((r) => q.roles!.includes(r))) &&
          words.every((w) => [e.title, ...e.genres, e.prompt ?? ""].join(" ").toLowerCase().includes(w)));
        return { ok: true, error: null, requestId: null, session: null, catalog: { total: all.length, entries: all.slice(q.offset, q.offset + q.limit) } };
      }
      case "previewEntry": this.s.preview = c.entryId; return this.ok();
      case "useEntry": {
        if (c.entryId.startsWith("node:")) {
          const id = c.entryId.slice(5);
          if (!this.nodes.has(id)) return this.error("unknown_node", `No node ${id}.`);
          this.select(id);
          return this.ok();
        }
        const e = LIBRARY.find((x) => x.id === c.entryId);
        if (!e) return this.error("bad_request", `No catalog entry ${c.entryId}.`);
        const parts = e.roles[0] === "bass" ? [bassPart(1, 4)] : [chordsPart(1, 4)];
        const id = this.addNode({ kind: "library", prompt: null, title: e.title, parts, key: [e.tonic ?? "C", e.mode ?? "major"], entryId: e.id });
        this.select(id);
        return this.ok();
      }
      case "dragEntry": return this.ok(false);
    }
  }

  private aiEntries(): Entry[] {
    return [...this.nodes.values()].filter((n) => n.node.kind !== "library" && !n.node.entryId).map(({ node, clip, key }) => ({
      id: `node:${node.id}`, origin: "ai", title: node.title, roles: clip.parts.map((p) => p.role), genres: [], tags: [], feel: null,
      tonic: key[0], mode: key[1], keyNote: null, tempo: 92, tempoMin: 92, tempoMax: 92, meterNumerator: 4, meterDenominator: 4, bars: clip.bars,
      energy: null, density: null, complexity: null, groove: null, credit: null, nodeId: node.id, prompt: node.prompt, kind: node.kind, createdAtMs: node.createdAtMs,
    }));
  }

  private allParts(v: number) { return PART_MAKERS.map((make) => make(v, this.s.context.bars)); }
  private current() { return this.s.currentNodeId ? this.nodes.get(this.s.currentNodeId) : undefined; }

  private addNode(n: { kind: Node["kind"]; prompt: string | null; title: string; parts: ClipPart[]; key: Stored["key"]; partIds?: string[] | null; parentId?: string | null; entryId?: string }) {
    const id = `n${this.seq++}`;
    const node: Node = {
      id, parentId: n.parentId !== undefined ? n.parentId : this.s.currentNodeId, kind: n.kind, prompt: n.prompt, partIds: n.partIds ?? null,
      title: n.title, createdAtMs: Date.now(), rating: null, entryId: n.entryId ?? null,
    };
    const bars = Math.max(1, ...n.parts.map((p) => Math.ceil(Math.max(0, ...p.notes.map((x) => x.tick + x.dur)) / BAR)));
    this.nodes.set(id, { node, clip: { ppq: PPQ, bars, ticksPerBar: BAR, parts: n.parts }, key: n.key });
    return id;
  }

  private select(id: string) {
    this.s.currentNodeId = id;
    this.history.push(id);
    this.redoStack = [];
  }

  /** A local command (reroll, tweak, touch): a new node at once. */
  private derive(kind: Node["kind"], partIds: string[] | null): Reply {
    const cur = this.current();
    if (!cur) return this.error("bad_request", "There is no idea yet.");
    const v = this.seq;
    const parts = cur.clip.parts.map((p) => (partIds === null || partIds.includes(p.partId) ? PART_MAKERS[["chords", "drums", "bass", "melody"].indexOf(p.role)]?.(v, cur.clip.bars) ?? p : p));
    const id = this.addNode({ kind, prompt: null, title: cur.node.title, parts, key: cur.key, partIds });
    this.select(id);
    return this.ok();
  }

  private requests = new Map<string, { cancelled: boolean }>();

  /** A model request: streams parts into a node per variation, as the plugin's client does. */
  private model(kind: Node["kind"], prompt: string | null, count: number, partIds: string[] | null): Reply {
    const requestId = `r${this.seq++}`;
    const req = { cancelled: false };
    this.requests.set(requestId, req);
    const parent = this.s.currentNodeId;
    const locked = this.s.parts.some((p) => p.locked);
    this.s.generations = [{ requestId, kind, stage: "planning", partsDone: [] }];
    if (prompt !== null) this.s.thread.push({ id: `t-${requestId}`, role: "user", text: prompt, nodeId: null, createdAtMs: Date.now() });
    this.later(() => this.emit({ type: "generationStarted", requestId, kind }), 0);

    if (locked && this.gap("lock")) {
      // The service answers `keep` with unavailable; the request fails without a node.
      this.later(() => {
        if (req.cancelled) return;
        this.s.generations = [];
        this.publish();
        this.emit({ type: "generationFailed", requestId, error: { code: "unavailable", message: "Planning around kept parts or a reference isn't built yet" } });
      }, this.delay);
      return this.ok(true, requestId);
    }

    const title = prompt ? prompt.replace(/^(.{0,28}).*$/, "$1").replace(/^\w/, (ch) => ch.toUpperCase()) : "Surprise idea";
    const variations = Array.from({ length: count }, (_, i) => this.seq + i);
    const makers = partIds && kind !== "initial"
      ? PART_MAKERS.filter((m) => partIds.includes(m(0, 1).partId))
      : PART_MAKERS;
    // Locked parts are kept as they are (the service plans around `keep`); only the rest is written.
    const lockedIds = new Set(this.s.parts.filter((p) => p.locked).map((p) => p.partId));
    const keptParts = kind === "initial" ? (this.nodes.get(parent ?? "")?.clip.parts ?? []).filter((p) => lockedIds.has(p.partId)) : [];
    const writing = makers.filter((m) => !lockedIds.has(m(0, 1).partId));
    const ids: (string | null)[] = variations.map(() => null);
    writing.forEach((make, step) => {
      this.later(() => {
        if (req.cancelled) return;
        variations.forEach((v, i) => {
          const part = make(v, this.s.context.bars);
          const existing = ids[i] ? this.nodes.get(ids[i]!) : undefined;
          if (!existing) {
            const base = kind === "initial" ? keptParts : (this.nodes.get(parent ?? "")?.clip.parts ?? []).filter((p) => p.partId !== part.partId);
            ids[i] = this.addNode({ kind, prompt, title, parts: [...base, part], key: ["A", "minor"], partIds, parentId: parent });
            if (i === 0 && this.s.currentNodeId === parent) this.select(ids[i]!);
          } else {
            existing.clip.parts = [...existing.clip.parts.filter((p) => p.partId !== part.partId), part];
          }
        });
        this.s.generations = [{ requestId, kind, stage: "streaming", partsDone: writing.slice(0, step + 1).map((m) => m(0, 1).partId) }];
        this.refresh();
        this.emit({ type: "session", session: this.session() });
        this.emit({ type: "partReady", requestId, partId: part0(make) });
      }, this.delay * (step + 1));
    });
    this.later(() => {
      if (req.cancelled) return;
      this.s.generations = [];
      for (const id of ids) if (id) this.s.thread.push({ id: `t-${id}`, role: "assistant", text: this.nodes.get(id)!.node.title, nodeId: id, createdAtMs: Date.now() });
      this.refresh();
      this.emit({ type: "session", session: this.session() });
      this.emit({ type: "generationDone", requestId, nodeIds: ids.filter((x): x is string => x !== null) });
    }, this.delay * (writing.length + 1));
    return this.ok(true, requestId);
  }

  private cancel(requestId: string): Reply {
    const req = this.requests.get(requestId);
    if (!req || !this.s.generations.some((g) => g.requestId === requestId)) return this.error("bad_request", `No running request ${requestId}.`);
    req.cancelled = true;
    this.s.generations = [];
    this.later(() => this.emit({ type: "generationFailed", requestId, error: { code: "cancelled", message: "Cancelled." } }), 0);
    return this.ok();
  }

  /** Recomputes the derived parts of the session: context, lineage view, clip and part states. */
  private refresh() {
    const s = this.s;
    const cur = this.current();
    const o = s.override;
    s.nodes = [...this.nodes.values()].map((n) => n.node);
    s.canUndo = this.history.length > 1;
    s.canRedo = this.redoStack.length > 0;
    s.clip = cur ? structuredClone(cur.clip) : null;
    const keyFrom = o.tonic || o.mode ? "override" : cur ? "score" : "default";
    const [tonic, mode] = cur ? cur.key : ["C", "major"] as const;
    const timeFrom = o.tempo || o.meterNumerator ? "override" : "host";
    s.context = {
      tonic: o.tonic ?? tonic, mode: o.mode ?? mode, keyFrom,
      tempo: o.tempo ?? this.host.tempo, meterNumerator: o.meterNumerator ?? this.host.meterNumerator, meterDenominator: o.meterDenominator ?? this.host.meterDenominator,
      timeFrom, bars: o.bars ?? cur?.clip.bars ?? 4,
    };
    const states = new Map(s.parts.map((p) => [p.partId, p]));
    s.parts = (s.clip?.parts ?? []).map((p) => states.get(p.partId) ?? { partId: p.partId, muted: false, solo: false, locked: false, density: 0.5 });
  }

  private tick() {
    if (!this.host.playing) return;
    const beatsPerBar = this.host.meterNumerator;
    const ppq = this.host.positionPpq + (this.host.tempo / 60) / 30;
    this.host = { ...this.host, positionPpq: ppq, bar: Math.floor(ppq / beatsPerBar) + 1, beat: (ppq % beatsPerBar) + 1 };
    this.emit({ type: "transport", transport: this.host });
  }
}

const part0 = (make: (v: number, bars: number) => ClipPart) => make(0, 1).partId;

declare global {
  interface Window { __flowstateMock?: MockPlugin }
}
