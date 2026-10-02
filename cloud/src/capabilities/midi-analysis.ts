// MIDI analysis: core's analyzer (fs-analyze) measures a library clip: key, grid and swing, groove,
// density, syncopation, the chords it hears. The model names the clip by catalog id; the host resolves the
// file and runs core (analyzer.ts), so no path or argument from the model reaches the analyzer.
import { Type } from "typebox";
import type { CapabilityFactory } from "./api.ts";

const Params = Type.Object({
  entryId: Type.String({ pattern: "^lib:[a-z0-9-]+/[a-z0-9-]+$", maxLength: 128, description: "A library clip id, e.g. lib:godflow/rnb-jazz-chords-01 (from library_examples)." }),
});

export const midiAnalysis: CapabilityFactory = (pi) => {
  pi.registerTool({
    name: "analyze_clip",
    label: "Analyze clip",
    description:
      "Measures a library clip with Flowstate's MIDI analyzer: detected key, grid and swing, groove, density, complexity, " +
      "syncopation and the chords it hears. Read-only.",
    parameters: Params,
    async execute(_id, params, signal, _onUpdate, ctx) {
      if (!ctx.analyzeClip) throw new Error("MIDI analysis isn't available on this server");
      const a = await ctx.analyzeClip(params.entryId, signal);
      if (!a.ok) throw new Error(`the clip can't be analyzed: ${a.error?.message ?? "unknown reason"}`);
      const key = a.key.tonic ? `${a.key.tonic} ${a.key.mode}` : `not fixed (${a.key.reason})`;
      const text = [
        `${params.entryId}: ${a.role}, ${a.bars} bars of ${a.meter[0]}/${a.meter[1]} at ${a.tempo} BPM`,
        `key: ${key}`,
        `grid: ${a.grid.grid} steps per beat, swing ${a.grid.swing}, groove ${a.descriptors.groove}`,
        `density ${a.descriptors.density}, complexity ${a.descriptors.complexity}, energy ${a.descriptors.energy}, syncopation ${a.descriptors.syncopation}`,
        ...(a.harmony.length ? [`chords: ${a.harmony.join(" ")}`] : []),
      ].join("\n");
      return { content: [{ type: "text", text }], details: { entryId: params.entryId } };
    },
  });
};
