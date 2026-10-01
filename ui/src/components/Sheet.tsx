import type { ComponentChildren } from "preact";
import { useEffect, useId, useRef } from "preact/hooks";
import { Button } from "./Button.tsx";

/**
 * Modal sheet (settings, sign-in). A native <dialog>: the page behind is inert, Tab stays inside,
 * Escape and the close button close it, and focus returns to whatever opened it.
 */
export function Sheet({ open, title, onClose, children, footer }: {
  open: boolean;
  title: string;
  onClose: () => void;
  children: ComponentChildren;
  footer?: ComponentChildren;
}) {
  const ref = useRef<HTMLDialogElement>(null);
  const opener = useRef<HTMLElement | null>(null);
  const titleId = useId();

  useEffect(() => {
    const d = ref.current;
    if (!d) return;
    if (open && !d.open) {
      opener.current = document.activeElement instanceof HTMLElement ? document.activeElement : null;
      d.showModal();
    } else if (!open && d.open) {
      d.close();
    }
  }, [open]);

  return (
    <dialog
      ref={ref}
      class="fs-sheet"
      aria-labelledby={titleId}
      onCancel={(e) => { e.preventDefault(); onClose(); }}
      onClose={() => { opener.current?.focus(); opener.current = null; }}
      onClick={(e) => { if (e.target === ref.current) onClose(); }}
    >
      <div class="fs-sheet__panel">
        <header class="fs-sheet__head">
          <h2 id={titleId} class="fs-sheet__title">{title}</h2>
          <Button variant="ghost" size="sm" icon="x" label="Close" class="fs-sheet__close" onClick={onClose} />
        </header>
        <div class="fs-sheet__body">{children}</div>
        {footer && <footer class="fs-sheet__foot">{footer}</footer>}
      </div>
    </dialog>
  );
}

/** A titled group inside a sheet (v1's "AI CONFIGURATION", "ACCOUNT"). */
export function SheetSection({ title, children }: { title: string; children: ComponentChildren }) {
  const id = useId();
  return (
    <section class="fs-sheet__section" aria-labelledby={id}>
      <h3 id={id} class="fs-sheet__section-title">{title}</h3>
      {children}
    </section>
  );
}
