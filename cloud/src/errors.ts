// Errors that carry a bridge ErrorCode, so the service can answer with the right code (docs/bridge-spec.md,
// "Error codes"). Anything else that escapes is reported as `internal`.
import type { ErrorCode } from "@flowstate/schema";

export class FlowstateError extends Error {
  constructor(readonly code: ErrorCode, message: string) {
    super(message);
    this.name = "FlowstateError";
  }
}

export const errorCode = (err: unknown): ErrorCode => (err instanceof FlowstateError ? err.code : "internal");

// pi-ai maps refusals and safety stops to stopReason "error"; the provider's own reason tells them apart.
const REFUSAL = /refus|content.?filter|safety|blocked/i;

/** A provider stop that isn't a normal end: refused, cancelled, or another upstream failure. */
export function providerFailure(raw: string, message: string): FlowstateError {
  if (/abort/i.test(raw)) return new FlowstateError("cancelled", message);
  return new FlowstateError(REFUSAL.test(`${raw} ${message}`) ? "refused" : "provider", message);
}
