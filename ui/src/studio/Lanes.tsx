// The part lanes: one Lane per part of the current idea (v1's Compose lanes, all visible at once).

import type { Session } from "@flowstate/schema";
import { useState } from "preact/hooks";
import { Button, Lane } from "../components/index.ts";
import { beatsPerBar, clipBeats, partColour, ROLE_LABELS, rollNotes, sublanes, type Clip, type Role } from "./clip.ts";
import type { Studio } from "./useStudio.ts";

type PartState = Session["parts"][number];
const ADDABLE: Role[] = ["pad", "arp", "counter", "chords", "bass", "melody", "drums"];

export function Lanes({ studio, clip, parts, playhead, splitDrums, compact }: {
  studio: Studio;
  clip: Clip;
  parts: PartState[];
  playhead: number | null;
  splitDrums: boolean;
  compact: boolean;
}) {
  const { send, gap } = studio;
  const [adding, setAdding] = useState(false);
  const beats = clipBeats(clip);
  const states = new Map(parts.map((p) => [p.partId, p]));
  const present = new Set(clip.parts.map((p) => p.role));
  const addable = ADDABLE.filter((r) => !present.has(r) || r === "pad" || r === "arp" || r === "counter");
  const noteEditor = gap("editNotes") ?? "The Studio's note editor isn't built yet.";

  return (
    <div class="st-lanes">
      <h2 class="sr-only">Parts</h2>
      {clip.parts.map((p) => {
        const st = states.get(p.partId) ?? { partId: p.partId, muted: false, solo: false, locked: false, density: 0.5 };
        const name = p.name || ROLE_LABELS[p.role];
        const drag = (partIds: string[]) => ({ nodeId: null, partIds, splitDrums: splitDrums && p.role === "drums" });
        return (
          <Lane
            key={p.partId}
            name={name}
            role={`${ROLE_LABELS[p.role]} · ch ${p.channel} · ${p.notes.length} notes`}
            colour={partColour(p.role)}
            notes={rollNotes(p.notes, clip.ppq)}
            beats={beats}
            beatsPerBar={beatsPerBar(clip)}
            state={{ muted: st.muted, solo: st.solo, locked: st.locked, density: st.density }}
            onState={(next) => void send({ type: "setPartState", state: { partId: p.partId, ...next } })}
            onVary={() => void send({ type: "vary", partId: p.partId })}
            onReroll={() => void send({ type: "reroll", partId: p.partId })}
            onEditNotes={() => {}}
            onRemove={() => void send({ type: "removePart", partId: p.partId })}
            onDragStart={() => void send({ type: "startDrag", ...drag([p.partId]) })}
            onDragKey={() => void send({ type: "exportMidi", ...drag([p.partId]) })}
            gaps={{
              vary: gap("vary"), reroll: gap("reroll"), lock: gap("lock"), density: gap("density"),
              editNotes: noteEditor,
            }}
            sublanes={sublanes(p, clip.ppq)}
            playhead={playhead}
            rollHeight={compact ? 30 : 44}
          />
        );
      })}
      <div class="st-addpart">
        <Button variant="ghost" size="sm" icon="plus" unavailable={gap("addPart")} aria-expanded={gap("addPart") ? undefined : adding} onClick={() => setAdding(!adding)}>Add part</Button>
        {adding && !gap("addPart") && (
          <div class="st-addpart__roles" role="group" aria-label="Part to add">
            {addable.map((r) => (
              <Button key={r} size="sm" onClick={() => { setAdding(false); void send({ type: "addPart", role: r, prompt: null }); }}>{ROLE_LABELS[r]}</Button>
            ))}
          </div>
        )}
      </div>
    </div>
  );
}
