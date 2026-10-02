// The prompt bar (v1's chat box): talk to the model. Return sends, Shift+Return adds a line. The
// suggestion chips change with state (v1's prompt library, as one-click prompts).

import type { Session } from "@flowstate/schema";
import { useEffect, useId, useState } from "preact/hooks";
import { Button, Icon, PromptBox, PromptSetting, Segmented, Select, Sheet, SuggestionChip, useToast } from "../components/index.ts";
import type { Studio } from "./useStudio.ts";

export type PromptMode = "new" | "edit";

const VIBES = ["lofi rainy study, 80 bpm", "neo-soul, lazy swing", "afrobeats groove", "dark trap, half-time"];

function GenerationStatus({ studio, generations }: { studio: Studio; generations: Session["generations"] }) {
  const g = generations[0];
  return (
    <div class="st-progress" role="status">
      {g && (
        <>
          <span class="st-progress__dot" aria-hidden="true" />
          <span class="st-progress__text">
            {g.stage === "planning" ? "Planning…" : `Writing parts · ${g.partsDone.length} in`}
            {generations.length > 1 ? ` (${generations.length} requests)` : ""}
          </span>
          <Button variant="danger" size="sm" onClick={() => void studio.send({ type: "cancel", requestId: g.requestId })}>Cancel</Button>
        </>
      )}
    </div>
  );
}

/** A prompt-row setting that may not be in this build: looks disabled and says why. */
function ModeSetting({ mode, onMode, editGap }: { mode: PromptMode; onMode: (m: PromptMode) => void; editGap: string | null }) {
  const toast = useToast();
  const id = useId();
  const value = mode === "new" ? "New idea" : "Change this idea";
  return (
    <>
      <button
        type="button"
        class={editGap ? "fs-prompt__setting is-unavailable" : "fs-prompt__setting"}
        aria-label={`Prompt makes: ${value}`}
        aria-disabled={editGap ? true : undefined}
        aria-describedby={editGap ? id : undefined}
        title={editGap ? `Change this idea: ${editGap}` : "Switch between a new idea and changing this one"}
        onClick={() => (editGap ? toast({ message: editGap }) : onMode(mode === "new" ? "edit" : "new"))}
      >
        <Icon name="lightbulb" size={16} />
        <span aria-hidden="true">{value}</span>
        <Icon name="caret" size={8} class="fs-prompt__caret" />
      </button>
      {editGap && <span id={id} class="sr-only">Change this idea: {editGap}</span>}
    </>
  );
}

export function PromptBar({ studio, session, value, onInput, mode, onMode, onCapture, onSettings }: {
  studio: Studio;
  session: Session;
  value: string;
  onInput: (v: string) => void;
  mode: PromptMode;
  onMode: (m: PromptMode) => void;
  onCapture: () => void;
  onSettings: () => void;
}) {
  const { send, gap } = studio;
  const [count, setCount] = useState(1);
  const busy = session.generations.length > 0;
  const hasIdea = session.clip !== null;
  const editGap = gap("edit");
  const effectiveMode: PromptMode = hasIdea && !editGap ? mode : "new";
  const captureGap = gap("capture") ?? (session.captureBars === 0 ? "Play something on this track first: nothing has been captured yet." : null);
  const lastPrompt = [...session.thread].reverse().find((t) => t.role === "user")?.text ?? "";
  const provider = session.settings.provider;

  const generate = (prompt: string, n = count) => send({ type: "generate", prompt, roles: null, count: n, capture: null });
  const submit = async (prompt: string) => {
    const reply = effectiveMode === "edit" ? await send({ type: "edit", prompt, partIds: null }) : await generate(prompt);
    if (reply.ok) onInput("");
  };

  useEffect(() => { if (!hasIdea) onMode("new"); }, [hasIdea]);

  return (
    <div class="st-dock">
      <GenerationStatus studio={studio} generations={session.generations} />
      <ul class="st-chips" aria-label="Suggestions">
        {hasIdea ? (
          <>
            <li><SuggestionChip unavailable={editGap} onClick={() => void send({ type: "edit", prompt: "darker", partIds: null })}>darker</SuggestionChip></li>
            <li><SuggestionChip unavailable={editGap} onClick={() => void send({ type: "edit", prompt: "half-time drums", partIds: null })}>half-time drums</SuggestionChip></li>
            <li><SuggestionChip unavailable={captureGap} onClick={onCapture}>answer my melody</SuggestionChip></li>
            <li><SuggestionChip unavailable={busy ? "A generation is already running." : null} onClick={() => void generate(lastPrompt, 3)}>3 variations</SuggestionChip></li>
          </>
        ) : (
          VIBES.map((v) => <li key={v}><SuggestionChip unavailable={busy ? "A generation is already running." : null} onClick={() => void generate(v)}>{v}</SuggestionChip></li>)
        )}
      </ul>
      <PromptBox
        value={value}
        onInput={onInput}
        onSubmit={(p) => void submit(p)}
        busy={busy}
        placeholder={effectiveMode === "edit" ? "Change this idea: darker chords, busier drums in bar 4…" : "Describe or ask…"}
        tools={<Button variant="ghost" icon="collect-audio-data" label="Use what I just played" class="st-capture" unavailable={captureGap} onClick={onCapture} />}
        settings={<>
          <PromptSetting icon={<Icon name="bot" size={18} class="st-bot" />} label="Model" value={provider ? `${provider.provider} • ${provider.model}` : "Managed default"} onClick={onSettings} />
          <PromptSetting icon={<Icon name="lightning" size={14} class="st-idea" />} label="Variations" value={`${count} variation${count > 1 ? "s" : ""}`} onClick={() => setCount((c) => (c % 4) + 1)} />
          {hasIdea && <ModeSetting mode={effectiveMode} onMode={onMode} editGap={editGap} />}
        </>}
      />
    </div>
  );
}

const INTENTS = [
  { value: "continue", label: "Continue" },
  { value: "harmonize", label: "Harmonize" },
  { value: "add_bass", label: "Add bass" },
  { value: "add_drums", label: "Add drums" },
  { value: "answer", label: "Answer" },
] as const;
type Intent = (typeof INTENTS)[number]["value"];

/** "Use what I just played": which bars, and what to do with them (proposal flow 4). */
export function CaptureSheet({ open, captureBars, prompt, onClose, onGenerate }: {
  open: boolean;
  captureBars: number;
  prompt: string;
  onClose: () => void;
  onGenerate: (bars: number, intent: Intent) => void;
}) {
  const [intent, setIntent] = useState<Intent>("continue");
  const choices = [1, 2, 4, 8, 16, 32, 64].filter((b) => b <= captureBars);
  const [bars, setBars] = useState(Math.min(4, captureBars));
  useEffect(() => { if (open) setBars(choices.includes(4) ? 4 : (choices.at(-1) ?? 1)); }, [open]);
  return (
    <Sheet
      open={open}
      title="Use what I just played"
      onClose={onClose}
      footer={<>
        <Button variant="danger" size="sm" onClick={onClose}>Cancel</Button>
        <Button variant="ok" size="sm" disabled={captureBars === 0} onClick={() => { onGenerate(bars, intent); onClose(); }}>Generate</Button>
      </>}
    >
      <div class="st-sheet-body">
        <p class="st-note">{captureBars} bars of what you played on this track are ready.{prompt.trim() ? ` The prompt goes with it: "${prompt.trim()}".` : ""}</p>
        <Segmented label="What to do" value={intent} onChange={setIntent} options={[...INTENTS]} />
        <Select label="Bars" value={String(bars)} onChange={(v) => setBars(Number(v))} options={choices.map((b) => ({ value: String(b), label: `Last ${b}` }))} />
      </div>
    </Sheet>
  );
}
