import { useId, useState } from "preact/hooks";
import { Button } from "./Button.tsx";
import { Icon } from "./Icon.tsx";
import { Knob } from "./Knob.tsx";
import { PianoRoll, type PartColour, type RollNote } from "./PianoRoll.tsx";

/** A part's mix and edit state, as the bridge's PartState carries it. */
export type LaneState = { muted: boolean; solo: boolean; locked: boolean; density: number };

export type Sublane = { id: string; name: string; notes: RollNote[]; muted: boolean };

/** Why a lane control can't run in this build (Session.unavailable); absent = available. */
export type LaneGaps = Partial<Record<"vary" | "reroll" | "lock" | "density" | "editNotes", string | null>>;

type Props = {
  name: string;
  role: string;
  colour: PartColour;
  notes: RollNote[];
  beats: number;
  beatsPerBar?: number;
  state: LaneState;
  onState: (next: LaneState) => void;
  onVary?: () => void;
  onReroll?: () => void;
  onEditNotes?: () => void;
  onRemove?: () => void;
  /** Pointer-down starts the native drag (docs/bridge-spec.md, startDrag). */
  onDragStart?: (e: PointerEvent) => void;
  /** Enter on the drag handle: the keyboard can't start an OS drag, so this saves a file instead. */
  onDragKey?: () => void;
  gaps?: LaneGaps;
  /** Drums: one row per sublane, collapsed by default. */
  sublanes?: Sublane[];
  /** Without it, sublanes have no mute buttons. */
  onSublaneMute?: (id: string, muted: boolean) => void;
  playhead?: number | null;
  rollHeight?: number;
};

/** One part row of the Studio: controls, a mini piano roll, and a drag handle. */
export function Lane({
  name, role, colour, notes, beats, beatsPerBar, state, onState, onVary, onReroll, onEditNotes, onRemove, onDragStart, onDragKey,
  gaps = {}, sublanes, onSublaneMute, playhead, rollHeight,
}: Props) {
  const [expanded, setExpanded] = useState(false);
  const subId = useId();
  const set = (patch: Partial<LaneState>) => onState({ ...state, ...patch });

  return (
    <article class={`fs-lane fs-lane--${colour}`} aria-label={`${name} lane`} data-locked={state.locked || undefined} data-muted={state.muted || undefined}>
      <div class="fs-lane__head">
        <button
          type="button"
          class="fs-lane__grip"
          aria-label={`Drag ${name} into your DAW`}
          title={onDragKey ? "Drag into your DAW (Enter saves a MIDI file)" : "Drag into your DAW"}
          onPointerDown={(e) => { e.preventDefault(); onDragStart?.(e); }}
          onClick={(e) => { if (e.detail === 0) onDragKey?.(); }}
        >
          <Icon name="grip" size={14} />
        </button>
        <div class="fs-lane__title">
          <h3 class="fs-lane__name">{name}</h3>
          <span class="fs-lane__role">{role}</span>
        </div>
        <div class="fs-lane__controls">
          <Button variant="ghost" size="sm" class="fs-btn--letter" label={`Mute ${name}`} pressed={state.muted} onClick={() => set({ muted: !state.muted })}>M</Button>
          <Button variant="ghost" size="sm" class="fs-btn--letter" label={`Solo ${name}`} pressed={state.solo} onClick={() => set({ solo: !state.solo })}>S</Button>
          <Button variant="ghost" size="sm" icon={state.locked ? "lock" : "lock-open"} label={`Lock ${name}`} pressed={state.locked} unavailable={gaps.lock} onClick={() => set({ locked: !state.locked })} />
          <Button variant="ghost" size="sm" icon="sparkle" label={`Vary ${name}`} disabled={state.locked && !gaps.vary} unavailable={gaps.vary} onClick={onVary} />
          <Button variant="ghost" size="sm" icon="arrow-repeat" label={`Re-roll ${name}`} disabled={state.locked && !gaps.reroll} unavailable={gaps.reroll} onClick={onReroll} />
          <Knob
            label={`${name} density`}
            value={state.density}
            defaultValue={0.5}
            format={(v) => `${Math.round(v * 100)}%`}
            onChange={(density) => set({ density })}
            size={24}
            showLabel={false}
            disabled={state.locked && !gaps.density}
            unavailable={gaps.density}
          />
          {onEditNotes && <Button variant="ghost" size="sm" icon="canvas" label={`Edit ${name} notes`} unavailable={gaps.editNotes} onClick={onEditNotes} />}
          {onRemove && <Button variant="ghost" size="sm" icon="x" label={`Remove ${name}`} onClick={onRemove} />}
          {sublanes && (
            <Button
              variant="ghost"
              size="sm"
              icon="chevron"
              class={expanded ? "fs-lane__expand is-open" : "fs-lane__expand"}
              label={`${name} sublanes`}
              aria-expanded={expanded}
              aria-controls={subId}
              onClick={() => setExpanded(!expanded)}
            />
          )}
        </div>
      </div>
      <PianoRoll label={`${name} notes`} notes={notes} beats={beats} beatsPerBar={beatsPerBar} colour={colour} playhead={playhead} height={rollHeight} />
      {sublanes && (
        <ul id={subId} class={onSublaneMute ? "fs-lane__subs" : "fs-lane__subs fs-lane__subs--plain"} hidden={!expanded}>
          {sublanes.map((s) => (
            <li key={s.id} class="fs-sublane" data-muted={s.muted || undefined}>
              <span class="fs-sublane__name">{s.name}</span>
              <PianoRoll label={`${s.name} notes`} notes={s.notes} beats={beats} beatsPerBar={beatsPerBar} colour={colour} playhead={playhead} height={14} />
              {onSublaneMute && (
                <Button variant="ghost" size="sm" class="fs-btn--letter" label={`Mute ${s.name}`} pressed={s.muted} onClick={() => onSublaneMute(s.id, !s.muted)}>M</Button>
              )}
            </li>
          ))}
        </ul>
      )}
    </article>
  );
}
