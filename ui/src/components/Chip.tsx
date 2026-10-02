import { useId } from "preact/hooks";
import { Icon } from "./Icon.tsx";
import { useToast } from "./toast-context.ts";

/**
 * A context strip value (key, tempo, meter, bars). `locked` means the host supplies it (a lock
 * icon); `source` says where any other value comes from. Pressing the chip opens an override.
 */
export function ContextChip({ label, value, locked, source, overridden, onClick }: {
  label: string;
  value: string;
  locked?: boolean;
  /** e.g. "the idea", "default"; "host" when locked */
  source?: string;
  overridden?: boolean;
  onClick?: () => void;
}) {
  const from = overridden ? ", overridden" : source ? `, from ${source}` : locked ? ", from host" : "";
  const cls = ["fs-chip", "fs-chip--context", locked && "is-locked", overridden && "is-overridden"].filter(Boolean).join(" ");
  return (
    <button type="button" class={cls} aria-label={`${label}: ${value}${from}`} title={`${label}${from}`} onClick={onClick}>
      <span class="fs-chip__key" aria-hidden="true">{label}</span>
      <span class="fs-chip__value fs-num" aria-hidden="true">{value}</span>
      {locked && <Icon name="lock" size={10} />}
      {overridden && <span class="fs-chip__dot" aria-hidden="true" />}
    </button>
  );
}

/** A suggested prompt under the prompt bar. `unavailable` works as on Button: looks disabled, says why. */
export function SuggestionChip({ children, onClick, unavailable }: { children: string; onClick?: () => void; unavailable?: string | null }) {
  const toast = useToast();
  const reasonId = useId();
  return (
    <>
      <button
        type="button"
        class={unavailable ? "fs-chip fs-chip--suggestion is-unavailable" : "fs-chip fs-chip--suggestion"}
        aria-disabled={unavailable ? true : undefined}
        aria-describedby={unavailable ? reasonId : undefined}
        title={unavailable ?? undefined}
        onClick={unavailable ? () => toast({ message: unavailable }) : onClick}
      >
        <Icon name="sparkle" size={10} />
        {children}
      </button>
      {unavailable && <span id={reasonId} class="sr-only">{unavailable}</span>}
    </>
  );
}

/** Static label, not a control (v1's yellow track chip). */
export function Badge({ children, tone = "accent" }: { children: string; tone?: "accent" | "muted" | "ok" | "warn" | "danger" }) {
  return <span class={`fs-badge fs-badge--${tone}`}>{children}</span>;
}
