# Threading rules

1. **Audio thread:** reads the playhead every block. Captures incoming MIDI into a lock-free ring (last 64 bars). Plays audition from a pre-rendered note list, handed over lock-free (`AuditionHandoff`: the newest clip in, clips no longer played back out for the message thread to free), and writes MIDI out. No allocation, locks, I/O, logging or JSON.
2. **Message thread:** owns the `Session`. Runs `core` re-renders (under 10 ms) and bridge traffic with the WebView. Swaps the audition buffer atomically.
3. **Network worker:** one pool per plugin process for HTTPS and SSE, shared by every instance (`plugin/src/ServiceClient.cpp`). JUCE's HTTP streams block, so each open stream holds one of its 8 threads while it waits; a `generate` runs up to 4 variations. Posts results to the message thread; never touches components directly.
4. **No child processes.** Nothing is forked or spawned inside the host.
5. **Editor lifetime is irrelevant to work.** Closing the editor never cancels a generation or drops state. The processor owns everything.
6. **Shared state:** processor-owned, message-thread-mutated, and snapshotted for `getStateInformation` under a short lock or via an immutable snapshot pointer. Never read a struct that is being mutated.
