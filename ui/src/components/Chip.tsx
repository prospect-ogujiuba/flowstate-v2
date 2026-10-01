import { Icon } from "./Icon.tsx";

/**
 * A context strip value (key, tempo, meter, bars). `locked` means the host or the score supplies
 * it; pressing the chip opens an override.
 */
export function ContextChip({ label, value, locked, onClick }: {
  label: string;
  value: string;
  locked?: boolean;
  onClick?: () => void;
}) {
  return (
    <button
      type="button"
      class={locked ? "fs-chip fs-chip--context is-locked" : "fs-chip fs-chip--context"}
      aria-label={`${label}: ${value}${locked ? ", from host" : ""}`}
      onClick={onClick}
    >
      <span class="fs-chip__key" aria-hidden="true">{label}</span>
      <span class="fs-chip__value fs-num" aria-hidden="true">{value}</span>
      {locked && <Icon name="lock" size={10} />}
    </button>
  );
}

/** A suggested prompt under the prompt bar. */
export function SuggestionChip({ children, onClick }: { children: string; onClick?: () => void }) {
  return (
    <button type="button" class="fs-chip fs-chip--suggestion" onClick={onClick}>
      <Icon name="sparkle" size={10} />
      {children}
    </button>
  );
}

/** Static label, not a control (v1's yellow track chip). */
export function Badge({ children, tone = "accent" }: { children: string; tone?: "accent" | "muted" | "ok" | "warn" | "danger" }) {
  return <span class={`fs-badge fs-badge--${tone}`}>{children}</span>;
}
