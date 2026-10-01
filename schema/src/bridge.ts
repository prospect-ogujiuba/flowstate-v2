// Bridge schema v0 — source of truth for WebView <-> plugin and plugin <-> agent service messages.
// Semantics: docs/bridge-spec.md. C++ types are generated into schema/cpp/include/flowstate/bridge.h.
// Conventions (as in score.ts): fixed keys, every key required, nullable instead of optional.
// Every schema the C++ generator should name is registered in `bridge` with a PascalCase id.
import { z } from "zod";
import { Context, DrumVoice, Mode, Part, Role, Score, Tonic } from "./score.ts";

export const BRIDGE_ID = "flowstate.bridge.v0";

export const bridge = z.registry<{ id: string }>();
function def<T extends z.ZodType>(id: string, schema: T): T {
  bridge.add(schema, { id });
  return schema;
}

// Score IR types. `core` owns their parsing (flowstate::parseScore), so C++ carries Score and
// ScorePart as raw JSON; ScoreContext and the enums are plain data and get generated types.
def("Role", Role);
def("Tonic", Tonic);
def("Mode", Mode);
def("DrumVoice", DrumVoice);
def("ScoreContext", Context);
def("Score", Score);
def("ScorePart", Part);

const int = (min: number, max: number) => z.number().int().min(min).max(max);
const Uint32 = int(0, 0xffffffff).meta({ cpp: "int64" });
const EpochMs = z.number().int().min(0).meta({ cpp: "int64" }).describe("Unix time in milliseconds");

// ---------- Shared ----------

export const ErrorCode = def("ErrorCode", z.enum([
  "bad_request", "unknown_node", "unknown_part", "busy", "cancelled", "unavailable",
  "refused", "truncated", "invalid_score", "provider", "network", "internal",
]));

export const ErrorInfo = def("ErrorInfo", z.object({
  code: ErrorCode,
  message: z.string().describe("Human-readable; never contains keys or other secrets"),
}));

export const ProviderChoice = def("ProviderChoice", z.object({
  provider: z.string().describe("pi-ai provider id, e.g. anthropic, openai, openrouter, google"),
  model: z.string(),
}));

export const BarRange = def("BarRange", z.object({
  startBar: z.number().int().min(1),
  endBar: z.number().int().min(1).describe("Inclusive"),
}));

export const CaptureIntent = def("CaptureIntent", z.enum(["continue", "harmonize", "add_bass", "add_drums", "answer"]));

// ---------- Session (plugin-owned; the UI sees a view of it) ----------

export const LoopRange = def("LoopRange", z.object({
  startPpq: z.number(),
  endPpq: z.number(),
}));

export const Transport = def("Transport", z.object({
  playing: z.boolean(),
  recording: z.boolean(),
  positionPpq: z.number().describe("Host position in quarter notes"),
  bar: z.number().int().describe("1-based"),
  beat: z.number().describe("1-based, fractional"),
  tempo: z.number(),
  meterNumerator: int(1, 32),
  meterDenominator: int(1, 32),
  loop: LoopRange.nullable(),
}));

export const ContextOverride = def("ContextOverride", z.object({
  tonic: Tonic.nullable(),
  mode: Mode.nullable(),
  tempo: z.number().min(20).max(400).nullable(),
  meterNumerator: int(1, 32).nullable(),
  meterDenominator: int(1, 32).nullable(),
  bars: int(1, 64).nullable(),
  swing: z.number().min(0).max(0.5).nullable().describe("null = as the score says"),
}).describe("null = follow the host (tempo, meter), the score (swing) or the default (key, bars)"));

export const KeySource = def("KeySource", z.enum(["override", "score", "detected", "default"]));
export const TimeSource = def("TimeSource", z.enum(["host", "override", "score", "default"]));

export const EffectiveContext = def("EffectiveContext", z.object({
  tonic: Tonic,
  mode: Mode,
  keyFrom: KeySource,
  tempo: z.number(),
  meterNumerator: int(1, 32),
  meterDenominator: int(1, 32),
  timeFrom: TimeSource,
  bars: int(1, 64),
}));

export const NodeKind = def("NodeKind", z.enum(["sketch", "initial", "regenerate", "vary", "edit", "tweak", "touch", "library"]));
export const Rating = def("Rating", z.enum(["up", "down"]));

export const NodeSummary = def("NodeSummary", z.object({
  id: z.string(),
  parentId: z.string().nullable(),
  kind: NodeKind,
  prompt: z.string().nullable(),
  partIds: z.array(z.string()).nullable().describe("Parts this node changed; null = all"),
  title: z.string(),
  createdAtMs: EpochMs,
  rating: Rating.nullable(),
  entryId: z.string().nullable().describe("The library clip this node started from (lib:<pack>/<clip>); its credit travels with the node"),
}));

export const LineageNode = def("LineageNode", z.object({
  id: z.string(),
  parentId: z.string().nullable(),
  kind: NodeKind,
  prompt: z.string().nullable(),
  partIds: z.array(z.string()).nullable(),
  createdAtMs: EpochMs,
  rating: Rating.nullable(),
  entryId: z.string().nullable(),
  seed: Uint32,
  score: Score,
}));

export const ThreadRole = def("ThreadRole", z.enum(["user", "assistant"]));

export const ThreadItem = def("ThreadItem", z.object({
  id: z.string(),
  role: ThreadRole,
  text: z.string().describe("Prompt, or the assistant's reply / answer to a production question"),
  nodeId: z.string().nullable().describe("The lineage node this item produced; its result card"),
  createdAtMs: EpochMs,
}));

export const PartState = def("PartState", z.object({
  partId: z.string(),
  muted: z.boolean(),
  solo: z.boolean(),
  locked: z.boolean().describe("Kept as-is by generate, vary and edit"),
  density: z.number().min(0).max(1).describe("Density knob; 0.5 = as written. A local transform, not a lineage node"),
}));

export const MidiOut = def("MidiOut", z.object({
  role: Role.nullable().describe("Only send this role's part from this instance; null = all parts"),
  channel: int(1, 16).nullable().describe("Send everything on this channel; null = each part's own channel"),
}));

export const Audition = def("Audition", z.object({
  nodeId: z.string().nullable().describe("Node to play; null = the current node"),
  loop: BarRange.nullable().describe("Bars to loop; null = the whole clip"),
  freeRun: z.boolean().describe("Play on the plugin's own clock while the host is stopped"),
}));

export const ClipNote = def("ClipNote", z.object({
  tick: z.number().int().min(0),
  dur: z.number().int().min(1),
  pitch: int(0, 127),
  vel: int(1, 127),
}));

export const DrumVoiceNote = def("DrumVoiceNote", z.object({
  voice: DrumVoice,
  pitch: int(0, 127),
  sublane: z.string().describe("v1 sublane: kick, snare, clap_rim, hats, toms, cymbals, aux_kit"),
}));

export const ClipPart = def("ClipPart", z.object({
  partId: z.string(),
  role: Role,
  name: z.string(),
  channel: int(1, 16),
  notes: z.array(ClipNote),
  voices: z.array(DrumVoiceNote).nullable().describe("Drums only: GM map for sublane rows and split export"),
}));

export const Clip = def("Clip", z.object({
  ppq: z.number().int().min(1),
  bars: int(1, 64),
  ticksPerBar: z.number().int().min(1),
  parts: z.array(ClipPart),
}).describe("core's realization of a node, for lanes and piano rolls"));

export const Usage = def("Usage", z.object({
  used: z.number().min(0),
  limit: z.number().min(0).nullable().describe("null = no limit (e.g. BYOK)"),
  resetsAtMs: EpochMs.nullable(),
}).describe("From the service, never estimated locally"));

export const Settings = def("Settings", z.object({
  provider: ProviderChoice.nullable().describe("null = managed default"),
  byokEnabled: z.boolean().describe("Release flag; read-only for the UI"),
  hasKey: z.boolean().describe("A BYOK key is stored for `provider`; the key itself never crosses the bridge back"),
  previewSynth: z.boolean(),
  usage: Usage.nullable(),
  buildId: z.string(),
}));

export const GenerationStage = def("GenerationStage", z.enum(["planning", "streaming"]));

export const Generation = def("Generation", z.object({
  requestId: z.string(),
  kind: NodeKind,
  stage: GenerationStage,
  partsDone: z.array(z.string()),
}));

// ---------- Catalog: library clips and AI results, searched together ----------
// One entry model: a library clip (with its credit) or an AI result (a lineage node, with its prompt).
// The built-in library ships as a LibraryCatalog (library/catalog/catalog.json, generated by `library/`).

export const LIBRARY_CATALOG_ID = "flowstate.libraryCatalog.v1";

export const CatalogOrigin = def("CatalogOrigin", z.enum(["library", "ai"]));
export const Groove = def("Groove", z.enum(["straight", "swing", "triplet"]));
const Unit = z.number().min(0).max(1);

export const ClipCredit = def("ClipCredit", z.object({
  packId: z.string(),
  packTitle: z.string(),
  producer: z.string(),
  text: z.string().describe("Attribution, shown wherever the clip is: card, preview, drag and export"),
  licenseId: z.string(),
  allowsExport: z.boolean(),
  allowsStyleExamples: z.boolean().describe("The planner may show the clip to a model as a style example"),
}));

export const CatalogEntry = def("CatalogEntry", z.object({
  id: z.string().describe("lib:<pack>/<clip> for library clips, node:<nodeId> for AI results"),
  origin: CatalogOrigin,
  title: z.string(),
  roles: z.array(Role).describe("The parts in the clip; a library clip has one"),
  genres: z.array(z.string()).describe("Style tags in the planner's vocabulary (lowercase kebab-case)"),
  tags: z.array(z.string()),
  feel: z.string().nullable(),
  tonic: Tonic.nullable().describe("null = not known reliably; keyNote says why"),
  mode: Mode.nullable(),
  keyNote: z.string().nullable().describe("How the key was found, or why it is blank"),
  tempo: z.number().describe("As written"),
  tempoMin: z.number().describe("Tempo range the clip suits"),
  tempoMax: z.number(),
  meterNumerator: int(1, 32),
  meterDenominator: int(1, 32),
  bars: int(1, 64),
  energy: Unit.nullable().describe("0..1 from core's analysis; null for AI results until analyzed"),
  density: Unit.nullable(),
  complexity: Unit.nullable(),
  groove: Groove.nullable(),
  credit: ClipCredit.nullable().describe("Set for library clips"),
  nodeId: z.string().nullable().describe("Set for AI results"),
  prompt: z.string().nullable(),
  kind: NodeKind.nullable(),
  createdAtMs: EpochMs.nullable(),
}));

export const ClipFidelity = def("ClipFidelity", z.object({
  rhythm: Unit.describe("Onset F1 between the IR's realization and the original MIDI"),
  pitch: Unit.describe("Mean pitch-class overlap of matched onsets"),
  notes: Unit.describe("Note F1 (exact pitch; pitch class for chords)"),
  literalNotes: z.number().int().min(0).describe("Original notes the IR keeps as literal notes"),
}));

export const LibraryClip = def("LibraryClip", z.object({
  entry: CatalogEntry,
  file: z.string().describe("Normalized SMF next to the catalog: flat file name, no directories"),
  sha256: z.string().describe("Of the pack's source file, so a changed source shows up as drift"),
  harmony: z.array(z.string()).describe("Chord symbols in order, as the analyzer named them"),
  fidelity: ClipFidelity,
  score: Score,
}));

export const LibraryPack = def("LibraryPack", z.object({
  id: z.string(),
  title: z.string(),
  producer: z.string(),
  credit: z.string(),
  licenseId: z.string(),
  notes: z.array(z.string()).describe("Lane notes and omissions, e.g. why a pack has no drums"),
}));

export const LibraryCatalog = def("LibraryCatalog", z.object({
  schema: z.literal(LIBRARY_CATALOG_ID),
  packs: z.array(LibraryPack),
  clips: z.array(LibraryClip),
}));

export const CatalogQuery = def("CatalogQuery", z.object({
  text: z.string().describe("Words matched against title, genres, tags, feel, producer and prompt; empty = any"),
  origins: z.array(CatalogOrigin).nullable().describe("null = both"),
  roles: z.array(Role).nullable().describe("null = any; an entry matches if it has one of these parts"),
  genres: z.array(z.string()).nullable(),
  fitContext: z.boolean().describe("Rank by fit to the session's key, tempo and meter, and leave out other meters"),
  limit: int(1, 100),
  offset: z.number().int().min(0),
}));

export const CatalogPage = def("CatalogPage", z.object({
  total: z.number().int().min(0).describe("Matches before limit and offset"),
  entries: z.array(CatalogEntry),
}));

export const Session = def("Session", z.object({
  protocol: z.literal(BRIDGE_ID),
  instanceId: z.string(),
  override: ContextOverride,
  context: EffectiveContext,
  captureBars: int(0, 64).describe("Bars of recently played MIDI available to `generate.capture`"),
  nodes: z.array(NodeSummary),
  currentNodeId: z.string().nullable(),
  canUndo: z.boolean(),
  canRedo: z.boolean(),
  thread: z.array(ThreadItem),
  parts: z.array(PartState),
  clip: Clip.nullable().describe("Realization of currentNodeId"),
  audition: Audition,
  midiOut: MidiOut,
  settings: Settings,
  generations: z.array(Generation).describe("Running requests"),
  preview: z.string().nullable().describe("Catalog entry being previewed in time with the host; null = none"),
}));

export const SavedSession = def("SavedSession", z.object({
  protocol: z.literal(BRIDGE_ID),
  instanceId: z.string(),
  override: ContextOverride,
  nodes: z.array(LineageNode),
  currentNodeId: z.string().nullable(),
  redo: z.array(z.string()).describe("Node ids undone from currentNodeId, most recent last"),
  thread: z.array(ThreadItem),
  parts: z.array(PartState),
  audition: Audition,
  midiOut: MidiOut,
  previewSynth: z.boolean(),
}).describe("Plugin state (getStateInformation). No keys, no provider credentials."));

// ---------- WebView -> plugin commands ----------
// One native function, `bridge`, takes a Command and completes with a Reply.

const msg = <T extends string, S extends z.ZodRawShape>(id: string, type: T, shape: S) =>
  def(id, z.object({ type: z.literal(type), ...shape }));

export const CaptureUse = def("CaptureUse", z.object({
  bars: int(1, 64).describe("The last N captured bars"),
  intent: CaptureIntent,
}));

export const TweakOp = def("TweakOp", z.enum(["register", "transpose", "humanize", "simplify", "intensify", "revoice"]));

export const Hello = msg("Hello", "hello", { protocol: z.literal(BRIDGE_ID) });
export const Generate = msg("Generate", "generate", {
  prompt: z.string().describe("May be empty (\"Surprise me\")"),
  roles: z.array(Role).nullable().describe("null = the planner chooses"),
  count: int(1, 4).describe("Variations to plan in parallel; each becomes a node"),
  capture: CaptureUse.nullable().describe("\"Use what I just played\""),
});
export const Edit = msg("Edit", "edit", {
  prompt: z.string(),
  partIds: z.array(z.string()).nullable().describe("null = any unlocked part"),
});
export const Vary = msg("Vary", "vary", { partId: z.string() });
export const Reroll = msg("Reroll", "reroll", { partId: z.string() });
export const Tweak = msg("Tweak", "tweak", {
  partId: z.string().nullable().describe("null = every unlocked part"),
  op: TweakOp,
  amount: z.number().nullable().describe("register: octaves; transpose: semitones; humanize: 0..1; others: null"),
});
export const EditNotes = msg("EditNotes", "editNotes", {
  partId: z.string(),
  remove: z.array(ClipNote).describe("Notes as they appear in the current clip"),
  add: z.array(ClipNote),
});
export const AddPart = msg("AddPart", "addPart", { role: Role, prompt: z.string().nullable() });
export const RemovePart = msg("RemovePart", "removePart", { partId: z.string() });
export const Cancel = msg("Cancel", "cancel", { requestId: z.string() });
export const SelectNode = msg("SelectNode", "selectNode", { nodeId: z.string() });
export const Undo = msg("Undo", "undo", {});
export const Redo = msg("Redo", "redo", {});
export const RateNode = msg("RateNode", "rateNode", { nodeId: z.string(), rating: Rating.nullable() });
export const SetPartState = msg("SetPartState", "setPartState", { state: PartState });
export const SetContextOverride = msg("SetContextOverride", "setContextOverride", { override: ContextOverride });
export const SetAudition = msg("SetAudition", "setAudition", { audition: Audition });
export const SetMidiOut = msg("SetMidiOut", "setMidiOut", { midiOut: MidiOut });
export const SetPreviewSynth = msg("SetPreviewSynth", "setPreviewSynth", { enabled: z.boolean() });
export const SetProvider = msg("SetProvider", "setProvider", { provider: ProviderChoice.nullable() });
export const SetApiKey = msg("SetApiKey", "setApiKey", {
  provider: z.string(),
  key: z.string().nullable().describe("null = delete. Goes straight to the OS keychain; never logged or saved"),
});
export const StartDrag = msg("StartDrag", "startDrag", {
  nodeId: z.string().nullable().describe("null = the current node (a thread card can drag its own node)"),
  partIds: z.array(z.string()).nullable().describe("null = all parts (multi-track drag)"),
  splitDrums: z.boolean().describe("One track per drum sublane"),
});
export const ExportMidi = msg("ExportMidi", "exportMidi", {
  nodeId: z.string().nullable(),
  partIds: z.array(z.string()).nullable(),
  splitDrums: z.boolean(),
});
export const SearchCatalog = msg("SearchCatalog", "searchCatalog", { query: CatalogQuery });
export const PreviewEntry = msg("PreviewEntry", "previewEntry", {
  entryId: z.string().nullable().describe("null = stop previewing"),
});
export const UseEntry = msg("UseEntry", "useEntry", {
  entryId: z.string().describe("A library clip becomes a new `library` node; an AI result is selected"),
});
export const DragEntry = msg("DragEntry", "dragEntry", {
  entryId: z.string().describe("Drags the clip as written: a library clip's MIDI, or an AI result's node"),
});
export const FocusReason = def("FocusReason", z.enum(["space", "escape", "blur"]));
export const ReleaseFocus = msg("ReleaseFocus", "releaseFocus", { reason: FocusReason });

export const Command = def("Command", z.discriminatedUnion("type", [
  Hello, Generate, Edit, Vary, Reroll, Tweak, EditNotes, AddPart, RemovePart, Cancel, SelectNode,
  Undo, Redo, RateNode, SetPartState, SetContextOverride, SetAudition, SetMidiOut, SetPreviewSynth,
  SetProvider, SetApiKey, StartDrag, ExportMidi, ReleaseFocus, SearchCatalog, PreviewEntry, UseEntry, DragEntry,
]));

export const Reply = def("Reply", z.object({
  ok: z.boolean(),
  error: ErrorInfo.nullable(),
  requestId: z.string().nullable().describe("Set when the command started a model request"),
  session: Session.nullable().describe("Set on hello and on any command that changed the session"),
  catalog: CatalogPage.nullable().describe("Set on searchCatalog"),
}));

// ---------- Plugin -> WebView events ----------
// Emitted on one event name, `bridge`.

export const SessionChanged = msg("SessionChanged", "session", { session: Session });
export const TransportTick = msg("TransportTick", "transport", { transport: Transport });
export const GenerationStarted = msg("GenerationStarted", "generationStarted", { requestId: z.string(), kind: NodeKind });
export const PartReady = msg("PartReady", "partReady", { requestId: z.string(), partId: z.string() });
export const GenerationDone = msg("GenerationDone", "generationDone", {
  requestId: z.string(),
  nodeIds: z.array(z.string()).describe("One per variation; empty when the reply was text only"),
});
export const GenerationFailed = msg("GenerationFailed", "generationFailed", { requestId: z.string(), error: ErrorInfo });
export const NoticeLevel = def("NoticeLevel", z.enum(["info", "warning", "error"]));
export const Notice = msg("Notice", "notice", { level: NoticeLevel, message: z.string() });

export const PluginEvent = def("PluginEvent", z.discriminatedUnion("type", [
  SessionChanged, TransportTick, GenerationStarted, PartReady, GenerationDone, GenerationFailed, Notice,
]));

// ---------- Plugin -> agent service (HTTPS + SSE) ----------
// A BYOK key travels in the `x-flowstate-provider-key` header, never in a body.

export const Reference = def("Reference", z.object({
  score: Score, // captured MIDI, converted to IR by core's analyzer
  intent: CaptureIntent,
}));

export const PlanRequest = def("PlanRequest", z.object({
  protocol: z.literal(BRIDGE_ID),
  prompt: z.string(),
  context: Context,
  roles: z.array(Role).nullable(),
  keep: Score.nullable().describe("Locked parts (and harmony) to plan around; null = fresh plan"),
  reference: Reference.nullable(),
  provider: ProviderChoice.nullable(),
}));

export const EditRequest = def("EditRequest", z.object({
  protocol: z.literal(BRIDGE_ID),
  prompt: z.string(),
  score: Score,
  partIds: z.array(z.string()).nullable().describe("Parts the edit may change; locked parts are excluded"),
  provider: ProviderChoice.nullable(),
}));

// SSE `data:` payloads, in order: header, then partStarted/partDone per part, then done (or error).
// `message` may arrive at any point before done.
export const ScoreHeader = msg("ScoreHeader", "header", {
  score: Score, // title, context, form, harmony and motifs; parts is empty
});
export const PartStarted = msg("PartStarted", "partStarted", { partId: z.string(), role: Role });
export const PartDone = msg("PartDone", "partDone", { part: Part });
export const AssistantMessage = msg("AssistantMessage", "message", {
  text: z.string().describe("For the thread: a short note on the result, or the answer to a question"),
});
export const ScoreDone = msg("ScoreDone", "done", {
  score: Score.nullable().describe("null when the request was answered with text only"),
});
export const ServiceError = msg("ServiceError", "error", { error: ErrorInfo });

export const ServiceEvent = def("ServiceEvent", z.discriminatedUnion("type", [
  ScoreHeader, PartStarted, PartDone, AssistantMessage, ScoreDone, ServiceError,
]));

export const Health = def("Health", z.object({
  protocol: z.literal(BRIDGE_ID),
  version: z.string(),
  ok: z.boolean(),
}));

// Top-level message types; fixtures in schema/fixtures/bridge name one of these.
export const bridgeRoots = {
  Command, Reply, PluginEvent, SavedSession, PlanRequest, EditRequest, ServiceEvent, Health, LibraryCatalog,
} as const;
export type BridgeRoot = keyof typeof bridgeRoots;

export type ContextOverride = z.infer<typeof ContextOverride>;
export type Transport = z.infer<typeof Transport>;
export type Session = z.infer<typeof Session>;
export type SavedSession = z.infer<typeof SavedSession>;
export type Command = z.infer<typeof Command>;
export type Reply = z.infer<typeof Reply>;
export type PluginEvent = z.infer<typeof PluginEvent>;
export type PlanRequest = z.infer<typeof PlanRequest>;
export type EditRequest = z.infer<typeof EditRequest>;
export type ServiceEvent = z.infer<typeof ServiceEvent>;
export type Health = z.infer<typeof Health>;
export type CatalogEntry = z.infer<typeof CatalogEntry>;
export type CatalogQuery = z.infer<typeof CatalogQuery>;
export type LibraryClip = z.infer<typeof LibraryClip>;
export type LibraryCatalog = z.infer<typeof LibraryCatalog>;
