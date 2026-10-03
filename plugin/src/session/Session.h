// The plugin's Session: context override, lineage of score IRs, thread, part states and output
// choices. Processor-owned and mutated on the message thread only (docs/threading.md). JUCE-free,
// so it is unit-tested without a host. Wire types come from the generated bridge
// (schema/cpp/include/flowstate/bridge.h); theory comes from core.
#pragma once

#include "flowstate/bridge.h"

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace flowstate::plugin {

namespace fb = flowstate::bridge;

// What the host told the audio thread most recently (see HostSync.h).
struct HostSnapshot {
    bool hasHost = false;  // the host provides a playhead with a PPQ position
    bool playing = false;
    bool recording = false;
    double ppq = 0.0;
    double bpm = 120.0;
    int meterNumerator = 4;
    int meterDenominator = 4;
    bool looping = false;
    double loopStartPpq = 0.0;
    double loopEndPpq = 0.0;

    fb::Transport toTransport() const;
};

class Session {
public:
    Session(std::string instanceId, std::string buildId);

    // ---- Lineage -------------------------------------------------------------------------------
    // Adds a node under `parent` (default: the current node), makes it current and clears redo.
    // Returns its id. The score must be valid IR; realization problems surface in clip().
    // `entryId` names the library clip a node starts from. A vary, edit, tweak or touch node without
    // one inherits its parent's, so the clip's credit stays with what is made from it.
    // With `makeCurrent` false the node joins the lineage without moving the selection or redo
    // (a variation that lands while the user looks at another idea).
    std::string addNode(nlohmann::json score, fb::NodeKind kind, std::optional<std::string> prompt,
                        std::optional<std::vector<std::string>> partIds, std::int64_t seed, std::int64_t createdAtMs,
                        std::optional<std::string> parent = std::nullopt, std::optional<std::string> entryId = std::nullopt,
                        bool makeCurrent = true);
    // Replaces a node's score: a streamed plan grows part by part, and its final score replaces
    // what streamed. Only for nodes a running request owns. Re-realizes if the node is current.
    bool updateNodeScore(const std::string& id, nlohmann::json score);
    // Removes a node without children (a streamed plan that failed). If it was current, its parent
    // becomes current. False if it is unknown or has children.
    bool removeNode(const std::string& id);
    // Moves a node under another (null = a root). False if either is unknown or it would make a cycle.
    bool reparent(const std::string& id, const std::optional<std::string>& parent);
    bool hasChildren(const std::string& id) const;

    bool hasNode(const std::string& id) const;
    const fb::LineageNode* node(const std::string& id) const;
    const fb::LineageNode* current() const;
    const std::vector<fb::LineageNode>& nodes() const { return nodes_; }

    // Restore / A-B. Clears redo.
    bool select(const std::string& id);
    // Undo walks to the current node's parent; redo walks back down the path undo came up.
    bool canUndo() const;
    bool canRedo() const { return !redo_.empty(); }
    bool undo();
    bool redo();
    bool rate(const std::string& id, std::optional<fb::Rating> rating);

    // ---- Bounds (P1-11) ------------------------------------------------------------------------
    // The lineage and thread stay small enough that plugin state is well under 1 MB. Past a bound,
    // the oldest nodes go first, except the current node and its last kUndoDepth ancestors, the
    // redo path, the auditioned node and pinned nodes. A removed node's children move up to its
    // parent (so undo skips it), and thread items that named it keep their text. The oldest thread
    // items go past their bound.
    static constexpr std::size_t kMaxNodes = 200;
    static constexpr std::size_t kUndoDepth = 50;
    static constexpr std::size_t kMaxScoreBytes = 640 * 1024;
    static constexpr std::size_t kMaxThreadItems = 400;
    // A running request pins the node it started from and the nodes it streams into.
    void pin(const std::string& id) { pinned_.insert(id); }
    void unpin(const std::string& id) { pinned_.erase(id); }
    // Applies the bounds; returns how many nodes it removed. addNode and restore call it.
    std::size_t prune();
    // Sum of the stored scores' JSON sizes.
    std::size_t scoreBytes() const;

    // ---- Parts and settings --------------------------------------------------------------------
    // Part state for a part of the current score; false if the part isn't there.
    bool setPartState(const fb::PartState& state);
    void setOverride(const fb::ContextOverride& o) { override_ = o; }
    const fb::ContextOverride& contextOverride() const { return override_; }
    void setAudition(const fb::Audition& a) { audition_ = a; }
    const fb::Audition& audition() const { return audition_; }
    void setMidiOut(const fb::MidiOut& m) { midiOut_ = m; }
    const fb::MidiOut& midiOut() const { return midiOut_; }
    void setPreviewSynth(bool on) { previewSynth_ = on; }
    bool previewSynth() const { return previewSynth_; }
    void setProvider(std::optional<fb::ProviderChoice> p) { provider_ = std::move(p); }
    void addThreadItem(fb::ThreadItem item);
    // The catalog entry previewing in time with the host (not saved with the project).
    void setPreview(std::optional<std::string> entryId) { preview_ = std::move(entryId); }
    const std::optional<std::string>& preview() const { return preview_; }
    // Running model requests, for the UI (not saved with the project).
    void setGenerations(std::vector<fb::Generation> g) { generations_ = std::move(g); }
    const std::optional<fb::ProviderChoice>& provider() const { return provider_; }
    const std::vector<fb::ThreadItem>& thread() const { return thread_; }
    // Part states as set (parts never touched have none).
    std::vector<fb::PartState> partStates() const;

    const std::string& instanceId() const { return instanceId_; }

    // ---- Realization ---------------------------------------------------------------------------
    // The current node's realization (cached; null when there is no current node or it failed).
    const std::optional<fb::Clip>& clip() const { return clip_; }
    const std::string& realizeError() const { return realizeError_; }
    // Any node's realization, e.g. for dragging a thread card that isn't current.
    std::optional<fb::Clip> realizeNode(const std::string& id, std::string* error = nullptr) const;

    // ---- Views and persistence -----------------------------------------------------------------
    fb::EffectiveContext effectiveContext(const HostSnapshot& host) const;
    fb::Session view(const HostSnapshot& host, int captureBars) const;

    fb::SavedSession save() const;
    // Replaces everything with a saved session. Unknown references (a current id or redo id that
    // isn't a node) are dropped rather than failing the restore; the reasons go to `warnings`.
    void restore(const fb::SavedSession& saved, std::vector<std::string>& warnings);

private:
    void refreshClip();
    std::string newNodeId();

    std::string instanceId_;
    std::string buildId_;
    fb::ContextOverride override_{};
    std::vector<fb::LineageNode> nodes_;
    std::optional<std::string> currentId_;
    std::vector<std::string> redo_;
    std::vector<fb::ThreadItem> thread_;
    std::map<std::string, fb::PartState> partStates_;
    fb::Audition audition_{};
    fb::MidiOut midiOut_{};
    bool previewSynth_ = true;
    std::optional<fb::ProviderChoice> provider_;
    std::optional<std::string> preview_;
    std::vector<fb::Generation> generations_;
    std::uint64_t nextNodeNumber_ = 1;
    std::set<std::string> pinned_;
    std::map<std::string, std::size_t> scoreBytes_;  // per node, so pruning doesn't re-serialize

    std::optional<fb::Clip> clip_;
    std::string realizeError_;
};

// Realizes one score IR with `seed` into the bridge's Clip. Throws flowstate::IrError.
fb::Clip realizeToClip(const nlohmann::json& score, std::int64_t seed);

// The fixed drum voice -> GM note -> v1 sublane table a drum ClipPart carries.
std::vector<fb::DrumVoiceNote> drumVoiceMap();

}  // namespace flowstate::plugin
