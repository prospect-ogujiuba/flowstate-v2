import { useEffect, useId, useMemo, useRef, useState } from "preact/hooks";
import { Button } from "./Button.tsx";
import type { PartColour } from "./PianoRoll.tsx";
import { Segmented } from "./Segmented.tsx";

/** A note in the clip's tick domain, as the bridge's ClipNote carries it. */
export type EditNote = { tick: number; dur: number; pitch: number; vel: number };
/** A note being edited: `id` follows it through moves so the selection sticks. */
export type DraftNote = EditNote & { id: number };
/** One pitch row, named (drum voices); pitched parts get their rows from the notes. */
export type NoteRow = { pitch: number; name: string };

const plain = ({ tick, dur, pitch, vel }: EditNote): EditNote => ({ tick, dur, pitch, vel });
const noteKey = (n: EditNote) => `${n.tick}:${n.dur}:${n.pitch}:${n.vel}`;

export const toDraft = (notes: EditNote[]): DraftNote[] => notes.map((n, i) => ({ ...plain(n), id: i + 1 }));

/**
 * The net change from `before` to `after`, as multisets: an unchanged note is in neither list, and
 * a note added then deleted (or moved back where it was) is in neither.
 */
export function noteDiff(before: EditNote[], after: EditNote[]): { remove: EditNote[]; add: EditNote[] } {
  const left = new Map<string, number>();
  for (const n of before) left.set(noteKey(n), (left.get(noteKey(n)) ?? 0) + 1);
  const add: EditNote[] = [];
  for (const n of after) {
    const k = noteKey(n);
    const c = left.get(k) ?? 0;
    if (c > 0) left.set(k, c - 1);
    else add.push(plain(n));
  }
  const remove: EditNote[] = [];
  for (const n of before) {
    const k = noteKey(n);
    const c = left.get(k) ?? 0;
    if (c > 0) { remove.push(plain(n)); left.set(k, c - 1); }
  }
  return { remove, add };
}

/** The velocity new notes take: the part's median, or 100 for an empty part. */
export function medianVelocity(notes: EditNote[]): number {
  if (notes.length === 0) return 100;
  const v = notes.map((n) => n.vel).sort((a, b) => a - b);
  const mid = v.length >> 1;
  return v.length % 2 ? v[mid]! : Math.round((v[mid - 1]! + v[mid]!) / 2);
}

const NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
/** A display label (middle C = C4), not theory. */
export const pitchName = (p: number) => `${NAMES[p % 12]}${Math.floor(p / 12) - 1}`;

const STEP_PX = 14; // a 1/16 note
const ROW_PX = 14;
const KEYS_PX = 76;
const RULER_PX = 18;
const MARGIN = 4;

const gcd = (a: number, b: number): number => (b === 0 ? a : gcd(b, a % b));
const clamp = (x: number, lo: number, hi: number) => Math.max(lo, Math.min(hi, x));

type Drag = { id: number; mode: "move" | "resize"; x0: number; y0: number; orig: DraftNote };

/**
 * A piano-roll note editor for one part ("touch"). Controlled: the caller owns the draft notes and
 * applies them. Time runs across (bars and beats, snapped to a 1/16 grid or the meter's step), pitch
 * rows run down. The grid is one focusable widget: arrows move a cursor, Enter adds or selects,
 * Shift+arrows move the selected note, Alt+Left/Right change its length, Delete removes it, Escape
 * deselects. Space is never handled here: it belongs to the DAW.
 */
export function NoteEditor({ label, notes, onNotes, ppq, ticksPerBar, bars, rows: drumRows, colour, initialVelocity = 100 }: {
  /** The part's name, for the controls' names. */
  label: string;
  notes: DraftNote[];
  onNotes: (next: DraftNote[]) => void;
  ppq: number;
  ticksPerBar: number;
  bars: number;
  /** Drums: one row per voice, in this order. Absent: pitched rows around the notes. */
  rows?: NoteRow[] | null;
  colour: PartColour;
  /** Velocity for new notes until the slider changes it. */
  initialVelocity?: number;
}) {
  const sixteenth = Math.max(1, Math.round(ppq / 4));
  const step = gcd(sixteenth, ticksPerBar);
  const total = bars * ticksPerBar;
  const pxPerTick = STEP_PX / sixteenth;
  const lengths = useMemo(() => [
    { value: "16", label: "1/16", ticks: sixteenth },
    { value: "8", label: "1/8", ticks: ppq / 2 },
    { value: "4", label: "1/4", ticks: ppq },
    { value: "2", label: "1/2", ticks: ppq * 2 },
    { value: "bar", label: "1 bar", ticks: ticksPerBar },
  ], [ppq, ticksPerBar, sixteenth]);

  const [length, setLength] = useState(lengths[1]!.value);
  const [selected, setSelected] = useState<number | null>(null);
  const [newVel, setNewVel] = useState(initialVelocity);
  const [cursor, setCursor] = useState(() => {
    // The earliest note, topmost row first (drums: the voice map's order; pitched: the highest).
    const rank = (p: number) => { const i = drumRows?.findIndex((r) => r.pitch === p) ?? -1; return i >= 0 ? i - 128 : -p; };
    const first = [...notes].sort((a, b) => a.tick - b.tick || rank(a.pitch) - rank(b.pitch))[0];
    if (first) return { tick: Math.floor(first.tick / step) * step, pitch: first.pitch };
    return { tick: 0, pitch: drumRows?.[0]?.pitch ?? 60 };
  });
  const drag = useRef<Drag | null>(null);
  const grid = useRef<HTMLDivElement>(null);
  const statusId = useId();
  const helpId = useId();

  // Pitched rows: the opened notes' range with a margin (two octaves at least), grown to keep
  // the cursor and every note in view. Drum rows: the voices, plus any stray pitch.
  const base = useMemo(() => {
    if (notes.length === 0) return [48, 72] as const;
    let lo = Math.min(...notes.map((n) => n.pitch)) - MARGIN;
    let hi = Math.max(...notes.map((n) => n.pitch)) + MARGIN;
    while (hi - lo < 24) { lo--; hi++; }
    return [lo, hi] as const;
  }, []); // fixed when the editor opens
  const rows: NoteRow[] = useMemo(() => {
    if (drumRows && drumRows.length) {
      const known = new Set(drumRows.map((r) => r.pitch));
      const stray = [...new Set(notes.map((n) => n.pitch).filter((p) => !known.has(p)))].sort((a, b) => b - a);
      return [...drumRows, ...stray.map((p) => ({ pitch: p, name: pitchName(p) }))];
    }
    const ps = [...notes.map((n) => n.pitch), cursor.pitch];
    const lo = clamp(Math.min(base[0], Math.min(...ps) - 1), 0, 127);
    const hi = clamp(Math.max(base[1], Math.max(...ps) + 1), 0, 127);
    return Array.from({ length: hi - lo + 1 }, (_, i) => ({ pitch: hi - i, name: pitchName(hi - i) }));
  }, [drumRows, notes, cursor.pitch, base]);
  const drums = !!(drumRows && drumRows.length);
  const rowIndex = useMemo(() => new Map(rows.map((r, i) => [r.pitch, i])), [rows]);

  /** The pitch `by` rows below `pitch` (negative = up). */
  const shiftRow = (pitch: number, by: number) => {
    if (!drums) return clamp(pitch - by, 0, 127);
    const i = rowIndex.get(pitch) ?? 0;
    return rows[clamp(i + by, 0, rows.length - 1)]!.pitch;
  };

  const sel = notes.find((n) => n.id === selected) ?? null;
  const noteAt = (tick: number, pitch: number) =>
    notes.find((n) => n.pitch === pitch && n.tick === tick) ??
    notes.find((n) => n.pitch === pitch && n.tick <= tick && tick < n.tick + n.dur) ?? null;
  const under = noteAt(cursor.tick, cursor.pitch);

  const update = (id: number, patch: Partial<EditNote>) => onNotes(notes.map((n) => (n.id === id ? { ...n, ...patch } : n)));
  const nextId = () => notes.reduce((m, n) => Math.max(m, n.id), 0) + 1;
  const add = (tick: number, pitch: number): DraftNote => {
    const want = lengths.find((l) => l.value === length)!.ticks;
    const t = clamp(tick, 0, total - step);
    const n = { id: nextId(), tick: t, dur: Math.max(1, Math.min(want, total - t)), pitch, vel: newVel };
    onNotes([...notes, n]);
    setSelected(n.id);
    return n;
  };
  const remove = (id: number) => {
    onNotes(notes.filter((n) => n.id !== id));
    if (selected === id) setSelected(null);
  };
  const moveTo = (n: DraftNote, tick: number, pitch: number) => {
    const t = clamp(tick, 0, Math.max(0, total - n.dur));
    update(n.id, { tick: t, pitch });
    setCursor({ tick: clamp(Math.floor(t / step) * step, 0, total - step), pitch });
  };
  const minDur = (n: EditNote) => Math.min(step, n.dur);
  const resize = (n: DraftNote, dur: number) => update(n.id, { dur: clamp(dur, minDur(n), total - n.tick) });

  // Keeps the cursor in view by scrolling the editor's own box only (scrollIntoView would scroll
  // the page or the sheet too), clear of the sticky ruler and row names.
  const scroller = useRef<HTMLDivElement>(null);
  const reveal = (centre: boolean) => {
    const sc = scroller.current;
    if (!sc) return;
    const x = KEYS_PX + cursor.tick * pxPerTick;
    const y = RULER_PX + (rowIndex.get(cursor.pitch) ?? 0) * ROW_PX;
    const w = step * pxPerTick;
    if (centre) { sc.scrollTop = y - sc.clientHeight / 2; sc.scrollLeft = Math.max(0, x - KEYS_PX - 4 * STEP_PX); return; }
    if (y - RULER_PX < sc.scrollTop) sc.scrollTop = y - RULER_PX;
    else if (y + ROW_PX > sc.scrollTop + sc.clientHeight) sc.scrollTop = y + ROW_PX - sc.clientHeight;
    if (x - KEYS_PX < sc.scrollLeft) sc.scrollLeft = x - KEYS_PX;
    else if (x + w > sc.scrollLeft + sc.clientWidth) sc.scrollLeft = x + w - sc.clientWidth;
  };
  useEffect(() => reveal(false), [cursor.tick, cursor.pitch]);
  // Centred once the box has a size (a sheet lays out only after it opens).
  useEffect(() => {
    const sc = scroller.current;
    if (!sc) return;
    if (sc.clientHeight > 0) { reveal(true); return; }
    const ro = new ResizeObserver(() => { if (sc.clientHeight > 0) { reveal(true); ro.disconnect(); } });
    ro.observe(sc);
    return () => ro.disconnect();
  }, []);

  const onKeyDown = (e: KeyboardEvent) => {
    if (e.code === "Space" || e.ctrlKey || e.metaKey) return; // Space is the DAW's (host/keys.ts)
    const k = e.key;
    if (k === "ArrowLeft" || k === "ArrowRight") {
      const d = k === "ArrowLeft" ? -1 : 1;
      if (e.altKey && sel) resize(sel, sel.dur + d * step);
      else if (e.shiftKey && sel) moveTo(sel, sel.tick + d * step, sel.pitch);
      else setCursor({ ...cursor, tick: clamp(cursor.tick + d * step, 0, total - step) });
    } else if (k === "ArrowUp" || k === "ArrowDown") {
      const d = k === "ArrowUp" ? -1 : 1;
      if (e.shiftKey && sel) moveTo(sel, sel.tick, shiftRow(sel.pitch, d));
      else if (e.altKey) return;
      else setCursor({ ...cursor, pitch: shiftRow(cursor.pitch, d) });
    } else if (k === "Home" || k === "End") {
      setCursor({ ...cursor, tick: k === "Home" ? 0 : total - step });
    } else if (k === "Enter") {
      if (under) setSelected(under.id);
      else add(cursor.tick, cursor.pitch);
    } else if (k === "Delete" || k === "Backspace") {
      const n = sel ?? under;
      if (n) remove(n.id);
    } else if (k === "Escape") {
      if (!sel) return; // a second Escape closes the sheet
      e.stopPropagation();
      setSelected(null);
    } else {
      return;
    }
    e.preventDefault();
  };

  const at = (e: PointerEvent) => {
    const r = grid.current!.getBoundingClientRect();
    const tick = clamp(Math.floor((e.clientX - r.left) / pxPerTick / step) * step, 0, total - step);
    const row = rows[clamp(Math.floor((e.clientY - r.top) / ROW_PX), 0, rows.length - 1)]!;
    return { tick, pitch: row.pitch };
  };

  const onPointerDown = (e: PointerEvent) => {
    if (e.button !== 0) return;
    e.preventDefault();
    grid.current?.focus({ preventScroll: true });
    const target = e.target as Element;
    const el = target.closest<HTMLElement>("[data-note]");
    let n: DraftNote | null = el ? notes.find((x) => x.id === Number(el.dataset.note)) ?? null : null;
    let mode: Drag["mode"] = target.closest("[data-handle]") ? "resize" : "move";
    if (n) {
      setSelected(n.id);
      setCursor({ tick: clamp(Math.floor(n.tick / step) * step, 0, total - step), pitch: n.pitch });
    } else {
      const c = at(e);
      setCursor(c);
      n = add(c.tick, c.pitch);
      mode = "move";
    }
    drag.current = { id: n.id, mode, x0: e.clientX, y0: e.clientY, orig: n };
    grid.current?.setPointerCapture?.(e.pointerId);
  };
  const onPointerMove = (e: PointerEvent) => {
    const d = drag.current;
    if (!d) return;
    const dx = (e.clientX - d.x0) / pxPerTick;
    const n = notes.find((x) => x.id === d.id);
    if (!n) return;
    if (d.mode === "move") {
      const tick = clamp(d.orig.tick + Math.round(dx / step) * step, 0, Math.max(0, total - n.dur));
      const pitch = shiftRow(d.orig.pitch, Math.round((e.clientY - d.y0) / ROW_PX));
      if (tick !== n.tick || pitch !== n.pitch) moveTo(n, tick, pitch);
    } else {
      const end = Math.round((d.orig.tick + d.orig.dur + dx) / step) * step;
      const dur = clamp(end - n.tick, minDur(d.orig), total - n.tick);
      if (dur !== n.dur) update(n.id, { dur });
    }
  };
  const endDrag = () => { drag.current = null; };

  const lengthName = (dur: number) => lengths.find((l) => l.ticks === dur)?.label ?? `${+(dur / ppq).toFixed(2)} beats`;
  const where = (tick: number) => {
    const bar = Math.floor(tick / ticksPerBar) + 1;
    const inBar = tick % ticksPerBar;
    const beat = Math.floor(inBar / ppq) + 1;
    const sub = Math.floor((inBar % ppq) / sixteenth) + 1;
    return `Bar ${bar}, beat ${beat}${sub > 1 ? `, sixteenth ${sub}` : ""}`;
  };
  const rowName = rows[rowIndex.get(cursor.pitch) ?? 0]?.name ?? pitchName(cursor.pitch);
  const status = `${where(cursor.tick)}, ${rowName}, ${
    under ? `${under.id === selected ? "selected note" : "note"}, ${lengthName(under.dur)}, velocity ${under.vel}` : "empty"
  }`;
  const vel = sel ? sel.vel : newVel;

  const width = total * pxPerTick;
  const height = rows.length * ROW_PX;
  const style = {
    "--ned-bar": `${ticksPerBar * pxPerTick}px`,
    "--ned-beat": `${ppq * pxPerTick}px`,
    "--ned-keys": `${KEYS_PX}px`,
    "--ned-ruler": `${RULER_PX}px`,
    "--ned-row": `${ROW_PX}px`,
  };

  return (
    <div class={`fs-ned fs-ned--${colour}`} style={style}>
      <div class="fs-ned__tools">
        <span class="fs-ned__tool-label" aria-hidden="true">Length</span>
        <Segmented label="Note length" value={length} onChange={setLength} options={lengths.map(({ value, label: l }) => ({ value, label: l }))} />
        <label class="fs-ned__vel">
          <span>{sel ? "Velocity" : "New velocity"}</span>
          <input
            type="range"
            min={1}
            max={127}
            value={vel}
            onInput={(e) => {
              const v = Number((e.target as HTMLInputElement).value);
              if (sel) update(sel.id, { vel: v });
              else setNewVel(v);
            }}
          />
          <span class="fs-num fs-ned__vel-value" aria-hidden="true">{vel}</span>
        </label>
        <Button variant="ghost" size="sm" label="Delete selected note" disabled={!sel} onClick={() => sel && remove(sel.id)}>Delete</Button>
      </div>
      <div class="fs-ned__scroll" ref={scroller}>
        <div class="fs-ned__sheet" style={{ width: `${KEYS_PX + width}px`, height: `${RULER_PX + height}px` }}>
          <div class="fs-ned__corner" aria-hidden="true" />
          <div class="fs-ned__ruler" aria-hidden="true" style={{ width: `${width}px` }}>
            {Array.from({ length: bars }, (_, b) => (
              <span key={b} class="fs-ned__barnum fs-num" style={{ left: `${b * ticksPerBar * pxPerTick}px` }}>{b + 1}</span>
            ))}
          </div>
          <div class="fs-ned__keys" aria-hidden="true" style={{ height: `${height}px` }}>
            {rows.map((r) => <span key={r.pitch} class={r.name.includes("#") && !drums ? "fs-ned__key is-sharp" : "fs-ned__key"}>{r.name}</span>)}
          </div>
          <div
            ref={grid}
            class="fs-ned__grid"
            role="application"
            aria-roledescription="note grid"
            aria-label={`${label} notes`}
            aria-describedby={`${statusId} ${helpId}`}
            tabIndex={0}
            style={{ width: `${width}px`, height: `${height}px` }}
            onKeyDown={onKeyDown}
            onPointerDown={onPointerDown}
            onPointerMove={onPointerMove}
            onPointerUp={endDrag}
            onPointerCancel={endDrag}
          >
            {rows.map((r, i) => (
              <div key={r.pitch} class={r.name.includes("#") && !drums ? "fs-ned__row is-sharp" : "fs-ned__row"} style={{ top: `${i * ROW_PX}px` }} />
            ))}
            <div class="fs-ned__lines" />
            {notes.map((n) => {
              const i = rowIndex.get(n.pitch);
              if (i === undefined) return null;
              return (
                <div
                  key={n.id}
                  data-note={n.id}
                  class={n.id === selected ? "fs-ned__note is-selected" : "fs-ned__note"}
                  style={{
                    left: `${n.tick * pxPerTick}px`,
                    top: `${i * ROW_PX}px`,
                    width: `${Math.max(4, n.dur * pxPerTick - 1)}px`,
                    opacity: 0.45 + 0.55 * (n.vel / 127),
                  }}
                >
                  <span class="fs-ned__handle" data-handle="" />
                </div>
              );
            })}
            <div
              class="fs-ned__cursor"
              style={{ left: `${cursor.tick * pxPerTick}px`, top: `${(rowIndex.get(cursor.pitch) ?? 0) * ROW_PX}px`, width: `${step * pxPerTick}px` }}
            />
          </div>
        </div>
      </div>
      <p id={statusId} class="fs-ned__status" aria-live="polite">{status}</p>
      <p id={helpId} class="fs-ned__help">
        Arrows move the cursor; Enter adds a note or selects one. Shift+arrows move the selected note, Alt+Left/Right change its
        length, Delete removes it, Escape deselects. Or click to add, drag to move, drag a note's end to resize.
      </p>
    </div>
  );
}
