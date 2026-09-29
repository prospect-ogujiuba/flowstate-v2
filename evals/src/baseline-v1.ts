// Generates the v1 baseline for every prompt: v1's deterministic lane compilers driven by
// request-derived fallback plans (v1 production pinned nearly every plan field to these values).
//
// Usage: tsx src/baseline-v1.ts [--prompts prompts/phase0.json] [--out out/v1] [--v1-bin <dir>]
//
// Output per prompt: out/v1/<id>.notes.json (core's notes.json format), out/v1/<id>.mid,
// out/v1/<id>.v1.json (plans, compile evidence summaries, deviations). Failures: out/v1/failures.json.
import { createHash } from "node:crypto";
import { mkdirSync, writeFileSync } from "node:fs";
import { createRequire } from "node:module";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { loadPrompts, parseArgs, type Note, type NotesFile, type Part, type PromptEntry } from "./common.ts";
import { notesFileToSmf } from "./smf.ts";

const here = path.dirname(fileURLToPath(import.meta.url));
const evalsRoot = path.join(here, "..");
const args = parseArgs(process.argv.slice(2));
const promptsFile = path.resolve(args.flags.prompts ?? path.join(evalsRoot, "prompts", "phase0.json"));
const outDir = path.resolve(args.flags.out ?? path.join(evalsRoot, "out", "v1"));
const v1Bin = path.resolve(
  args.flags["v1-bin"] ?? process.env.FLOWSTATE_V1_BIN ??
    "/home/priz/projects/flowstate/packaging/standalone-sidecar/FlowstateSidecar/bin",
);

// ---- v1 modules (CommonJS, loaded read-only from the v1 repo) ----------------------------------
type Json = any; // v1 is untyped JS; its results are handled defensively below.
type CompileFn = (plan: Json, request: Json) => Json;
type FallbackFn = (request: Json) => Json;
const req = createRequire(import.meta.url);
const load = (file: string): Record<string, unknown> => req(path.join(v1Bin, file)) as Record<string, unknown>;
const chordsMod = load("ai-midi-chords-compiler.js");
const bassMod = load("ai-midi-bass-compiler.js");
const melodyMod = load("ai-midi-melody-compiler.js");
const drumsMod = load("ai-midi-drums-compiler.js");
const adapter = load("flowstate-sidecar-sdk-adapter.js");

const lanes = {
  chords: { fallback: chordsMod.fallbackChordsPlan as FallbackFn, compile: chordsMod.compileChordsPlan as CompileFn },
  bass: { fallback: bassMod.fallbackBassPlan as FallbackFn, compile: bassMod.compileBassPlan as CompileFn },
  melody: { fallback: melodyMod.fallbackMelodyPlan as FallbackFn, compile: melodyMod.compileMelodyPlan as CompileFn },
  drums: { fallback: drumsMod.fallbackDrumsPlan as FallbackFn, compile: drumsMod.compileDrumsPlan as CompileFn },
} as const;
type Lane = keyof typeof lanes;
const LANE_ORDER: Lane[] = ["chords", "bass", "melody", "drums"]; // chords first: bass/melody consume its harmonicContext
const normalizeAiMidiRequest = adapter.normalizeAiMidiRequest as ((r: Json) => Json) | undefined;
const resolveExpressiveMidiContext = adapter.resolveExpressiveMidiContext as (r: Json) => Json;
const validateAiMidiGenerationRequest = adapter.validateAiMidiGenerationRequest as (r: Json) => Json;

// ---- control mapping ---------------------------------------------------------------------------
const PPQ_OUT = 960;
const PPQ_V1 = 480;
/** score-IR mode -> v1 `scale`. v1 supported only these; prompts must stay inside them. */
const V1_SCALE: Record<string, string> = {
  major: "major", minor: "minor", dorian: "dorian", phrygian: "phrygian", mixolydian: "mixolydian", major_pentatonic: "pentatonic",
};
const PART_META: Record<Lane, { id: string; name: string; channel: number }> = {
  chords: { id: "chords", name: "v1 Chords", channel: 1 },
  bass: { id: "bass", name: "v1 Bass", channel: 2 },
  melody: { id: "melody", name: "v1 Melody", channel: 3 },
  drums: { id: "drums", name: "v1 Drums", channel: 10 },
};
/** v1 plugin default note velocity for chord events (MidiClipModel.h defaultVelocity). */
const V1_CHORD_VELOCITY = 96;

function seedFor(id: string): string { return createHash("sha256").update(`phase0:${id}`).digest("hex").slice(0, 16); }

function baseRequest(p: PromptEntry, lane: Lane, seed: string): Json {
  const c = p.controls;
  const scale = V1_SCALE[c.mode];
  if (!scale) throw new Error(`mode ${c.mode} is not supported by v1`);
  const requestId = `phase0-${p.id}-${lane}`;
  // Mirrors v1's plugin payload (FlowstatePromptEnvelope::buildAiMidiGenerationPayload) with UI defaults:
  // density/energy/complexity 0, balanced expressive context, full per-lane event budget, compiler v2 for chords.
  const request: Json = {
    schema: "flowstate.aiMidiGenerationRequest.v1",
    requestId,
    lane,
    userPrompt: p.prompt,
    genreHint: c.style.join(", "),
    referenceHint: "",
    key: c.tonic,
    scale,
    tempoBpm: c.tempo,
    meterNumerator: c.meterNumerator,
    meterDenominator: c.meterDenominator,
    bars: c.bars,
    maxEvents: lane === "drums" ? 32 : 16,
    latencyBudgetMilliseconds: 45000,
    density: 0,
    energy: 0,
    complexity: 0,
    monophonic: lane === "melody" || lane === "bass",
    requireFullChordTimeline: lane === "chords",
    expressiveContext: resolveExpressiveMidiContext({}),
    desiredSourceMode: "aiGenerated",
    responseSchema: "flowstate.aiMidiPlan.v1",
    compilerCapability: { planSchema: "flowstate.aiMidiPlan.v1", compilerId: "flowstate.midiPlanCompiler", compilerVersion: lane === "chords" ? 2 : 1 },
    seed,
    lineage: { mode: "initial", rootRequestId: requestId, ordinal: 0 },
  };
  if (lane === "drums") request.drumGeneration = { kind: "wholeKit" };
  // v1 plan lanes require a Flowstate-owned 64-hex request fingerprint; derive it deterministically.
  request.requestFingerprint = createHash("sha256").update(JSON.stringify(request)).digest("hex");
  return normalizeAiMidiRequest ? normalizeAiMidiRequest(request) : request;
}

// ---- event conversion (v1 beats are quarter notes; v1 ticks are 480 PPQ) -------------------------
const beatsToTicks = (beats: number): number => Math.round(Number(beats) * PPQ_OUT);

function chordsNotes(compiled: Json): Note[] {
  const out: Note[] = [];
  for (const e of compiled.candidate.events as Json[]) {
    if (e.type !== "chord") continue;
    const pitches: number[] = Array.isArray(e.renderedPitches) ? e.renderedPitches : [];
    // compiler v2 events carry authoritative startTick/durationTicks at 480 PPQ; fall back to beats.
    const tick = Number.isFinite(e.startTick) ? Math.round((e.startTick * PPQ_OUT) / PPQ_V1) : beatsToTicks(e.startBeat);
    const dur = Number.isFinite(e.durationTicks) ? Math.round((e.durationTicks * PPQ_OUT) / PPQ_V1) : beatsToTicks(e.durationBeats);
    for (const pitch of pitches) out.push({ tick, dur, pitch, vel: V1_CHORD_VELOCITY });
  }
  return out;
}

function pitchedNotes(compiled: Json): Note[] {
  return (compiled.candidate.events as Json[])
    .filter((e) => e.type === "note")
    .map((e) => ({ tick: beatsToTicks(e.startBeat), dur: Math.max(1, beatsToTicks(e.durationBeats)), pitch: Number(e.pitch), vel: Number(e.velocity) }));
}

function drumNotes(compiled: Json): Note[] {
  // compiled.candidate is v1's humanized expanded preview (what the sidecar hands the plugin).
  return (compiled.candidate.events as Json[]).map((e) => ({
    tick: beatsToTicks(e.startBeat), dur: Math.max(1, beatsToTicks(e.durationBeats)), pitch: Number(e.pitch), vel: Number(e.velocity),
  }));
}

function clipNotes(notes: Note[], clipTicks: number): Note[] {
  return notes
    .filter((n) => n.tick >= 0 && n.tick < clipTicks)
    .map((n) => ({ ...n, dur: Math.max(1, Math.min(n.dur, clipTicks - n.tick)) }))
    .sort((a, b) => a.tick - b.tick || a.pitch - b.pitch);
}

// ---- main --------------------------------------------------------------------------------------
interface Failure { promptId: string; lane: string; stage: string; code: string; message: string; attempts?: unknown[] }

const prompts = loadPrompts(promptsFile);
mkdirSync(outDir, { recursive: true });
const failures: Failure[] = [];
const summary: Record<string, unknown>[] = [];

for (const p of prompts) {
  const c = p.controls;
  const seed = seedFor(p.id);
  const ticksPerBar = (PPQ_OUT * 4 * c.meterNumerator) / c.meterDenominator;
  const clipTicks = ticksPerBar * c.bars;
  const parts: Part[] = [];
  const laneRecords: Record<string, unknown> = {};
  let harmonicContext: Json = null;

  for (const lane of LANE_ORDER.filter((l) => c.lanes.includes(l))) {
    try {
      const request = baseRequest(p, lane, seed);
      if (harmonicContext && (lane === "bass" || lane === "melody")) request.harmonicContext = harmonicContext;
      const validation = validateAiMidiGenerationRequest(request);
      if (!validation.ok) {
        failures.push({ promptId: p.id, lane, stage: "request_validation", code: String(validation.code), message: String(validation.message) });
        console.log(`FAIL ${p.id}/${lane}  request_validation ${validation.code}`);
        continue;
      }
      const { fallback, compile } = lanes[lane];
      const plan = fallback(request);
      let compiled = compile(plan, request);
      const deviations: string[] = [];
      const attempts: unknown[] = [];
      // v1's provider schema let the model choose melody phraseLengthBeats in {2,4,8}. The fallback always
      // uses 4, which exceeds the 16-event budget on long clips; try the other schema-legal values before
      // declaring a failure (the model could have picked them), and record the deviation.
      if (!compiled.ok && lane === "melody") {
        for (const phraseLengthBeats of [8, 2]) {
          attempts.push({ phraseLengthBeats: plan.intent.phraseLengthBeats, code: compiled.code });
          const alt = { ...plan, intent: { ...plan.intent, phraseLengthBeats } };
          const retry = compile(alt, request);
          if (retry.ok) { compiled = retry; deviations.push(`melody intent.phraseLengthBeats=${phraseLengthBeats} (fallback 4 failed: ${String(attempts[0] && (attempts[0] as Json).code)})`); break; }
          compiled = retry;
        }
      }
      if (!compiled.ok) {
        failures.push({ promptId: p.id, lane, stage: "compile", code: String(compiled.code), message: String(compiled.message), ...(attempts.length ? { attempts } : {}) });
        console.log(`FAIL ${p.id}/${lane}  ${compiled.code}: ${compiled.message}`);
        continue;
      }
      const raw = lane === "chords" ? chordsNotes(compiled) : lane === "drums" ? drumNotes(compiled) : pitchedNotes(compiled);
      const notes = clipNotes(raw, clipTicks);
      const meta = PART_META[lane];
      parts.push({ id: meta.id, role: lane, name: meta.name, channel: meta.channel, notes });
      if (lane === "chords") {
        const spans = (compiled.candidate.events as Json[])
          .filter((e) => e.type === "chord")
          .slice(0, 16)
          .map((e) => ({ symbol: String(e.value), startBeat: Number(e.startBeat), durationBeats: Number(e.durationBeats) }));
        harmonicContext = {
          key: request.key, scale: request.scale, policyVersion: "flowstate.harmonicPolicy.v1", contextRevision: 1,
          sourceChordIdeaId: String(compiled.candidate.ideaId), chordSpans: spans,
        };
      }
      laneRecords[lane] = {
        planIntent: (compiled.compileEvidence?.resolvedControls ?? null),
        decisions: compiled.compileEvidence?.decisions ?? null,
        adherence: compiled.compileEvidence?.adherence ?? null,
        chordAware: lane === "bass" || lane === "melody" ? Boolean(request.harmonicContext) : undefined,
        deviations,
        noteCount: notes.length,
      };
    } catch (err) {
      failures.push({ promptId: p.id, lane, stage: "exception", code: "exception", message: String(err) });
      console.log(`FAIL ${p.id}/${lane}  exception ${String(err).slice(0, 200)}`);
    }
  }

  const doc: NotesFile = { ppq: PPQ_OUT, bars: c.bars, ticksPerBar, tempo: c.tempo, meter: [c.meterNumerator, c.meterDenominator], parts };
  const base = path.join(outDir, p.id);
  writeFileSync(`${base}.notes.json`, JSON.stringify(doc, null, 1));
  writeFileSync(`${base}.mid`, notesFileToSmf(doc));
  writeFileSync(`${base}.v1.json`, JSON.stringify({ promptId: p.id, seed, lanes: laneRecords }, null, 2));
  summary.push({ id: p.id, parts: parts.map((x) => `${x.role}:${x.notes.length}`).join(" ") });
  console.log(`ok   ${p.id.padEnd(26)} ${parts.map((x) => `${x.role}=${x.notes.length}`).join(" ")}`);
}

writeFileSync(path.join(outDir, "failures.json"), JSON.stringify({ v1Bin, generatedAt: new Date().toISOString(), count: failures.length, failures }, null, 2));
console.log(`\n${prompts.length} prompts, ${failures.length} lane failure(s). Output: ${outDir}`);
