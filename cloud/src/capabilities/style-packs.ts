// Style packs: per-style guidance as a prompt section. The first pack is the drum grooves (GROOVES in the
// IR schema) that match the request's style tags and meter, so the model reaches for the idiomatic pattern
// instead of writing its own (models tended to skip e.g. `trap` on a trap prompt). Per-style prompt tuning
// (P1-3) adds packs here.
import { GROOVES } from "@flowstate/schema";
import type { CapabilityFactory, PlanControls } from "./api.ts";

/** The grooves named by the request's style tags, in its meter; none when it asks for no drums. */
export function matchingGrooves(c: PlanControls): string[] {
  if (!c.lanes.includes("drums")) return [];
  const tags = c.style.map((t) => t.toLowerCase());
  return (Object.entries(GROOVES) as [string, (typeof GROOVES)[keyof typeof GROOVES]][])
    .filter(([, g]) => g.meter[0] === c.meterNumerator && g.meter[1] === c.meterDenominator)
    .filter(([name, g]) => tags.some((t) => name === t.replace(/-/g, "_") || g.description.toLowerCase().startsWith(t.replace(/-/g, " "))))
    .map(([name]) => name);
}

export const stylePacks: CapabilityFactory = (pi) => {
  pi.on("before_agent_start", (event, ctx) => {
    const grooves = matchingGrooves(ctx.request);
    if (grooves.length === 0) return;
    event.systemPromptOptions.sections.style_pack = [
      "<style_pack>",
      `For this request's styles (${ctx.request.style.join(", ")}):`,
      `- drum grooves for these styles: ${grooves.join(", ")} (see Grooves)`,
      "</style_pack>",
    ].join("\n");
  });
};
