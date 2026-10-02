import { useEffect, useRef } from "preact/hooks";
import { cssVar } from "./css.ts";

/** A note in beats (quarter notes). The Studio maps clip ticks to this; the roll does no theory. */
export type RollNote = { pitch: number; start: number; length: number; velocity: number };

export type PartColour = "chords" | "bass" | "melody" | "drums" | "extra";

/**
 * Mini piano roll for a lane. Canvas, repainted on the next frame after a change; the playhead is
 * a separate prop so transport updates don't rebuild the notes.
 */
export function PianoRoll({ label, notes, beats, beatsPerBar = 4, colour, playhead = null, height = 44 }: {
  label: string;
  notes: RollNote[];
  beats: number;
  /** Bar lines are drawn in the accent, as v1's sequencer preview did. */
  beatsPerBar?: number;
  colour: PartColour;
  /** In beats, or null when stopped. */
  playhead?: number | null;
  height?: number;
}) {
  const canvas = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    const c = canvas.current;
    if (!c) return;
    const draw = () => {
      const dpr = window.devicePixelRatio || 1;
      const w = c.clientWidth;
      const h = c.clientHeight;
      c.width = Math.round(w * dpr);
      c.height = Math.round(h * dpr);
      const g = c.getContext("2d");
      if (!g || w === 0) return;
      g.setTransform(dpr, 0, 0, dpr, 0, 0);
      g.clearRect(0, 0, w, h);

      const bx = w / beats;
      g.fillStyle = cssVar(c, "--line-soft");
      for (let b = 1; b < beats; b++) g.fillRect(Math.round(b * bx), 0, 1, h);
      g.fillStyle = cssVar(c, "--roll-bar");
      for (let b = beatsPerBar; b < beats - 1e-6; b += beatsPerBar) g.fillRect(Math.round(b * bx), 0, 1, h);

      if (notes.length > 0) {
        const lo = Math.min(...notes.map((n) => n.pitch)) - 1;
        const hi = Math.max(...notes.map((n) => n.pitch)) + 1;
        const row = Math.max(2, Math.min(6, (h - 4) / (hi - lo + 1)));
        const top = (h - row * (hi - lo + 1)) / 2;
        const fill = cssVar(c, `--part-${colour}`);
        for (const n of notes) {
          g.globalAlpha = 0.45 + 0.55 * Math.max(0, Math.min(1, n.velocity / 127));
          g.fillStyle = fill;
          const x = n.start * bx;
          const y = top + (hi - n.pitch) * row;
          const nw = Math.max(1.5, n.length * bx - 1);
          const nh = Math.max(1.5, row - 1);
          g.beginPath();
          g.roundRect(x + 0.5, y, nw, nh, Math.min(1.5, nw / 2, nh / 2));
          g.fill();
        }
        g.globalAlpha = 1;
      }

      if (playhead !== null) {
        g.fillStyle = cssVar(c, "--text");
        g.fillRect(Math.round(playhead * bx), 0, 1, h);
      }
    };
    let frame = requestAnimationFrame(draw);
    const ro = new ResizeObserver(() => {
      cancelAnimationFrame(frame);
      frame = requestAnimationFrame(draw);
    });
    ro.observe(c);
    return () => {
      cancelAnimationFrame(frame);
      ro.disconnect();
    };
  }, [notes, beats, beatsPerBar, colour, playhead]);

  return (
    <div class="fs-roll" style={{ height: `${height}px` }} role="img" aria-label={`${label}: ${notes.length} notes over ${beats} beats`}>
      <canvas ref={canvas} class="fs-roll__canvas" />
    </div>
  );
}
