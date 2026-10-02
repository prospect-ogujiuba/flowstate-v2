// The capability API: a narrow subset of Pi's `ExtensionAPI` (pi-coding-agent's core/extensions/types.ts),
// so a capability reads like a Pi extension: a factory that calls `registerTool` and `on(event)`, with
// handlers that take `(event, ctx)`. What Pi's API has and this leaves out is deliberate: `exec` (shell),
// commands, shortcuts, flags, providers, renderers and session writes. Capabilities are registered
// statically in registry.ts; nothing is discovered from disk or npm.
//
// Capability modules reach the outside world only through `ctx`: read-only data and host services the
// service hands in. A test (capabilities.test.ts) checks they import no Node built-ins.
import type { AgentToolResult, AgentToolUpdateCallback } from "@earendil-works/pi-agent-core";
import type { LibraryCatalog } from "@flowstate/schema";
import type { Static, TSchema } from "typebox";

/** The session a request plans for. The planner fixes these; capabilities only read them. */
export interface PlanControls {
  tonic: string;
  mode: string;
  bars: number;
  tempo: number;
  meterNumerator: number;
  meterDenominator: number;
  style: string[];
  lanes: string[];
}

/** What core's analyzer measured on a clip (the `analysisJson` fields a model can use). */
export interface ClipAnalysis {
  ok: boolean;
  error?: { code: string; message: string };
  role: string;
  key: { tonic: string | null; mode: string | null; guess: string | null; reason: string };
  grid: { grid: number; swing: number; fit: number };
  descriptors: { density: number; complexity: number; energy: number; syncopation: number; groove: string };
  tempo: number;
  meter: [number, number];
  bars: number;
  harmony: string[];
  warnings: string[];
}

/** Pi's `ExtensionContext`, cut to what Flowstate's capabilities may use. */
export interface CapabilityContext {
  /** The request's session: key, meter, tempo, length, style tags and lanes. */
  request: PlanControls;
  /** Flowstate's built-in library (library/catalog), as data. Null when the service has none. */
  catalog: LibraryCatalog | null;
  /** core's MIDI analyzer (fs-analyze) over a library clip, by catalog entry id. Null when it isn't built. */
  analyzeClip: ((entryId: string, signal?: AbortSignal) => Promise<ClipAnalysis>) | null;
}

/** Pi's `ToolDefinition`, without the TUI rendering hooks. */
export interface ToolDefinition<TParams extends TSchema = TSchema, TDetails = unknown> {
  /** Tool name (used in model tool calls). Must be in the registry's allowlist. */
  name: string;
  label: string;
  /** Description for the model. */
  description: string;
  /** Guideline bullets added to the system prompt while the tool is offered. */
  promptGuidelines?: string[];
  parameters: TParams;
  execute(
    toolCallId: string,
    params: Static<TParams>,
    signal: AbortSignal | undefined,
    onUpdate: AgentToolUpdateCallback<TDetails> | undefined,
    ctx: CapabilityContext,
  ): Promise<AgentToolResult<TDetails>>;
}

/** Pi's `BeforeAgentStartEvent`: handlers add or change named prompt sections. */
export interface BeforeAgentStartEvent {
  type: "before_agent_start";
  /** The request's prompt text. */
  prompt: string;
  /** Mutable prompt sections, rendered after the base prompt. Later handlers see earlier changes. */
  systemPromptOptions: { sections: Record<string, string> };
}

/** Pi's `ToolCallEvent` for a custom tool: fired before a tool runs; a handler can block it. */
export interface ToolCallEvent {
  type: "tool_call";
  toolCallId: string;
  toolName: string;
  input: Record<string, unknown>;
}

export interface ToolCallEventResult {
  block?: boolean;
  reason?: string;
}

export type CapabilityHandler<E, R = undefined> = (event: E, ctx: CapabilityContext) => Promise<R | void> | R | void;

export interface CapabilityAPI {
  on(event: "before_agent_start", handler: CapabilityHandler<BeforeAgentStartEvent>): () => void;
  on(event: "tool_call", handler: CapabilityHandler<ToolCallEvent, ToolCallEventResult>): () => void;
  registerTool<TParams extends TSchema = TSchema, TDetails = unknown>(tool: ToolDefinition<TParams, TDetails>): void;
}

/** Pi's `ExtensionFactory`. */
export type CapabilityFactory = (pi: CapabilityAPI) => void;

export interface Capability {
  name: string;
  factory: CapabilityFactory;
}
