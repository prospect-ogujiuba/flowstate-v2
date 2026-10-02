import type { Command, PluginEvent, Reply } from "@flowstate/schema";

/** The plugin, as the UI sees it (docs/bridge-spec.md): one command function and one event stream. */
export interface Bridge {
  send(command: Command): Promise<Reply>;
  onEvent(fn: (event: PluginEvent) => void): void;
}
