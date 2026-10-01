import { useId, useState } from "preact/hooks";
import { Button } from "./Button.tsx";
import { Icon } from "./Icon.tsx";
import { Knob } from "./Knob.tsx";
import { PianoRoll, type PartColour, type RollNote } from "./PianoRoll.tsx";

export type LaneState = { playing: boolean; solo: boolean; locked: boolean; density: number };

export type Sublane = { id: string; name: string; notes: RollNote[]; muted: boolean };

type Props = {
  name: string;
  role: string;
  colour: PartColour;
  notes: RollNote[];
  beats: number;
  state: LaneState;
  onState: (next: LaneState) => void;
  onVary?: () => void;
  onReroll?: () => void;
  /** Pointer-down starts the native drag (docs/bridge-spec.md, startDrag). */
  onDragStart?: (e: PointerEvent) => void;
  /** Drums: one row per sublane, collapsed by default. */
  sublanes?: Sublane[];
  onSublaneMute?: (id: string, muted: boolean) => void;
  playhead?: number | null;
};

/** One part row of the Studio: controls, a mini piano roll, and a drag handle. */
export function Lane({ name, role, colour, notes, beats, state, onState, onVary, onReroll, onDragStart, sublanes, onSublaneMute, playhead }: Props) {
  const [expanded, setExpanded] = useState(false);
  const subId = useId();
  const set = (patch: Partial<LaneState>) => onState({ ...state, ...patch });

  return (
    <article class={`fs-lane fs-lane--${colour}`} aria-label={`${name} lane`} data-locked={state.locked || undefined}>
      <div class="fs-lane__head">
        <button
          type="button"
          class="fs-lane__grip"
          aria-label={`Drag ${name} into your DAW`}
          title="Drag into your DAW"
          onPointerDown={(e) => { e.preventDefault(); onDragStart?.(e); }}
        >
          <Icon name="grip" size={14} />
        </button>
        <div class="fs-lane__title">
          <h3 class="fs-lane__name">{name}</h3>
          <span class="fs-lane__role">{role}</span>
        </div>
        <div class="fs-lane__controls">
          <Button variant="ghost" size="sm" icon={state.playing ? "stop" : "play"} label={`Play ${name}`} pressed={state.playing} onClick={() => set({ playing: !state.playing })} />
          <Button variant="ghost" size="sm" class="fs-btn--letter" label={`Solo ${name}`} pressed={state.solo} onClick={() => set({ solo: !state.solo })}>S</Button>
          <Button variant="ghost" size="sm" icon={state.locked ? "lock" : "lock-open"} label={`Lock ${name}`} pressed={state.locked} onClick={() => set({ locked: !state.locked })} />
          <Button variant="ghost" size="sm" icon="sparkle" label={`Vary ${name}`} disabled={state.locked} onClick={onVary} />
          <Button variant="ghost" size="sm" icon="arrow-repeat" label={`Re-roll ${name}`} disabled={state.locked} onClick={onReroll} />
          <Knob
            label={`${name} density`}
            value={state.density}
            defaultValue={0.5}
            format={(v) => `${Math.round(v * 100)}%`}
            onChange={(density) => set({ density })}
            size={24}
            showLabel={false}
            disabled={state.locked}
          />
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
      <PianoRoll label={`${name} notes`} notes={notes} beats={beats} colour={colour} playhead={playhead} />
      {sublanes && (
        <ul id={subId} class="fs-lane__subs" hidden={!expanded}>
          {sublanes.map((s) => (
            <li key={s.id} class="fs-sublane" data-muted={s.muted || undefined}>
              <span class="fs-sublane__name">{s.name}</span>
              <PianoRoll label={`${s.name} notes`} notes={s.notes} beats={beats} colour={colour} playhead={playhead} height={14} />
              <Button variant="ghost" size="sm" class="fs-btn--letter" label={`Mute ${s.name}`} pressed={s.muted} onClick={() => onSublaneMute?.(s.id, !s.muted)}>M</Button>
            </li>
          ))}
        </ul>
      )}
    </article>
  );
}
