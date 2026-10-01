import type { ComponentChildren, JSX } from "preact";
import { Icon } from "./Icon.tsx";

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
  children?: ComponentChildren;
};

export function Button({ variant = "secondary", size = "md", icon, label, pressed, active, children, class: cls, ...rest }: Props) {
  const iconOnly = children === undefined || children === null;
  if (iconOnly && !label) throw new Error("an icon-only Button needs a label");
  const classes = ["fs-btn", `fs-btn--${variant}`, `fs-btn--${size}`, iconOnly && "fs-btn--icon", active && "is-active", cls]
    .filter(Boolean)
    .join(" ");
  return (
    <button
      type="button"
      class={classes}
      aria-label={label}
      title={label}
      aria-pressed={pressed}
      {...rest}
    >
      {icon && <Icon name={icon} size={size === "sm" ? 12 : 14} />}
      {!iconOnly && <span>{children}</span>}
    </button>
  );
}
