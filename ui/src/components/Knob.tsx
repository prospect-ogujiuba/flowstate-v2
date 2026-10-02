import { useId, useRef } from "preact/hooks";

type Props = {
  label: string;
  value: number;
  min?: number;
  max?: number;
  step?: number;
  /** Double-click resets to this value. */
  defaultValue?: number;
  format?: (value: number) => string;
  onChange: (value: number) => void;
  size?: number;
  showLabel?: boolean;
  disabled?: boolean;
  /** Why this build can't use it (Session.unavailable): looks disabled, stays focusable, says why. */
  unavailable?: string | null;
};

const SWEEP = 270; // degrees, from 7:30 to 4:30
const DRAG_PX = 150; // pixels for a full sweep; Shift drags 10x finer

function arc(cx: number, cy: number, r: number, from: number, to: number) {
  const pt = (deg: number) => {
    const a = ((deg - 90) * Math.PI) / 180;
    return `${cx + r * Math.cos(a)} ${cy + r * Math.sin(a)}`;
  };
  return `M ${pt(from)} A ${r} ${r} 0 ${to - from > 180 ? 1 : 0} 1 ${pt(to)}`;
}

/**
 * Rotary control, exposed as a slider. Keys: arrows step, PageUp and PageDown step 10x, Home and
 * End jump to the ends. Pointer: drag up or down. Double-click resets.
 */
export function Knob({ label, value, min = 0, max = 1, step = 0.01, defaultValue, format, onChange, size = 32, showLabel = true, disabled, unavailable }: Props) {
  const reasonId = useId();
  const inert = disabled || Boolean(unavailable);
  const drag = useRef<{ y: number; start: number } | null>(null);
  const clamp = (v: number) => Math.min(max, Math.max(min, Math.round(v / step) * step));
  const set = (v: number) => {
    const next = Number(clamp(v).toFixed(6));
    if (next !== value) onChange(next);
  };
  const t = (value - min) / (max - min);
  const start = -SWEEP / 2;
  const end = start + SWEEP * t;
  // A range that spans zero (register, transpose) fills from the centre.
  const origin = min < 0 && max > 0 ? start + SWEEP * ((0 - min) / (max - min)) : start;
  const r = size / 2 - 3;
  const text = format ? format(value) : String(value);

  return (
    <div class={showLabel ? "fs-knob" : "fs-knob fs-knob--bare"}>
      <div
        role="slider"
        tabIndex={disabled ? -1 : 0}
        aria-label={label}
        aria-valuemin={min}
        aria-valuemax={max}
        aria-valuenow={value}
        aria-valuetext={text}
        aria-disabled={inert || undefined}
        aria-describedby={unavailable ? reasonId : undefined}
        title={unavailable ? `${label}: ${unavailable}` : `${label}: ${text}`}
        class="fs-knob__dial"
        style={{ width: `${size}px`, height: `${size}px` }}
        onKeyDown={(e) => {
          if (inert) return;
          const big = step * 10;
          const by: Record<string, number> = { ArrowUp: step, ArrowRight: step, ArrowDown: -step, ArrowLeft: -step, PageUp: big, PageDown: -big };
          if (e.key in by) set(value + by[e.key]!);
          else if (e.key === "Home") set(min);
          else if (e.key === "End") set(max);
          else return;
          e.preventDefault();
        }}
        onPointerDown={(e) => {
          if (inert) return;
          (e.currentTarget as HTMLElement).setPointerCapture(e.pointerId);
          drag.current = { y: e.clientY, start: value };
        }}
        onPointerMove={(e) => {
          if (!drag.current) return;
          const fine = e.shiftKey ? 0.1 : 1;
          set(drag.current.start + ((drag.current.y - e.clientY) / DRAG_PX) * (max - min) * fine);
        }}
        onPointerUp={() => { drag.current = null; }}
        onPointerCancel={() => { drag.current = null; }}
        onDblClick={() => { if (!inert && defaultValue !== undefined) set(defaultValue); }}
      >
        <svg width={size} height={size} viewBox={`0 0 ${size} ${size}`} aria-hidden="true">
          <path class="fs-knob__track" d={arc(size / 2, size / 2, r, start, start + SWEEP)} />
          {Math.abs(end - origin) > 0.5 && (
            <path class="fs-knob__value" d={arc(size / 2, size / 2, r, Math.min(origin, end), Math.max(origin, end))} />
          )}
          <line
            class="fs-knob__pointer"
            x1={size / 2}
            y1={size / 2}
            x2={size / 2}
            y2={size / 2 - r + 3}
            transform={`rotate(${end} ${size / 2} ${size / 2})`}
          />
        </svg>
      </div>
      {unavailable && <span id={reasonId} class="sr-only">{unavailable}</span>}
      {showLabel && (
        <span class="fs-knob__label" aria-hidden="true">
          {label}
          <span class="fs-knob__value-text fs-num">{text}</span>
        </span>
      )}
    </div>
  );
}
