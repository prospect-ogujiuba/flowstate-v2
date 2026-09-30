// Keys that belong to the host (spike P0-7, docs/threading.md). Space with no text entry focused
// goes to the DAW's transport, so controls activate with Enter, never Space. Escape in a text
// entry hands focus back too, unless a sheet is open (Escape closes the sheet first).

export type ReleaseReason = "space" | "escape" | "blur";

export function isTextEntry(el: Element | null): boolean {
  if (!(el instanceof HTMLElement)) return false;
  if (el.isContentEditable || el instanceof HTMLTextAreaElement || el instanceof HTMLSelectElement) return true;
  return el instanceof HTMLInputElement && !["button", "checkbox", "radio", "range", "submit", "reset"].includes(el.type);
}

export function installHostKeys(release: (reason: ReleaseReason) => void): () => void {
  const onKey = (e: KeyboardEvent) => {
    const active = document.activeElement;
    const typing = isTextEntry(active);
    const inSheet = active?.closest("dialog[open]") != null;
    if ((e.code === "Space" && !typing) || (e.key === "Escape" && typing && !inSheet)) {
      e.preventDefault();
      if (active instanceof HTMLElement) active.blur();
      release(e.code === "Space" ? "space" : "escape");
    }
  };
  document.addEventListener("keydown", onKey, true);
  return () => document.removeEventListener("keydown", onKey, true);
}
