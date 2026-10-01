// Semantic checks the JSON schema can't express. Errors go back to the model for one repair pass.
import type { Score } from "@flowstate/schema";

const STEP_TOKENS: Record<string, RegExp> = {
  chords: /^[.xX-]$/,
  pad: /^[.xX-]$/,
  arp: /^[.xX-]$/,
  bass: /^[.xXg\-R3578a]$/,
  melody: /^[.xX\-R3578]$/,
  counter: /^[.xX\-R3578]$/,
  drums: /^[.xXg]$/,
};

const NOTE_NAME = /^[A-G](#|b)?-?\d$/;
// Compact motif token: <pitch>:<beats>[!], pitch r or [b|#]degree with +/- octave marks, beats decimal or fraction.
const MOTIF_TOKEN = /^(r|[b#]?\d{1,2}(\++|-+)?):(\d*\.?\d+|\d+\/\d+)!?$/;

function checkMotifString(id: string, notes: string, errors: string[]) {
  for (const token of notes.trim().split(/\s+/)) {
    if (token === "|" || token === "") continue;
    const m = MOTIF_TOKEN.exec(token);
    const beats = m ? (m[3]!.includes("/") ? Number(m[3]!.split("/")[0]) / Number(m[3]!.split("/")[1]) : Number(m[3])) : NaN;
    if (!m || !(beats > 0) || !Number.isFinite(beats))
      errors.push(`motif ${id}: bad token '${token}' (expected <degree>:<beats>, e.g. 5:.5, b3:1!, 1+:1/3, r:.5)`);
  }
}

function checkSteps(label: string, steps: string, tokens: RegExp, stepsPerBar: number, errors: string[]) {
  const bars = steps.replace(/\s+/g, "").split("|");
  bars.forEach((bar, i) => {
    if (bar.length !== stepsPerBar)
      errors.push(`${label}: bar ${i + 1} of pattern has ${bar.length} steps, expected ${stepsPerBar}`);
    for (const ch of bar) if (!tokens.test(ch)) errors.push(`${label}: invalid step token '${ch}'`);
  });
}

export function validateScore(score: Score): string[] {
  const errors: string[] = [];
  const { context } = score;
  const totalBars = context.bars;
  const beatsPerBar = context.meterNumerator;

  if (totalBars < 1 || totalBars > 64) errors.push(`context.bars ${totalBars} out of range 1..64`);
  if (![2, 4, 8, 16].includes(context.meterDenominator)) errors.push(`meterDenominator ${context.meterDenominator} unsupported`);

  let expectedStart = 1;
  for (const s of score.form) {
    if (s.startBar !== expectedStart) errors.push(`form section '${s.name}' starts at bar ${s.startBar}, expected ${expectedStart}`);
    expectedStart = s.startBar + s.bars;
  }
  if (score.form.length > 0 && expectedStart !== totalBars + 1)
    errors.push(`form covers bars 1..${expectedStart - 1}, but context.bars is ${totalBars}`);

  for (const c of score.harmony) {
    if (c.bar < 1 || c.bar > totalBars) errors.push(`chord ${c.symbol} at bar ${c.bar} is outside 1..${totalBars}`);
    if (c.beat < 1 || c.beat >= beatsPerBar + 1) errors.push(`chord ${c.symbol} beat ${c.beat} outside the bar`);
    if (c.beats <= 0) errors.push(`chord ${c.symbol} has non-positive duration`);
  }

  const motifIds = new Set(score.motifs.map((m) => m.id));
  for (const m of score.motifs) if (typeof m.notes === "string") checkMotifString(m.id, m.notes, errors);
  const partIds = new Set<string>();
  for (const part of score.parts) {
    if (partIds.has(part.id)) errors.push(`duplicate part id '${part.id}'`);
    partIds.add(part.id);
    if (!NOTE_NAME.test(part.low) || !NOTE_NAME.test(part.high)) errors.push(`part ${part.id}: bad range ${part.low}..${part.high}`);
    if (part.grid < 1 || part.grid > 8) errors.push(`part ${part.id}: grid ${part.grid} out of range 1..8`);
    const stepsPerBar = beatsPerBar * part.grid;
    const tokens = STEP_TOKENS[part.role]!;

    let lastEnd = 0;
    for (const [i, b] of part.blocks.entries()) {
      const label = `part ${part.id} block ${i + 1}`;
      if (b.startBar < 1 || b.endBar > totalBars || b.startBar > b.endBar)
        errors.push(`${label}: bars ${b.startBar}..${b.endBar} invalid for a ${totalBars}-bar clip`);
      if (b.startBar <= lastEnd) errors.push(`${label}: overlaps the previous block`);
      lastEnd = Math.max(lastEnd, b.endBar);
      if (b.rhythm) checkSteps(`${label} rhythm`, b.rhythm, tokens, stepsPerBar, errors);
      if (b.motif && !motifIds.has(b.motif)) errors.push(`${label}: unknown motif '${b.motif}'`);
      for (const lane of b.drums ?? []) checkSteps(`${label} ${lane.voice}`, lane.steps, STEP_TOKENS.drums!, stepsPerBar, errors);
      for (const n of b.notes ?? []) if (!NOTE_NAME.test(n.pitch)) errors.push(`${label}: bad literal pitch '${n.pitch}'`);
      if (part.role === "drums" && !b.drums?.length) errors.push(`${label}: drums block has no drum lanes`);
      if ((part.role === "melody" || part.role === "counter") && !b.motif && !b.rhythm && !b.notes?.length)
        errors.push(`${label}: melody block needs a motif, a rhythm or literal notes`);
    }
  }
  return errors;
}
