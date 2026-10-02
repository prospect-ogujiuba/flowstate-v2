// The capability registry: every capability the service has, imported statically. Nothing is discovered
// from disk or npm at runtime. Per request, `createLoadout` runs each capability's factory against a fresh
// API, collects its prompt sections, and wraps its tools for the agent with the request's context bound.
//
// The tool allowlist is the only way a tool reaches a model: registering a name outside it throws, and
// `beforeToolCall` refuses any call outside it before capability handlers see it. There is no shell,
// filesystem or MCP tool in the process to allow.
import type { AgentTool, BeforeToolCallContext, BeforeToolCallResult } from "@earendil-works/pi-agent-core";
import type {
  BeforeAgentStartEvent, Capability, CapabilityAPI, CapabilityContext, CapabilityHandler, ToolCallEvent, ToolCallEventResult, ToolDefinition,
} from "./api.ts";
import { libraryExamples } from "./library-examples.ts";
import { midiAnalysis } from "./midi-analysis.ts";
import { stylePacks } from "./style-packs.ts";

export const CAPABILITIES: readonly Capability[] = [
  { name: "style-packs", factory: stylePacks },
  { name: "library-examples", factory: libraryExamples },
  { name: "midi-analysis", factory: midiAnalysis },
];

/** Every tool a model may call. */
export const ALLOWED_TOOLS: ReadonlySet<string> = new Set(["library_examples", "analyze_clip"]);

export interface Loadout {
  /** Named prompt sections for the system prompt, after the base prompt. */
  sections: Record<string, string>;
  /** The tools offered to the model; empty unless tools were asked for. */
  tools: AgentTool[];
  /** The agent's `beforeToolCall`: the allowlist, then the capabilities' `tool_call` handlers. */
  beforeToolCall(call: Pick<BeforeToolCallContext, "toolCall" | "args">): Promise<BeforeToolCallResult | undefined>;
}

export interface LoadoutOptions {
  prompt: string;
  ctx: CapabilityContext;
  /** Offer the capabilities' tools. Off by default: a tool call costs a model round trip. */
  tools?: boolean;
  /** For tests; the service always uses CAPABILITIES. */
  capabilities?: readonly Capability[];
}

export async function createLoadout(options: LoadoutOptions): Promise<Loadout> {
  const { ctx } = options;
  const startHandlers: CapabilityHandler<BeforeAgentStartEvent>[] = [];
  const callHandlers: CapabilityHandler<ToolCallEvent, ToolCallEventResult>[] = [];
  const defs = new Map<string, ToolDefinition>();

  for (const capability of options.capabilities ?? CAPABILITIES) {
    const api: CapabilityAPI = {
      on(event: "before_agent_start" | "tool_call", handler: CapabilityHandler<never, never>) {
        const list: unknown[] = event === "before_agent_start" ? startHandlers : callHandlers;
        list.push(handler);
        return () => void list.splice(list.indexOf(handler), 1);
      },
      registerTool(tool) {
        if (!ALLOWED_TOOLS.has(tool.name)) throw new Error(`capability '${capability.name}' registers '${tool.name}', which is not in the tool allowlist`);
        if (defs.has(tool.name)) throw new Error(`tool '${tool.name}' is registered twice`);
        defs.set(tool.name, tool as unknown as ToolDefinition);
      },
    };
    capability.factory(api);
  }

  const event: BeforeAgentStartEvent = { type: "before_agent_start", prompt: options.prompt, systemPromptOptions: { sections: {} } };
  for (const handler of startHandlers) await handler(event, ctx);
  const sections = event.systemPromptOptions.sections;

  const offered = options.tools ? [...defs.values()] : [];
  const guidelines = offered.flatMap((t) => t.promptGuidelines ?? []);
  if (guidelines.length) sections.tool_guidelines = ["<tool_guidelines>", ...guidelines.map((g) => `- ${g}`), "</tool_guidelines>"].join("\n");

  const tools: AgentTool[] = offered.map((def) => ({
    name: def.name,
    label: def.label,
    description: def.description,
    parameters: def.parameters,
    execute: (id, params, signal, onUpdate) => def.execute(id, params, signal, onUpdate, ctx),
  }));
  const offeredNames = new Set(tools.map((t) => t.name));

  return {
    sections,
    tools,
    async beforeToolCall({ toolCall, args }) {
      if (!ALLOWED_TOOLS.has(toolCall.name) || !offeredNames.has(toolCall.name))
        return { block: true, reason: `tool '${toolCall.name}' is not allowed` };
      const call: ToolCallEvent = { type: "tool_call", toolCallId: toolCall.id, toolName: toolCall.name, input: (args ?? {}) as Record<string, unknown> };
      for (const handler of callHandlers) {
        const result = await handler(call, ctx);
        if (result?.block) return { block: true, reason: result.reason ?? `tool '${toolCall.name}' was blocked` };
      }
      return undefined;
    },
  };
}

/** The system prompt with its sections, as one text, for backends without system messages (pi-ai's rendering). */
export function systemText(base: string, sections: Record<string, string>): string {
  return [base, ...Object.values(sections)].filter((s) => s.length > 0).join("\n\n");
}
