// The Studio toolbar beside the context strip (undo, redo, free run, tweak, drag and export, the
// thread drawer), and the tweak sheet: local transforms on the IR with no model call.

import type { Session } from "@flowstate/schema";
import { useState } from "preact/hooks";
import { Button, Knob, Select, Sheet, SheetSection } from "../components/index.ts";
import { ROLE_LABELS } from "./clip.ts";
import type { Studio } from "./useStudio.ts";

export function Toolbar({ studio, session, splitDrums, onSplitDrums, onTweak, threadOpen, onThread }: {
  studio: Studio;
  session: Session;
  splitDrums: boolean;
  onSplitDrums: (v: boolean) => void;
  onTweak: () => void;
  threadOpen: boolean;
  onThread: () => void;
}) {
  const { send, gap } = studio;
  const hasIdea = session.clip !== null;
  const hasDrums = session.clip?.parts.some((p) => p.role === "drums") ?? false;
  const drag = { nodeId: null, partIds: null, splitDrums };
  return (
    <div class="st-toolbar" role="toolbar" aria-label="Idea">
      <Button variant="ghost" size="sm" icon="undo" label="Undo" disabled={!session.canUndo} onClick={() => void send({ type: "undo" })} />
      <Button variant="ghost" size="sm" icon="redo" label="Redo" disabled={!session.canRedo} onClick={() => void send({ type: "redo" })} />
      <span class="st-toolbar__sep" aria-hidden="true" />
      <Button
        variant="ghost"
        size="sm"
        icon={session.audition.freeRun ? "stop" : "play"}
        label="Play while the host is stopped"
        pressed={session.audition.freeRun}
        disabled={!hasIdea}
        onClick={() => void send({ type: "setAudition", audition: { ...session.audition, freeRun: !session.audition.freeRun } })}
      />
      <Button variant="ghost" size="sm" icon="sliders2" label="Tweak" disabled={!hasIdea && !gap("tweak")} unavailable={gap("tweak")} onClick={onTweak} />
      <span class="st-toolbar__sep" aria-hidden="true" />
      {hasDrums && (
        <Button variant="ghost" size="sm" class="fs-btn--letter st-split" label="Split drums by sublane when dragging" pressed={splitDrums} onClick={() => onSplitDrums(!splitDrums)}>Split</Button>
      )}
      <Button
        variant="ghost"
        size="sm"
        icon="grip"
        label="Drag all parts into your DAW"
        title="Drag all parts into your DAW (Enter saves a MIDI file)"
        disabled={!hasIdea}
        class="st-dragall"
        onPointerDown={(e) => { e.preventDefault(); void send({ type: "startDrag", ...drag }); }}
        onClick={(e) => { if (e.detail === 0) void send({ type: "exportMidi", ...drag }); }}
      />
      <Button variant="ghost" size="sm" icon="export" label="Save as MIDI file" disabled={!hasIdea} onClick={() => void send({ type: "exportMidi", ...drag })} />
      <span class="st-toolbar__sep" aria-hidden="true" />
      <Button variant="ghost" size="sm" icon="menu" label="Thread" pressed={threadOpen} aria-controls="st-thread" onClick={onThread} />
    </div>
  );
}

type Op = Extract<Parameters<Studio["send"]>[0], { type: "tweak" }>["op"];

/** Tweak (the proposal's middle mode): instant local transforms, each a new lineage node. */
export function TweakSheet({ open, studio, session, onClose }: { open: boolean; studio: Studio; session: Session; onClose: () => void }) {
  const [target, setTarget] = useState("");
  const [humanize, setHumanize] = useState(0.3);
  const parts = session.clip?.parts ?? [];
  const partId = target === "" ? null : target;
  const tweak = (op: Op, amount: number | null) => void studio.send({ type: "tweak", partId, op, amount });
  return (
    <Sheet open={open} title="Tweak" onClose={onClose} footer={<Button size="sm" onClick={onClose}>Done</Button>}>
      <div class="st-sheet-body">
        <p class="st-note">Local changes, no model call. Each one is a new card you can undo.</p>
        <Select label="Apply to" value={target} onChange={setTarget}
          options={[{ value: "", label: "Every unlocked part" }, ...parts.map((p) => ({ value: p.partId, label: p.name || ROLE_LABELS[p.role] }))]} />
        <SheetSection title="Shape">
          <div class="st-row">
            <Button size="sm" onClick={() => tweak("simplify", null)}>Simplify</Button>
            <Button size="sm" onClick={() => tweak("intensify", null)}>Intensify</Button>
            <Button size="sm" onClick={() => tweak("revoice", null)}>Revoice</Button>
          </div>
        </SheetSection>
        <SheetSection title="Pitch">
          <div class="st-row">
            <Button size="sm" onClick={() => tweak("register", -1)}>Octave down</Button>
            <Button size="sm" onClick={() => tweak("register", 1)}>Octave up</Button>
            <Button size="sm" onClick={() => tweak("transpose", -1)}>Semitone down</Button>
            <Button size="sm" onClick={() => tweak("transpose", 1)}>Semitone up</Button>
          </div>
        </SheetSection>
        <SheetSection title="Feel">
          <div class="st-row">
            <Knob label="Humanize" value={humanize} defaultValue={0.3} format={(v) => `${Math.round(v * 100)}%`} onChange={setHumanize} />
            <Button size="sm" onClick={() => tweak("humanize", humanize)}>Apply humanize</Button>
          </div>
        </SheetSection>
      </div>
    </Sheet>
  );
}
