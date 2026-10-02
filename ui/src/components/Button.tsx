import type { ComponentChildren, JSX } from "preact";
import { useId } from "preact/hooks";
import { Icon } from "./Icon.tsx";
import { useToast } from "./toast-context.ts";

export type ButtonVariant = "primary" | "secondary" | "ghost" | "danger" | "ok" | "nav";

type Props = Omit<JSX.ButtonHTMLAttributes<HTMLButtonElement>, "icon" | "label" | "size"> & {
  variant?: ButtonVariant;
  size?: "md" | "sm";
  icon?: string;
  /** Accessible name. Required when there is no visible text (icon-only), and overrides short visible text ("S"). */
  label?: string;
  /** Makes it a toggle button (aria-pressed). */
  pressed?: boolean;
  /** Keeps a nav or segment button lit (v1's "active" state). */
  active?: boolean;
  disabled?: boolean;
  /**
   * Why this build can't do it (Session.unavailable). The button looks disabled but stays
   * focusable; its description and tooltip carry the reason, and pressing it shows the reason
   * instead of running.
   */
  unavailable?: string | null;
  children?: ComponentChildren;
};

export function Button({ variant = "secondary", size = "md", icon, label, pressed, active, unavailable, children, class: cls, onClick, ...rest }: Props) {
  const iconOnly = children === undefined || children === null;
  if (iconOnly && !label) throw new Error("an icon-only Button needs a label");
  const toast = useToast();
  const reasonId = useId();
  const classes = ["fs-btn", `fs-btn--${variant}`, `fs-btn--${size}`, iconOnly && "fs-btn--icon", active && "is-active", unavailable && "is-unavailable", cls]
    .filter(Boolean)
    .join(" ");
  // The reason sits next to the button, not inside it, so it never joins a visible-text name.
  return (
    <>
      <button
        type="button"
        class={classes}
        aria-label={label}
        title={unavailable ? (label ? `${label}: ${unavailable}` : unavailable) : label}
        aria-pressed={pressed}
        aria-disabled={unavailable ? true : undefined}
        aria-describedby={unavailable ? reasonId : undefined}
        {...rest}
        onClick={unavailable ? () => toast({ message: unavailable }) : onClick}
        onPointerDown={unavailable ? undefined : rest.onPointerDown}
      >
        {icon && <Icon name={icon} size={size === "sm" ? 12 : 14} />}
        {!iconOnly && <span>{children}</span>}
      </button>
      {unavailable && <span id={reasonId} class="sr-only">{unavailable}</span>}
    </>
  );
}
