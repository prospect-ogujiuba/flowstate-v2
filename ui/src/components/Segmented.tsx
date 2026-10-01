import { useRef } from "preact/hooks";
import { Icon } from "./Icon.tsx";

export type SegmentOption<T extends string> = { value: T; label: string; icon?: string };

/** One-of-n choice (v1's Chat / Create switch). A radio group: Tab enters it, arrows move. */
export function Segmented<T extends string>({ label, options, value, onChange }: {
  label: string;
  options: SegmentOption<T>[];
  value: T;
  onChange: (value: T) => void;
}) {
  const ref = useRef<HTMLDivElement>(null);
  const move = (from: number, by: number) => {
    const next = (from + by + options.length) % options.length;
    onChange(options[next]!.value);
    ref.current?.querySelectorAll<HTMLButtonElement>("[role=radio]")[next]?.focus();
  };
  return (
    <div ref={ref} role="radiogroup" aria-label={label} class="fs-segmented">
      {options.map((o, i) => (
        <button
          key={o.value}
          type="button"
          role="radio"
          aria-checked={o.value === value}
          tabIndex={o.value === value ? 0 : -1}
          class={o.value === value ? "fs-segment is-active" : "fs-segment"}
          onClick={() => onChange(o.value)}
          onKeyDown={(e) => {
            if (e.key === "ArrowRight" || e.key === "ArrowDown") { e.preventDefault(); move(i, 1); }
            else if (e.key === "ArrowLeft" || e.key === "ArrowUp") { e.preventDefault(); move(i, -1); }
          }}
        >
          {o.icon && <Icon name={o.icon} size={12} />}
          {o.label}
        </button>
      ))}
    </div>
  );
}
