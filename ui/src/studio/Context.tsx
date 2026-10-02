// The context strip (v1's Context tab, folded into one row) and the sheet that overrides it.
// Values come from the session's effective context; `keyFrom`/`timeFrom` say who supplied them.

import type { Session } from "@flowstate/schema";
import { useEffect, useRef, useState } from "preact/hooks";
import { Button, ContextChip, Knob, Select, Sheet, SheetSection, TextField, Toggle } from "../components/index.ts";
import { modeLabel } from "./clip.ts";

type Ctx = Session["context"];
type Override = Session["override"];
export type ContextField = "key" | "tempo" | "meter" | "bars" | "swing";

const KEY_SOURCE: Record<Ctx["keyFrom"], string> = { override: "", score: "the idea", detected: "what you played", default: "default" };
const TIME_SOURCE: Record<Ctx["timeFrom"], string> = { host: "host", override: "", score: "the idea", default: "default" };

export function ContextStrip({ context, override, onOpen }: { context: Ctx; override: Override; onOpen: (f: ContextField) => void }) {
  const hostTime = context.timeFrom === "host";
  return (
    <div class="st-strip__chips" role="group" aria-label="Context">
      <ContextChip label="Key" value={`${context.tonic} ${modeLabel(context.mode)}`} overridden={context.keyFrom === "override"} source={KEY_SOURCE[context.keyFrom] || undefined} onClick={() => onOpen("key")} />
      <ContextChip label="Tempo" value={`${Math.round(context.tempo * 10) / 10} bpm`} locked={hostTime && override.tempo === null} overridden={override.tempo !== null} source={hostTime ? undefined : TIME_SOURCE[context.timeFrom] || undefined} onClick={() => onOpen("tempo")} />
      <ContextChip label="Meter" value={`${context.meterNumerator}/${context.meterDenominator}`} locked={hostTime && override.meterNumerator === null} overridden={override.meterNumerator !== null} source={hostTime ? undefined : TIME_SOURCE[context.timeFrom] || undefined} onClick={() => onOpen("meter")} />
      <ContextChip label="Bars" value={String(context.bars)} overridden={override.bars !== null} source={override.bars === null ? "the idea" : undefined} onClick={() => onOpen("bars")} />
      <ContextChip label="Swing" value={override.swing === null ? "as written" : `${Math.round(override.swing * 100)}%`} overridden={override.swing !== null} onClick={() => onOpen("swing")} />
    </div>
  );
}

const TONICS: Ctx["tonic"][] = ["C", "C#", "Db", "D", "D#", "Eb", "E", "F", "F#", "Gb", "G", "G#", "Ab", "A", "A#", "Bb", "B"];
const MODES: Ctx["mode"][] = ["major", "minor", "dorian", "phrygian", "lydian", "mixolydian", "locrian", "harmonic_minor", "melodic_minor", "major_pentatonic", "minor_pentatonic", "blues"];
const METERS = ["2/4", "3/4", "4/4", "5/4", "6/8", "7/8", "9/8", "12/8"];
const BARS = [1, 2, 4, 8, 12, 16, 24, 32];
const FOLLOW = "";

/** Overrides for the next generation. Empty choices follow the host, the idea or the default. */
export function ContextSheet({ open, focus, context, override, onApply, onClose }: {
  open: boolean;
  focus: ContextField | null;
  context: Ctx;
  override: Override;
  onApply: (o: Override) => void;
  onClose: () => void;
}) {
  const [draft, setDraft] = useState(override);
  const [tempoText, setTempoText] = useState("");
  const body = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (!open) return;
    setDraft(override);
    setTempoText(override.tempo === null ? "" : String(override.tempo));
    // Focus the field of the chip that opened the sheet.
    requestAnimationFrame(() => body.current?.querySelector<HTMLElement>(`[data-field="${focus}"] select, [data-field="${focus}"] input, [data-field="${focus}"] [role=slider], [data-field="${focus}"] [role=switch]`)?.focus());
  }, [open]);

  const tempo = tempoText.trim() === "" ? null : Number(tempoText);
  const tempoError = tempo !== null && (!Number.isFinite(tempo) || tempo < 20 || tempo > 400) ? "Between 20 and 400 bpm, or empty to follow the host." : undefined;
  const meter = draft.meterNumerator === null ? FOLLOW : `${draft.meterNumerator}/${draft.meterDenominator}`;
  const set = (patch: Partial<Override>) => setDraft({ ...draft, ...patch });

  return (
    <Sheet
      open={open}
      title="Context"
      onClose={onClose}
      footer={<>
        <Button variant="danger" size="sm" onClick={onClose}>Cancel</Button>
        <Button variant="ok" size="sm" disabled={Boolean(tempoError)} onClick={() => { onApply({ ...draft, tempo }); onClose(); }}>Apply</Button>
      </>}
    >
      <div ref={body} class="st-sheet-body">
        <p class="st-note">What the next generation uses. Leave a field on "follow" to take it from the host, the current idea, or the default.</p>
        <SheetSection title="Key">
          <div class="st-pair" data-field="key">
            <Select label="Tonic" value={draft.tonic ?? FOLLOW} onChange={(v) => set({ tonic: v === FOLLOW ? null : (v as Ctx["tonic"]) })}
              options={[{ value: FOLLOW, label: `Follow (${context.tonic})` }, ...TONICS.map((t) => ({ value: t, label: t }))]} />
            <Select label="Mode" value={draft.mode ?? FOLLOW} onChange={(v) => set({ mode: v === FOLLOW ? null : (v as Ctx["mode"]) })}
              options={[{ value: FOLLOW, label: `Follow (${modeLabel(context.mode)})` }, ...MODES.map((m) => ({ value: m, label: modeLabel(m) }))]} />
          </div>
        </SheetSection>
        <SheetSection title="Time">
          <div class="st-pair">
            <div data-field="tempo">
              <TextField label="Tempo (bpm)" value={tempoText} inputMode="decimal" placeholder={`Follow host (${Math.round(context.tempo)})`} error={tempoError} onInput={setTempoText} />
            </div>
            <div data-field="meter">
              <Select label="Meter" value={meter} onChange={(v) => {
                if (v === FOLLOW) return set({ meterNumerator: null, meterDenominator: null });
                const [n, d] = v.split("/").map(Number);
                set({ meterNumerator: n!, meterDenominator: d! });
              }} options={[{ value: FOLLOW, label: `Follow host (${context.meterNumerator}/${context.meterDenominator})` }, ...METERS.map((m) => ({ value: m, label: m }))]} />
            </div>
          </div>
          <div class="st-pair">
            <div data-field="bars">
              <Select label="Bars" value={draft.bars === null ? FOLLOW : String(draft.bars)} onChange={(v) => set({ bars: v === FOLLOW ? null : Number(v) })}
                options={[{ value: FOLLOW, label: `Follow (${context.bars})` }, ...BARS.map((b) => ({ value: String(b), label: String(b) }))]} />
            </div>
            <div class="st-swing" data-field="swing">
              <Toggle label="Swing as written" checked={draft.swing === null} onChange={(asWritten) => set({ swing: asWritten ? null : 0.1 })} />
              <Knob label="Swing" value={draft.swing ?? 0} min={0} max={0.5} step={0.01} defaultValue={0} format={(v) => `${Math.round(v * 100)}%`} disabled={draft.swing === null} onChange={(swing) => set({ swing })} />
            </div>
          </div>
        </SheetSection>
      </div>
    </Sheet>
  );
}
