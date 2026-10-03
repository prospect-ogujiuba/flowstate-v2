#include "session/Session.h"

#include "flowstate/ir.h"
#include "flowstate/realize.h"

#include <algorithm>

namespace flowstate::plugin {

fb::Transport HostSnapshot::toTransport() const {
    fb::Transport t;
    t.playing = playing;
    t.recording = recording;
    t.positionPpq = ppq;
    t.tempo = bpm;
    t.meterNumerator = meterNumerator;
    t.meterDenominator = meterDenominator;
    // One bar = numerator beats of 4/denominator quarter notes.
    const double ppqPerBar = meterNumerator * 4.0 / meterDenominator;
    const double bars = ppqPerBar > 0.0 ? std::max(0.0, ppq) / ppqPerBar : 0.0;
    const double wholeBars = static_cast<double>(static_cast<long long>(bars));
    t.bar = static_cast<int>(wholeBars) + 1;
    t.beat = 1.0 + (bars - wholeBars) * meterNumerator;
    if (looping && loopEndPpq > loopStartPpq) t.loop = fb::LoopRange{loopStartPpq, loopEndPpq};
    return t;
}

std::vector<fb::DrumVoiceNote> drumVoiceMap() {
    std::vector<fb::DrumVoiceNote> voices;
    for (int v = 0; v <= static_cast<int>(DrumVoice::Cowbell); ++v) {
        const auto voice = static_cast<DrumVoice>(v);
        const int note = drumVoiceNote(voice);
        voices.push_back({fb::parseDrumVoice(toString(voice)).value_or(fb::DrumVoice::Kick), note, drumSublaneForNote(note)});
    }
    return voices;
}

fb::Clip realizeToClip(const nlohmann::json& score, std::int64_t seed) {
    std::vector<std::string> warnings;
    const Score parsed = parseScore(score.dump(), warnings);
    RealizeOptions options;
    options.seed = static_cast<std::uint64_t>(seed);
    const Realization r = realize(parsed, options);

    fb::Clip clip;
    clip.ppq = r.ppq;
    clip.bars = r.bars;
    clip.ticksPerBar = static_cast<int>(r.ticksPerBar);
    for (const auto& p : r.parts) {
        fb::ClipPart part;
        part.partId = p.id;
        part.role = fb::parseRole(toString(p.role)).value_or(fb::Role::Chords);
        part.name = p.name;
        part.channel = p.channel + 1;
        part.notes.reserve(p.notes.size());
        for (const auto& n : p.notes)
            part.notes.push_back({static_cast<int>(n.tick), static_cast<int>(n.dur), n.pitch, n.vel});
        if (p.role == Role::Drums) part.voices = drumVoiceMap();
        clip.parts.push_back(std::move(part));
    }
    return clip;
}

Session::Session(std::string instanceId, std::string buildId)
    : instanceId_(std::move(instanceId)), buildId_(std::move(buildId)) {}

std::string Session::newNodeId() {
    std::string id;
    do id = "n" + std::to_string(nextNodeNumber_++);
    while (hasNode(id));
    return id;
}

std::string Session::addNode(nlohmann::json score, fb::NodeKind kind, std::optional<std::string> prompt,
                             std::optional<std::vector<std::string>> partIds, std::int64_t seed,
                             std::int64_t createdAtMs, std::optional<std::string> parent,
                             std::optional<std::string> entryId, bool makeCurrent) {
    fb::LineageNode n;
    n.id = newNodeId();
    n.parentId = parent ? parent : currentId_;
    if (n.parentId && !hasNode(*n.parentId)) n.parentId.reset();
    n.kind = kind;
    n.prompt = std::move(prompt);
    n.partIds = std::move(partIds);
    n.createdAtMs = createdAtMs;
    n.seed = seed;
    n.score = std::move(score);
    n.entryId = std::move(entryId);
    // Edits of a library clip's content keep its credit; a fresh plan doesn't.
    const bool derived = kind == fb::NodeKind::Vary || kind == fb::NodeKind::Edit || kind == fb::NodeKind::Tweak ||
                         kind == fb::NodeKind::Touch;
    if (!n.entryId && derived && n.parentId)
        if (const auto* p = node(*n.parentId)) n.entryId = p->entryId;
    const auto id = n.id;
    scoreBytes_[id] = n.score.dump().size();
    nodes_.push_back(std::move(n));
    if (makeCurrent) {
        currentId_ = id;
        redo_.clear();
        refreshClip();
    }
    // The node just made never goes in its own prune.
    const bool wasPinned = pinned_.count(id) > 0;
    pin(id);
    prune();
    if (!wasPinned) unpin(id);
    return id;
}

void Session::addThreadItem(fb::ThreadItem item) {
    thread_.push_back(std::move(item));
    if (thread_.size() > kMaxThreadItems) thread_.erase(thread_.begin(), thread_.end() - static_cast<std::ptrdiff_t>(kMaxThreadItems));
}

std::size_t Session::scoreBytes() const {
    std::size_t total = 0;
    for (const auto& [id, bytes] : scoreBytes_) total += bytes;
    return total;
}

std::size_t Session::prune() {
    std::set<std::string> keep = pinned_;
    std::size_t depth = 0;
    for (const auto* n = current(); n != nullptr && depth <= kUndoDepth; n = n->parentId ? node(*n->parentId) : nullptr, ++depth)
        if (!keep.insert(n->id).second) break;  // never loop, even on a malformed tree
    keep.insert(redo_.begin(), redo_.end());
    if (audition_.nodeId) keep.insert(*audition_.nodeId);

    std::size_t bytes = scoreBytes();
    std::size_t removed = 0;
    while (nodes_.size() > kMaxNodes || bytes > kMaxScoreBytes) {
        const auto victim = std::find_if(nodes_.begin(), nodes_.end(), [&](const fb::LineageNode& n) { return keep.count(n.id) == 0; });
        if (victim == nodes_.end()) break;
        const auto id = victim->id;
        const auto parent = victim->parentId;
        nodes_.erase(victim);
        for (auto& n : nodes_)
            if (n.parentId == id) n.parentId = parent;
        for (auto& t : thread_)
            if (t.nodeId == id) t.nodeId.reset();
        bytes -= scoreBytes_[id];
        scoreBytes_.erase(id);
        ++removed;
    }
    if (thread_.size() > kMaxThreadItems) thread_.erase(thread_.begin(), thread_.end() - static_cast<std::ptrdiff_t>(kMaxThreadItems));
    return removed;
}

bool Session::updateNodeScore(const std::string& id, nlohmann::json score) {
    for (auto& n : nodes_)
        if (n.id == id) {
            n.score = std::move(score);
            scoreBytes_[id] = n.score.dump().size();
            if (currentId_ == id) refreshClip();
            return true;
        }
    return false;
}

bool Session::hasChildren(const std::string& id) const {
    return std::any_of(nodes_.begin(), nodes_.end(), [&](const fb::LineageNode& n) { return n.parentId == id; });
}

bool Session::reparent(const std::string& id, const std::optional<std::string>& parent) {
    if (!hasNode(id) || (parent && !hasNode(*parent))) return false;
    for (auto p = parent; p; p = node(*p)->parentId)
        if (*p == id) return false;
    for (auto& n : nodes_)
        if (n.id == id) n.parentId = parent;
    return true;
}

bool Session::removeNode(const std::string& id) {
    const auto it = std::find_if(nodes_.begin(), nodes_.end(), [&](const fb::LineageNode& n) { return n.id == id; });
    if (it == nodes_.end() || hasChildren(id)) return false;
    const auto parent = it->parentId;
    nodes_.erase(it);
    scoreBytes_.erase(id);
    redo_.erase(std::remove(redo_.begin(), redo_.end(), id), redo_.end());
    if (audition_.nodeId == id) audition_.nodeId.reset();
    if (currentId_ == id) {
        currentId_ = parent;
        refreshClip();
    }
    return true;
}

std::vector<fb::PartState> Session::partStates() const {
    std::vector<fb::PartState> out;
    for (const auto& [id, state] : partStates_) out.push_back(state);
    return out;
}

bool Session::hasNode(const std::string& id) const { return node(id) != nullptr; }

const fb::LineageNode* Session::node(const std::string& id) const {
    for (const auto& n : nodes_)
        if (n.id == id) return &n;
    return nullptr;
}

const fb::LineageNode* Session::current() const { return currentId_ ? node(*currentId_) : nullptr; }

bool Session::select(const std::string& id) {
    if (!hasNode(id)) return false;
    currentId_ = id;
    redo_.clear();
    refreshClip();
    return true;
}

bool Session::canUndo() const {
    const auto* c = current();
    return c != nullptr && c->parentId.has_value();
}

bool Session::undo() {
    if (!canUndo()) return false;
    redo_.push_back(*currentId_);
    currentId_ = current()->parentId;
    refreshClip();
    return true;
}

bool Session::redo() {
    if (redo_.empty()) return false;
    currentId_ = redo_.back();
    redo_.pop_back();
    refreshClip();
    return true;
}

bool Session::setChanged(const std::string& id, std::vector<std::string> partIds) {
    for (auto& n : nodes_)
        if (n.id == id) {
            n.partIds = std::move(partIds);
            return true;
        }
    return false;
}

bool Session::rate(const std::string& id, std::optional<fb::Rating> rating) {
    for (auto& n : nodes_)
        if (n.id == id) {
            n.rating = rating;
            return true;
        }
    return false;
}

bool Session::setPartState(const fb::PartState& state) {
    if (!clip_) return false;
    const bool known = std::any_of(clip_->parts.begin(), clip_->parts.end(),
                                   [&](const fb::ClipPart& p) { return p.partId == state.partId; });
    if (!known) return false;
    partStates_[state.partId] = state;
    return true;
}

std::optional<fb::Clip> Session::realizeNode(const std::string& id, std::string* error) const {
    if (currentId_ && *currentId_ == id) return clip_;
    const auto* n = node(id);
    if (n == nullptr) return std::nullopt;
    try {
        return realizeToClip(n->score, n->seed);
    } catch (const std::exception& e) {
        if (error != nullptr) *error = e.what();
        return std::nullopt;
    }
}

void Session::refreshClip() {
    clip_.reset();
    realizeError_.clear();
    const auto* c = current();
    if (c == nullptr) return;
    try {
        clip_ = realizeToClip(c->score, c->seed);
    } catch (const std::exception& e) {
        realizeError_ = e.what();
    }
}

fb::EffectiveContext Session::effectiveContext(const HostSnapshot& host) const {
    fb::EffectiveContext ctx;
    const nlohmann::json* scoreCtx = nullptr;
    if (const auto* c = current(); c != nullptr && c->score.contains("context")) scoreCtx = &c->score["context"];
    auto fromScore = [&](const char* key) -> const nlohmann::json* {
        if (scoreCtx == nullptr || !scoreCtx->contains(key)) return nullptr;
        return &(*scoreCtx)[key];
    };

    // Key: override, then the current score, then C major.
    ctx.tonic = fb::Tonic::C;
    ctx.mode = fb::Mode::Major;
    ctx.keyFrom = fb::KeySource::Default;
    if (const auto* t = fromScore("tonic"); t && t->is_string())
        if (const auto v = fb::parseTonic(t->get<std::string>())) {
            ctx.tonic = *v;
            ctx.keyFrom = fb::KeySource::Score;
        }
    if (const auto* m = fromScore("mode"); m && m->is_string())
        if (const auto v = fb::parseMode(m->get<std::string>())) ctx.mode = *v;
    if (override_.tonic) {
        ctx.tonic = *override_.tonic;
        ctx.keyFrom = fb::KeySource::Override;
    }
    if (override_.mode) {
        ctx.mode = *override_.mode;
        ctx.keyFrom = fb::KeySource::Override;
    }

    // Tempo and meter: override, then the host, then the current score, then 120 in 4/4.
    ctx.tempo = 120.0;
    ctx.meterNumerator = 4;
    ctx.meterDenominator = 4;
    ctx.timeFrom = fb::TimeSource::Default;
    if (const auto* t = fromScore("tempo"); t && t->is_number()) {
        ctx.tempo = t->get<double>();
        if (const auto* n = fromScore("meterNumerator"); n && n->is_number_integer()) ctx.meterNumerator = n->get<int>();
        if (const auto* d = fromScore("meterDenominator"); d && d->is_number_integer()) ctx.meterDenominator = d->get<int>();
        ctx.timeFrom = fb::TimeSource::Score;
    }
    if (host.hasHost) {
        ctx.tempo = host.bpm;
        ctx.meterNumerator = host.meterNumerator;
        ctx.meterDenominator = host.meterDenominator;
        ctx.timeFrom = fb::TimeSource::Host;
    }
    if (override_.tempo || override_.meterNumerator || override_.meterDenominator) ctx.timeFrom = fb::TimeSource::Override;
    if (override_.tempo) ctx.tempo = *override_.tempo;
    if (override_.meterNumerator) ctx.meterNumerator = *override_.meterNumerator;
    if (override_.meterDenominator) ctx.meterDenominator = *override_.meterDenominator;

    // Bars: override, then the current score, then 4.
    ctx.bars = 4;
    if (const auto* b = fromScore("bars"); b && b->is_number_integer()) ctx.bars = std::clamp(b->get<int>(), 1, 64);
    if (override_.bars) ctx.bars = *override_.bars;
    return ctx;
}

fb::Session Session::view(const HostSnapshot& host, int captureBars) const {
    fb::Session s;
    s.instanceId = instanceId_;
    s.override = override_;
    s.context = effectiveContext(host);
    s.captureBars = std::clamp(captureBars, 0, 64);
    for (const auto& n : nodes_) {
        fb::NodeSummary summary;
        summary.id = n.id;
        summary.parentId = n.parentId;
        summary.kind = n.kind;
        summary.prompt = n.prompt;
        summary.partIds = n.partIds;
        summary.title = n.score.contains("title") && n.score["title"].is_string() ? n.score["title"].get<std::string>() : "";
        summary.createdAtMs = n.createdAtMs;
        summary.rating = n.rating;
        summary.entryId = n.entryId;
        s.nodes.push_back(std::move(summary));
    }
    s.currentNodeId = currentId_;
    s.canUndo = canUndo();
    s.canRedo = canRedo();
    s.thread = thread_;
    if (clip_)
        for (const auto& p : clip_->parts) {
            const auto it = partStates_.find(p.partId);
            s.parts.push_back(it != partStates_.end() ? it->second : fb::PartState{p.partId, false, false, false, 0.5});
        }
    s.clip = clip_;
    s.audition = audition_;
    s.midiOut = midiOut_;
    s.settings.provider = provider_;
    s.settings.byokEnabled = false;  // P1-12 wires the release flag
    s.settings.hasKey = false;
    s.settings.previewSynth = previewSynth_;
    s.settings.buildId = buildId_;
    s.preview = preview_;
    s.generations = generations_;
    return s;
}

fb::SavedSession Session::save() const {
    fb::SavedSession s;
    s.instanceId = instanceId_;
    s.override = override_;
    s.nodes = nodes_;
    s.currentNodeId = currentId_;
    s.redo = redo_;
    s.thread = thread_;
    for (const auto& [id, state] : partStates_) s.parts.push_back(state);
    s.audition = audition_;
    s.midiOut = midiOut_;
    s.previewSynth = previewSynth_;
    return s;
}

void Session::restore(const fb::SavedSession& saved, std::vector<std::string>& warnings) {
    instanceId_ = saved.instanceId;
    override_ = saved.override;
    nodes_ = saved.nodes;
    thread_ = saved.thread;
    audition_ = saved.audition;
    midiOut_ = saved.midiOut;
    previewSynth_ = saved.previewSynth;
    partStates_.clear();
    for (const auto& p : saved.parts) partStates_[p.partId] = p;

    currentId_ = saved.currentNodeId;
    if (currentId_ && !hasNode(*currentId_)) {
        warnings.push_back("current node " + *currentId_ + " is missing; nothing selected");
        currentId_.reset();
    }
    redo_.clear();
    for (const auto& id : saved.redo) {
        if (hasNode(id)) redo_.push_back(id);
        else warnings.push_back("redo node " + id + " is missing; dropped");
    }
    for (auto& n : nodes_)
        if (n.parentId && !hasNode(*n.parentId)) {
            warnings.push_back("node " + n.id + " has a missing parent; now a root");
            n.parentId.reset();
        }
    for (auto& t : thread_)
        if (t.nodeId && !hasNode(*t.nodeId)) {
            warnings.push_back("thread item " + t.id + " names a missing node; kept as text");
            t.nodeId.reset();
        }
    if (audition_.nodeId && !hasNode(*audition_.nodeId)) {
        warnings.push_back("auditioned node " + *audition_.nodeId + " is missing; auditioning the current node");
        audition_.nodeId.reset();
    }
    pinned_.clear();
    scoreBytes_.clear();
    for (const auto& n : nodes_) scoreBytes_[n.id] = n.score.dump().size();
    if (const auto pruned = prune(); pruned > 0)
        warnings.push_back("the saved lineage was over its size bound; removed the " + std::to_string(pruned) + " oldest nodes");
    // Ids continue after the highest restored "n<number>", so a removed id is never reused.
    nextNodeNumber_ = 1;
    for (const auto& n : nodes_)
        if (n.id.size() > 1 && n.id[0] == 'n' && n.id.size() < 18 && n.id.find_first_not_of("0123456789", 1) == std::string::npos)
            nextNodeNumber_ = std::max<std::uint64_t>(nextNodeNumber_, std::stoull(n.id.substr(1)) + 1);
    refreshClip();
    if (!realizeError_.empty()) warnings.push_back("current node could not be realized: " + realizeError_);
}

}  // namespace flowstate::plugin
