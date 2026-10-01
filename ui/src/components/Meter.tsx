import { useEffect, useRef } from "preact/hooks";
import { cssVar } from "./css.ts";

/** Usage or progress bar (v1's 5 hr and weekly limits). */
export function UsageMeter({ label, value, valueText, tone = "ok" }: {
  label: string;
  /** 0..1 */
  value: number;
  valueText: string;
  tone?: "ok" | "warn" | "danger";
}) {
  return (
    <div class="fs-usage">
      <div class="fs-usage__row">
        <span class="fs-usage__label">{label}</span>
        <span class="fs-usage__text fs-num">{valueText}</span>
      </div>
      <div
        role="meter"
        aria-label={label}
        aria-valuemin={0}
        aria-valuemax={100}
        aria-valuenow={Math.round(value * 100)}
        aria-valuetext={valueText}
        class={`fs-usage__track fs-usage--${tone}`}
      >
        <span class="fs-usage__fill" style={{ width: `${Math.max(0, Math.min(1, value)) * 100}%` }} />
      </div>
    </div>
  );
}

/**
 * Output level meter (v1's footer meter): green to red, with a held peak. Drawn on canvas; the
 * caller feeds it at most 30 Hz and the canvas repaints on the next frame.
 */
export function LevelMeter({ label, level, peak }: { label: string; level: number; peak: number }) {
  const canvas = useRef<HTMLCanvasElement>(null);
  const gradient = useRef<CanvasGradient | null>(null);

  useEffect(() => {
    const c = canvas.current;
    if (!c) return;
    const frame = requestAnimationFrame(() => {
      const dpr = window.devicePixelRatio || 1;
      const w = c.clientWidth;
      const h = c.clientHeight;
      if (c.width !== Math.round(w * dpr) || c.height !== Math.round(h * dpr)) {
        c.width = Math.round(w * dpr);
        c.height = Math.round(h * dpr);
        gradient.current = null;
      }
      const g = c.getContext("2d");
      if (!g) return;
      g.setTransform(dpr, 0, 0, dpr, 0, 0);
      g.clearRect(0, 0, w, h);
      if (!gradient.current) {
        const lg = g.createLinearGradient(0, 0, w, 0);
        lg.addColorStop(0, cssVar(c, "--meter-low"));
        lg.addColorStop(0.78, cssVar(c, "--meter-mid"));
        lg.addColorStop(0.95, cssVar(c, "--meter-high"));
        lg.addColorStop(1, cssVar(c, "--meter-clip"));
        gradient.current = lg;
      }
      g.fillStyle = gradient.current;
      g.fillRect(0, 0, w * Math.max(0, Math.min(1, level)), h);
      const px = Math.round(w * Math.max(0, Math.min(1, peak)));
      g.fillStyle = cssVar(c, peak >= 0.999 ? "--meter-clip" : "--meter-peak");
      g.fillRect(Math.max(0, px - 2), 0, 2, h);
    });
    return () => cancelAnimationFrame(frame);
  }, [level, peak]);

  return (
    <div class="fs-level" role="img" aria-label={`${label}: ${Math.round(level * 100)}%`}>
      <canvas ref={canvas} class="fs-level__canvas" />
    </div>
  );
}
