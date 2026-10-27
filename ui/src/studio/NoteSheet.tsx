// Touch: the note editor sheet for one part. Edits stay local until Apply, which sends one
// `editNotes` with the net difference against the clip as it was when the sheet opened
// (docs/bridge-spec.md). The plugin answers with the new session, or a reason to show.

import { useMemo, useState } from "preact/hooks";
import { Button, medianVelocity, noteDiff, NoteEditor, Sheet, toDraft, type DraftNote, type NoteRow } from "../components/index.ts";
import { partColour, ROLE_LABELS, type Clip, type ClipPart } from "./clip.ts";
import type { Studio } from "./useStudio.ts";

/** What the sheet edits: a snapshot of the part and its clip's grid, taken when it opened. */
export type Touching = { part: ClipPart; ppq: number; ticksPerBar: number; bars: number; seq: number };

export const touching = (clip: Clip, part: ClipPart, seq: number): Touching =>
  ({ part: structuredClone(part), ppq: clip.ppq, ticksPerBar: clip.ticksPerBar, bars: clip.bars, seq });

const voiceName = (voice: string) => voice.replace(/_/g, " ").replace(/^\w/, (c) => c.toUpperCase());

/** Drum rows from the part's voice map (one per GM note, in the map's order). */
function drumRows(part: ClipPart): NoteRow[] | null {
  if (!part.voices || part.voices.length === 0) return null;
  const seen = new Set<number>();
  const rows: NoteRow[] = [];
  for (const v of part.voices) if (!seen.has(v.pitch)) { seen.add(v.pitch); rows.push({ pitch: v.pitch, name: voiceName(v.voice) }); }
  return rows;
}

export function NoteSheet({ open, studio, touch, onClose }: { open: boolean; studio: Studio; touch: Touching | null; onClose: () => void }) {
  const name = touch ? touch.part.name || ROLE_LABELS[touch.part.role] : "";
  return (
    <Sheet open={open} title={`Edit ${name} notes`} wide onClose={onClose}>
      {touch && <Editor key={touch.seq} studio={studio} touch={touch} name={name} onClose={onClose} />}
    </Sheet>
  );
}

function Editor({ studio, touch, name, onClose }: { studio: Studio; touch: Touching; name: string; onClose: () => void }) {
  const { part } = touch;
  const [draft, setDraft] = useState<DraftNote[]>(() => toDraft(part.notes));
  const [busy, setBusy] = useState(false);
  const [refusal, setRefusal] = useState<string | null>(null);
  const rows = useMemo(() => drumRows(part), [part]);
  const diff = noteDiff(part.notes, draft);
  const changes = diff.add.length + diff.remove.length;

  const apply = async () => {
    if (changes === 0) { onClose(); return; }
    setBusy(true);
    const r = await studio.send({ type: "editNotes", partId: part.partId, remove: diff.remove, add: diff.add });
    setBusy(false);
    if (r.ok) onClose();
    else setRefusal(r.error?.message ?? "The plugin didn't take these edits.");
  };

  const summary = changes === 0
    ? "No changes yet"
    : [diff.add.length && `${diff.add.length} added`, diff.remove.length && `${diff.remove.length} removed`].filter(Boolean).join(", ") + " (a move counts as both)";

  // The footer lives inside the editor so it shares the draft; the sheet itself has none.
  return (
    <div class="st-touch">
      <NoteEditor
        label={name}
        notes={draft}
        onNotes={(next) => { setDraft(next); setRefusal(null); }}
        ppq={touch.ppq}
        ticksPerBar={touch.ticksPerBar}
        bars={touch.bars}
        rows={rows}
        colour={partColour(part.role)}
        initialVelocity={medianVelocity(part.notes)}
      />
      <div class="st-touch__foot">
        {refusal
          ? <p class="st-touch__note st-touch__note--error" role="alert">{refusal}</p>
          : <p class="st-touch__note">{summary}</p>}
        <Button variant="danger" size="sm" onClick={onClose}>Cancel</Button>
        <Button variant="ok" size="sm" disabled={busy} onClick={() => void apply()}>Apply</Button>
      </div>
    </div>
  );
}
