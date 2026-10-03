// The thread drawer (v1's Chats drawer): the conversation as result cards. Each card is a lineage
// node with play, restore, branch and drag; text-only answers stay plain text.

import type { Session } from "@flowstate/schema";
import { useEffect, useRef } from "preact/hooks";
import { Button, ResultCard } from "../components/index.ts";
import { ago, KIND_LABELS } from "./clip.ts";
import type { Studio } from "./useStudio.ts";

type Node = Session["nodes"][number];
type Item =
  | { type: "text"; id: string; role: "user" | "assistant"; text: string; at: number }
  | { type: "card"; id: string; node: Node; at: number };

/** Thread items in order, plus nodes no thread item names (library clips, tweaks), at their time. */
function timeline(session: Session): Item[] {
  const byId = new Map(session.nodes.map((n) => [n.id, n]));
  const named = new Set<string>();
  const items: Item[] = [];
  for (const t of session.thread) {
    const node = t.nodeId ? byId.get(t.nodeId) : undefined;
    if (node) {
      named.add(node.id);
      if (t.text && t.text !== node.title) items.push({ type: "text", id: `${t.id}-text`, role: t.role, text: t.text, at: t.createdAtMs });
      items.push({ type: "card", id: t.id, node, at: t.createdAtMs });
    } else {
      items.push({ type: "text", id: t.id, role: t.role, text: t.text || "(empty prompt: surprise me)", at: t.createdAtMs });
    }
  }
  // A sketch shows only while it's the idea playing (it stays after a generation that didn't finish).
  for (const n of session.nodes) if (!named.has(n.id) && (n.kind !== "sketch" || n.id === session.currentNodeId)) items.push({ type: "card", id: `node-${n.id}`, node: n, at: n.createdAtMs });
  return items.sort((a, b) => a.at - b.at);
}

export function Thread({ open, studio, session, credits, splitDrums, onBranch, onClose }: {
  open: boolean;
  studio: Studio;
  session: Session;
  credits: Map<string, string>;
  splitDrums: boolean;
  onBranch: (node: Node) => void;
  onClose: () => void;
}) {
  const { send } = studio;
  const list = useRef<HTMLOListElement>(null);
  const items = timeline(session);
  useEffect(() => { if (open) list.current?.lastElementChild?.scrollIntoView?.({ block: "nearest" }); }, [items.length, open]);

  const meta = (n: Node) => {
    const bits = [KIND_LABELS[n.kind]];
    if (n.partIds) bits.push(`${n.partIds.length} part${n.partIds.length > 1 ? "s" : ""} changed`);
    if (n.entryId) bits.push(credits.get(n.entryId) ?? "Library clip");
    bits.push(ago(n.createdAtMs));
    return bits.join(" · ");
  };

  return (
    <aside id="st-thread" class="st-thread" aria-labelledby="st-thread-h" hidden={!open}>
      <header class="st-thread__head">
        <h2 id="st-thread-h" class="st-thread__title">Thread</h2>
        <Button variant="ghost" size="sm" icon="x" label="Close thread" onClick={onClose} />
      </header>
      {items.length === 0 ? (
        <p class="st-note st-thread__empty">Every result lands here as a card: play it, restore it, branch from it or drag it out.</p>
      ) : (
        <ol ref={list} class="st-thread__list">
          {items.map((it) => (
            <li key={it.id}>
              {it.type === "text" ? (
                <p class={`st-msg st-msg--${it.role}`}>{it.text}</p>
              ) : (
                <ResultCard
                  title={it.node.title || "Untitled idea"}
                  kind={it.node.kind}
                  meta={meta(it.node)}
                  current={it.node.id === session.currentNodeId}
                  playing={session.audition.nodeId === it.node.id}
                  onPlay={() => void send({ type: "setAudition", audition: { ...session.audition, nodeId: session.audition.nodeId === it.node.id ? null : it.node.id } })}
                  onRestore={() => void send({ type: "selectNode", nodeId: it.node.id })}
                  onBranch={() => onBranch(it.node)}
                  onDragStart={() => void send({ type: "startDrag", nodeId: it.node.id, partIds: null, splitDrums })}
                />
              )}
            </li>
          ))}
        </ol>
      )}
    </aside>
  );
}
