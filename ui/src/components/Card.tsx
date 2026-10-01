import type { Session } from "@flowstate/schema";
import type { ComponentChildren } from "preact";
import { Button } from "./Button.tsx";

/** A plain surface for grouped content. */
export function Card({ title, children, class: cls }: { title?: string; children: ComponentChildren; class?: string }) {
  return (
    <section class={cls ? `fs-card ${cls}` : "fs-card"}>
      {title && <h3 class="fs-card__title">{title}</h3>}
      {children}
    </section>
  );
}

type NodeKind = Session["nodes"][number]["kind"];

/** A result in the thread drawer: one lineage node. */
export function ResultCard({ title, kind, meta, current, playing, onPlay, onRestore, onBranch, onDragStart }: {
  title: string;
  kind: NodeKind;
  meta: string;
  current?: boolean;
  playing?: boolean;
  onPlay?: () => void;
  onRestore?: () => void;
  onBranch?: () => void;
  onDragStart?: (e: PointerEvent) => void;
}) {
  return (
    <article class={current ? "fs-card fs-result is-current" : "fs-card fs-result"} aria-current={current ? "true" : undefined} aria-label={title}>
      <div class="fs-result__head">
        <span class={`fs-result__kind fs-result__kind--${kind}`}>{kind}</span>
        <h3 class="fs-result__title">{title}</h3>
      </div>
      <p class="fs-result__meta">{meta}</p>
      <div class="fs-result__actions">
        <Button variant="ghost" size="sm" icon={playing ? "stop" : "play"} label={`Play ${title}`} pressed={playing ?? false} onClick={onPlay} />
        <Button variant="ghost" size="sm" icon="restore" label={`Restore ${title}`} disabled={current} onClick={onRestore} />
        <Button variant="ghost" size="sm" icon="branch" label={`Branch from ${title}`} onClick={onBranch} />
        <Button
          variant="ghost"
          size="sm"
          icon="grip"
          label={`Drag ${title} into your DAW`}
          onPointerDown={(e) => { e.preventDefault(); onDragStart?.(e); }}
        />
      </div>
    </article>
  );
}
