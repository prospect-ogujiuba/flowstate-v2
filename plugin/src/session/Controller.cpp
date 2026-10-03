#include "session/Controller.h"

#include "flowstate/analyze.h"
#include "flowstate/sketch.h"
#include "flowstate/theory.h"

#include <algorithm>
#include <cctype>
#include <type_traits>

namespace flowstate::plugin {

namespace {

template <class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

MidiMeta metaOf(const fb::LineageNode& node) {
    MidiMeta meta;
    // The credit is filled in by Controller::metaFor.
    const auto& s = node.score;
    const auto t = s.find("title");
    meta.title = t != s.end() && t->is_string() && !t->get<std::string>().empty() ? t->get<std::string>() : "Flowstate idea";
    if (const auto c = s.find("context"); c != s.end() && c->is_object()) {
        if (const auto v = c->find("tempo"); v != c->end() && v->is_number()) meta.tempo = v->get<double>();
        if (const auto v = c->find("meterNumerator"); v != c->end() && v->is_number_integer()) meta.meterNumerator = v->get<int>();
        if (const auto v = c->find("meterDenominator"); v != c->end() && v->is_number_integer()) meta.meterDenominator = v->get<int>();
    }
    return meta;
}

}  // namespace

const Library* Controller::library() {
    if (library_) return &*library_;
    if (!libraryError_.empty()) return nullptr;
    const auto bytes = platform_.libraryResource("catalog.json");
    if (!bytes) {
        library_.emplace();
        return &*library_;
    }
    try {
        library_ = Library::parse(std::string(bytes->begin(), bytes->end()));
    } catch (const std::exception& e) {
        libraryError_ = std::string("The built-in library is damaged: ") + e.what();
        return nullptr;
    }
    return &*library_;
}

MidiMeta Controller::metaFor(const fb::LineageNode& node) {
    MidiMeta meta = metaOf(node);
    if (node.entryId)
        if (const auto* lib = library())
            if (const auto* clip = lib->find(*node.entryId); clip && clip->entry.credit) meta.credit = clip->entry.credit->text;
    return meta;
}

Controller::AuditionSource Controller::audition() {
    AuditionSource out;
    if (const auto& entry = session_.preview()) {
        if (entry->rfind("node:", 0) == 0) {
            out.clip = session_.realizeNode(entry->substr(5));
        } else if (const auto* lib = library()) {
            if (const auto* clip = lib->find(*entry)) {
                try {
                    out.clip = realizeToClip(clip->score, 1);
                } catch (const std::exception&) {
                }
            }
        }
        return out;
    }
    const auto& a = session_.audition();
    out.clip = a.nodeId ? session_.realizeNode(*a.nodeId) : session_.clip();
    out.filter.loop = a.loop;
    out.filter.role = session_.midiOut().role;
    out.filter.parts = session_.partStates();
    return out;
}

const std::vector<fb::FeatureGap>& Controller::featureGaps() {
    using F = fb::Feature;
    static const std::vector<fb::FeatureGap> gaps{
        {F::Reroll, "Re-roll needs the realizer to make seeded choices (voicing, rhythm), which isn't built yet."},
        {F::Tweak, "Local transforms aren't in core yet."},
        {F::EditNotes, "Note edits aren't in core yet."},
        {F::Density, "The density knob doesn't change playback until core has the transform."},
    };
    return gaps;
}

namespace {
const fb::FeatureGap kNoKeychain{fb::Feature::ApiKey, "This build has no OS keychain to keep your key in."};
}  // namespace

std::vector<fb::FeatureGap> Controller::gaps() const {
    auto out = featureGaps();
    if (platform_.keyStore() == nullptr) out.push_back(kNoKeychain);
    return out;
}

const std::string& Controller::gapReason(fb::Feature feature) const {
    if (feature == fb::Feature::ApiKey && platform_.keyStore() == nullptr) return kNoKeychain.reason;
    for (const auto& g : featureGaps())
        if (g.feature == feature) return g.reason;
    static const std::string none;
    return none;
}

fb::Session Controller::view() const {
    auto s = session_.view(platform_.host(), platform_.captureBars());
    s.unavailable = gaps();
    s.settings.byokEnabled = byok_;
    s.settings.hasKey = s.settings.provider && hasKey(s.settings.provider->provider);
    return s;
}

// ---- Keys (P1-12) --------------------------------------------------------------------------------

void Controller::serviceFeatures(const fb::ServiceFeatures& features) {
    if (features.byok == byok_) return;
    byok_ = features.byok;
    if (onChanged) onChanged();
}

bool Controller::hasKey(const std::string& provider) const {
    auto* store = platform_.keyStore();
    if (store == nullptr) return false;
    const auto it = hasKey_.find(provider);
    if (it != hasKey_.end()) return it->second;
    return hasKey_[provider] = store->contains(provider);
}

std::optional<std::string> Controller::providerKey() const {
    const auto& provider = session_.provider();
    auto* store = platform_.keyStore();
    if (!byok_ || !provider || store == nullptr) return std::nullopt;
    // The keychain is the authority: another instance may have stored or removed the key.
    auto key = store->read(provider->provider);
    hasKey_[provider->provider] = key.has_value();
    return key;
}

fb::Reply Controller::setApiKey(const fb::SetApiKey& c) {
    // Errors never quote the key or the command.
    auto* store = platform_.keyStore();
    if (store == nullptr) return fail(fb::ErrorCode::Unavailable, gapReason(fb::Feature::ApiKey));
    if (!validProviderId(c.provider)) return fail(fb::ErrorCode::BadRequest, "Choose a provider for the key.");
    if (c.key) {
        if (!byok_) return fail(fb::ErrorCode::Unavailable, "Using your own key is turned off.");
        if (!validKey(*c.key))
            return fail(fb::ErrorCode::BadRequest, "That doesn't look like an API key: it should be one word of letters, digits and symbols.");
        if (auto e = store->write(c.provider, *c.key)) return fail(fb::ErrorCode::Internal, *e);
    } else if (auto e = store->remove(c.provider)) {
        return fail(fb::ErrorCode::Internal, *e);
    }
    hasKey_[c.provider] = c.key.has_value();
    return ok(true);
}

fb::Reply Controller::ok(bool changed) {
    fb::Reply r;
    r.ok = true;
    if (changed) {
        r.session = view();
        if (onChanged) onChanged();
    }
    return r;
}

fb::Reply Controller::fail(fb::ErrorCode code, std::string message) {
    fb::Reply r;
    r.ok = false;
    r.error = fb::ErrorInfo{code, std::move(message)};
    return r;
}

fb::Reply Controller::handle(const fb::Command& command) {
    const auto unknownNode = [](const std::string& id) { return fail(fb::ErrorCode::UnknownNode, "No node " + id + "."); };
    const auto unknownPart = [](const std::string& id) { return fail(fb::ErrorCode::UnknownPart, "No part " + id + " in the current idea."); };
    const auto unavailable = [this](fb::Feature feature) { return fail(fb::ErrorCode::Unavailable, gapReason(feature)); };

    // Resolves the node a drag or export is about: an explicit id, or the current node.
    const auto clipFor = [this](const std::optional<std::string>& nodeId, fb::Reply& error,
                                MidiMeta& meta) -> std::optional<fb::Clip> {
        const auto* node = nodeId ? session_.node(*nodeId) : session_.current();
        if (node == nullptr) {
            error = nodeId ? fail(fb::ErrorCode::UnknownNode, "No node " + *nodeId + ".")
                           : fail(fb::ErrorCode::BadRequest, "There is no idea to drag yet.");
            return std::nullopt;
        }
        std::string why;
        auto clip = session_.realizeNode(node->id, &why);
        if (!clip) error = fail(fb::ErrorCode::InvalidScore, "This idea can't be realized: " + why);
        meta = metaFor(*node);
        return clip;
    };

    return std::visit(
        Overloaded{
            [&](const fb::Hello&) {
                // The release flags and the keychain may have changed since the last editor.
                platform_.checkService();
                hasKey_.clear();
                fb::Reply r;
                r.ok = true;
                r.session = view();
                return r;
            },
            [&](const fb::Generate& c) { return generate(c); },
            [&](const fb::Edit& c) { return startEdit(fb::EditKind::Edit, c.prompt, c.partIds, std::nullopt, c.prompt); },
            [&](const fb::Vary& c) {
                std::string name = c.partId;
                if (const auto* cur = session_.current())
                    for (const auto& p : cur->score.value("parts", nlohmann::json::array()))
                        if (p.value("id", "") == c.partId) name = p.value("name", c.partId);
                return startEdit(fb::EditKind::Vary, "", std::vector<std::string>{c.partId}, std::nullopt, "Vary " + name);
            },
            [&](const fb::AddPart& c) {
                const auto prompt = c.prompt.value_or("");
                return startEdit(fb::EditKind::AddPart, prompt, std::vector<std::string>{}, c.role,
                                 prompt.empty() ? std::string("Add a ") + fb::toString(c.role) + " part" : prompt);
            },
            [&](const fb::Reroll&) { return unavailable(fb::Feature::Reroll); },
            [&](const fb::Tweak&) { return unavailable(fb::Feature::Tweak); },
            [&](const fb::EditNotes&) { return unavailable(fb::Feature::EditNotes); },
            [&](const fb::RemovePart& c) {
                const auto* cur = session_.current();
                if (cur == nullptr) return unknownPart(c.partId);
                auto score = cur->score;
                auto& parts = score["parts"];
                const auto before = parts.size();
                parts.erase(std::remove_if(parts.begin(), parts.end(),
                                           [&](const nlohmann::json& p) { return p.value("id", "") == c.partId; }),
                            parts.end());
                if (parts.size() == before) return unknownPart(c.partId);
                session_.addNode(std::move(score), fb::NodeKind::Tweak, std::nullopt,
                                 std::vector<std::string>{c.partId}, cur->seed, platform_.nowMs());
                return ok(true);
            },
            [&](const fb::Cancel& c) { return cancel(c.requestId); },
            [&](const fb::SelectNode& c) { return session_.select(c.nodeId) ? ok(true) : unknownNode(c.nodeId); },
            [&](const fb::Undo&) {
                return session_.undo() ? ok(true) : fail(fb::ErrorCode::BadRequest, "Nothing to undo.");
            },
            [&](const fb::Redo&) {
                return session_.redo() ? ok(true) : fail(fb::ErrorCode::BadRequest, "Nothing to redo.");
            },
            [&](const fb::RateNode& c) { return session_.rate(c.nodeId, c.rating) ? ok(true) : unknownNode(c.nodeId); },
            [&](const fb::SetPartState& c) {
                return session_.setPartState(c.state) ? ok(true) : unknownPart(c.state.partId);
            },
            [&](const fb::SetContextOverride& c) {
                session_.setOverride(c.override);
                return ok(true);
            },
            [&](const fb::SetAudition& c) {
                if (c.audition.nodeId && !session_.hasNode(*c.audition.nodeId)) return unknownNode(*c.audition.nodeId);
                if (c.audition.loop && c.audition.loop->endBar < c.audition.loop->startBar)
                    return fail(fb::ErrorCode::BadRequest, "The loop ends before it starts.");
                session_.setAudition(c.audition);
                return ok(true);
            },
            [&](const fb::SetMidiOut& c) {
                session_.setMidiOut(c.midiOut);
                return ok(true);
            },
            [&](const fb::SetPreviewSynth& c) {
                session_.setPreviewSynth(c.enabled);
                return ok(true);
            },
            [&](const fb::SetProvider& c) {
                session_.setProvider(c.provider);
                return ok(true);
            },
            [&](const fb::SetApiKey& c) { return setApiKey(c); },
            [&](const fb::StartDrag& c) {
                fb::Reply error;
                MidiMeta meta;
                const auto clip = clipFor(c.nodeId, error, meta);
                if (!clip) return error;
                if (auto e = platform_.startDrag(*clip, meta, c.partIds, c.splitDrums)) return fail(e->code, e->message);
                return ok(false);
            },
            [&](const fb::ExportMidi& c) {
                fb::Reply error;
                MidiMeta meta;
                const auto clip = clipFor(c.nodeId, error, meta);
                if (!clip) return error;
                if (auto e = platform_.exportMidi(*clip, meta, c.partIds, c.splitDrums)) return fail(e->code, e->message);
                return ok(false);
            },
            [&](const fb::ReleaseFocus& c) {
                platform_.releaseFocus(c.reason);
                return ok(false);
            },
            [&](const fb::SearchCatalog& c) {
                const auto* lib = library();
                if (lib == nullptr) return fail(fb::ErrorCode::Internal, libraryError_);
                std::vector<fb::CatalogEntry> entries;
                for (const auto& clip : lib->clips()) entries.push_back(clip.entry);
                for (const auto& n : session_.nodes())
                    if (isAiResult(n)) entries.push_back(entryForNode(n));
                std::vector<CatalogItem> items;
                items.reserve(entries.size());
                for (const auto& e : entries) items.push_back(catalogItem(e));

                const auto& q = c.query;
                CatalogSearch search;
                search.text = q.text;
                if (q.origins) {
                    const auto has = [&](fb::CatalogOrigin o) { return std::find(q.origins->begin(), q.origins->end(), o) != q.origins->end(); };
                    search.includeLibrary = has(fb::CatalogOrigin::Library);
                    search.includeAi = has(fb::CatalogOrigin::Ai);
                }
                if (q.roles)
                    for (const auto r : *q.roles)
                        if (const auto role = roleFromString(fb::toString(r))) search.roles.push_back(*role);
                if (q.genres) search.genres = *q.genres;
                if (q.fitContext) {
                    const auto ctx = session_.effectiveContext(platform_.host());
                    search.fit = CatalogFit{pitchClassFromName(fb::toString(ctx.tonic)),
                                            modeFromString(fb::toString(ctx.mode)).value_or(Mode::Major), ctx.tempo,
                                            ctx.meterNumerator, ctx.meterDenominator};
                }
                search.limit = q.limit;
                search.offset = q.offset;
                const auto found = searchCatalog(items, search);
                fb::Reply r;
                r.ok = true;
                fb::CatalogPage page;
                page.total = found.total;
                for (const auto& h : found.hits) page.entries.push_back(entries[h.index]);
                r.catalog = std::move(page);
                return r;
            },
            [&](const fb::PreviewEntry& c) {
                if (c.entryId) {
                    const auto* lib = library();
                    const bool known = (lib != nullptr && lib->find(*c.entryId) != nullptr) ||
                                       (c.entryId->rfind("node:", 0) == 0 && session_.hasNode(c.entryId->substr(5)));
                    if (!known) return fail(fb::ErrorCode::BadRequest, "No catalog entry " + *c.entryId + ".");
                }
                session_.setPreview(c.entryId);
                return ok(true);
            },
            [&](const fb::UseEntry& c) {
                if (c.entryId.rfind("node:", 0) == 0) {
                    const auto id = c.entryId.substr(5);
                    return session_.select(id) ? ok(true) : unknownNode(id);
                }
                const auto* lib = library();
                const auto* clip = lib != nullptr ? lib->find(c.entryId) : nullptr;
                if (clip == nullptr) return fail(fb::ErrorCode::BadRequest, "No catalog entry " + c.entryId + ".");
                session_.addNode(clip->score, fb::NodeKind::Library, std::nullopt, std::nullopt, 1, platform_.nowMs(),
                                 std::nullopt, clip->entry.id);
                return ok(true);
            },
            [&](const fb::DragEntry& c) {
                if (c.entryId.rfind("node:", 0) == 0) {
                    fb::Reply error;
                    MidiMeta meta;
                    const auto clip = clipFor(c.entryId.substr(5), error, meta);
                    if (!clip) return error;
                    if (auto e = platform_.startDrag(*clip, meta, std::nullopt, false)) return fail(e->code, e->message);
                    return ok(false);
                }
                const auto* lib = library();
                const auto* item = lib != nullptr ? lib->find(c.entryId) : nullptr;
                if (item == nullptr) return fail(fb::ErrorCode::BadRequest, "No catalog entry " + c.entryId + ".");
                if (item->entry.credit && !item->entry.credit->allowsExport)
                    return fail(fb::ErrorCode::BadRequest, "This clip's license doesn't allow dragging it out.");
                const auto bytes = platform_.libraryResource(item->file);
                if (!bytes) return fail(fb::ErrorCode::Internal, "The clip's MIDI is missing from this build.");
                std::string why;
                const auto clip = clipFromMidi(*bytes, item->entry, why);
                if (!clip) return fail(fb::ErrorCode::Internal, "The clip's MIDI can't be read: " + why);
                MidiMeta meta;
                meta.title = item->entry.title;
                meta.credit = item->entry.credit ? item->entry.credit->text : "";
                meta.tempo = item->entry.tempo;
                meta.meterNumerator = item->entry.meterNumerator;
                meta.meterDenominator = item->entry.meterDenominator;
                if (auto e = platform_.startDrag(*clip, meta, std::nullopt, false)) return fail(e->code, e->message);
                return ok(false);
            },
        },
        command);
}

// ---- Generate: the agent service's stream becomes nodes ----------------------------------------
//
// One `generate` is one request id, and each variation is one service stream (`<id>.<n>`). A
// variation's node is made when its first part lands, under the node that was current when the
// request started. It becomes current only if the user hasn't moved since, so the first variation
// to land plays and later ones join the thread. Each part after that updates the node's score,
// which re-renders the audition (it switches on the next bar line). `done` replaces the streamed
// score with the authoritative one. A variation that fails or is cancelled loses its partial node.

namespace {

const char* captureLabel(fb::CaptureIntent intent) {
    switch (intent) {
        case fb::CaptureIntent::Continue: return "Continue what I played";
        case fb::CaptureIntent::Harmonize: return "Harmonize what I played";
        case fb::CaptureIntent::AddBass: return "Add bass to what I played";
        case fb::CaptureIntent::AddDrums: return "Add drums to what I played";
        case fb::CaptureIntent::Answer: return "Answer what I played";
    }
    return "Use what I played";
}

// The lane an intent writes when it keeps the riff; none for continue and answer.
std::optional<fb::Role> captureLane(fb::CaptureIntent intent) {
    switch (intent) {
        case fb::CaptureIntent::Harmonize: return fb::Role::Chords;
        case fb::CaptureIntent::AddBass: return fb::Role::Bass;
        case fb::CaptureIntent::AddDrums: return fb::Role::Drums;
        default: return std::nullopt;
    }
}

// "What you played is already ___."
std::string asPlayed(const std::string& role) {
    if (role == "bass") return "a bass line";
    if (role == "melody") return "a melody";
    return role;  // chords, drums
}

}  // namespace

std::optional<fb::Reply> Controller::useCapture(const fb::Generate& c, const fb::EffectiveContext& ctx, fb::PlanRequest& plan,
                                                std::optional<nlohmann::json>& riff) {
    const auto& use = *c.capture;
    const double ppqPerBar = ctx.meterNumerator * 4.0 / ctx.meterDenominator;
    const auto data = capturedNotes(platform_.captured(use.bars), ppqPerBar, ctx.tempo, ctx.meterNumerator, ctx.meterDenominator);
    const auto bars = std::to_string(use.bars) + (use.bars == 1 ? " bar" : " bars");
    if (data.notes.empty()) return fail(fb::ErrorCode::BadRequest, "Nothing was played in the last " + bars + " on this track.");

    // The played notes, kept exactly; core names the lane, key and chords. The user's key wins;
    // else a key core is sure of; else the session's.
    AnalyzeOptions options;
    options.title = "What I played";
    options.partName = "Played";
    options.literalPart = true;
    const auto& o = session_.contextOverride();
    const auto forceKey = [&](fb::Tonic tonic, fb::Mode mode) {
        options.keyTonic = pitchClassFromName(fb::toString(tonic));
        options.keyMode = modeFromString(fb::toString(mode));
    };
    if (o.tonic && o.mode) forceKey(*o.tonic, *o.mode);
    auto analysis = analyzeMidiData(data, options);
    if (analysis.ok && !options.keyTonic && !analysis.key.reliable && analysis.role != Role::Drums) {
        forceKey(ctx.tonic, ctx.mode);
        analysis = analyzeMidiData(data, options);
    }
    if (!analysis.ok) return fail(fb::ErrorCode::BadRequest, "Flowstate couldn't read what you played: " + analysis.error + ".");
    auto score = nlohmann::json::parse(analysis.scoreJson);
    const std::string role = toString(analysis.role);
    if (const auto t = fb::parseTonic(score["context"].value("tonic", ""))) plan.context.tonic = *t;
    if (const auto m = fb::parseMode(score["context"].value("mode", ""))) plan.context.mode = *m;

    // Harmonize, add bass and add drums keep the riff and write the new lanes at its length.
    if (const auto lane = captureLane(use.intent)) {
        std::vector<fb::Role> lanes;
        for (const auto r : c.roles ? *c.roles : std::vector<fb::Role>{*lane})
            if (fb::toString(r) != role) lanes.push_back(r);
        if (lanes.empty())
            return fail(fb::ErrorCode::BadRequest, "What you played is already " + asPlayed(role) + ". Choose another way to use it.");
        plan.roles = lanes;
        plan.context.bars = analysis.bars;
        riff = score;
    }
    plan.reference = fb::Reference{std::move(score), use.intent};
    return std::nullopt;
}

fb::Reply Controller::generate(const fb::Generate& c) {
    if (!requests_.empty()) return fail(fb::ErrorCode::Busy, "A generation is already running.");

    const auto host = platform_.host();
    const auto ctx = session_.effectiveContext(host);
    const auto* cur = session_.current();

    fb::PlanRequest plan;
    plan.prompt = c.prompt;
    plan.context.tempo = ctx.tempo;
    plan.context.meterNumerator = ctx.meterNumerator;
    plan.context.meterDenominator = ctx.meterDenominator;
    plan.context.tonic = ctx.tonic;
    plan.context.mode = ctx.mode;
    plan.context.bars = ctx.bars;
    plan.context.swing = 0.0;
    if (cur != nullptr)
        if (const auto cx = cur->score.find("context"); cx != cur->score.end() && cx->is_object())
            if (const auto sw = cx->find("swing"); sw != cx->end() && sw->is_number()) plan.context.swing = sw->get<double>();
    if (const auto& o = session_.contextOverride(); o.swing) plan.context.swing = *o.swing;
    plan.roles = c.roles;
    plan.provider = session_.provider();

    // What was just played goes as the reference; it plans a new idea, so locks don't apply.
    std::optional<nlohmann::json> riff;
    if (c.capture) {
        if (auto error = useCapture(c, ctx, plan, riff)) return *error;
    } else if (cur != nullptr && cur->score.contains("parts")) {
        // Locked parts (and the harmony) go to the service as `keep`.
        std::vector<std::string> locked;
        for (const auto& st : session_.partStates())
            if (st.locked) locked.push_back(st.partId);
        nlohmann::json kept = nlohmann::json::array();
        for (const auto& p : cur->score["parts"])
            if (std::find(locked.begin(), locked.end(), p.value("id", "")) != locked.end()) kept.push_back(p);
        if (!kept.empty()) {
            auto keep = cur->score;
            keep["parts"] = std::move(kept);
            plan.keep = std::move(keep);
        }
    }

    Request request;
    request.id = "r" + std::to_string(nextRequestNumber_++);
    request.kind = fb::NodeKind::Initial;
    request.prompt = c.prompt;
    request.parentId = cur != nullptr ? std::optional<std::string>(cur->id) : std::nullopt;
    const auto now = platform_.nowMs();
    for (int i = 0; i < c.count; ++i) {
        Stream stream;
        stream.id = request.id + "." + std::to_string(i + 1);
        // A fresh seed per variation; the node keeps it, so the idea re-realizes identically.
        stream.seed = static_cast<std::int64_t>((static_cast<std::uint64_t>(now) * 2654435761u + static_cast<std::uint64_t>(i) * 40503u) &
                                                0xffffffffu);
        request.streams.push_back(std::move(stream));
    }

    // Start every variation; if one can't start, none runs.
    const auto key = providerKey();
    for (std::size_t i = 0; i < request.streams.size(); ++i) {
        if (auto e = platform_.startPlan(request.streams[i].id, plan, key)) {
            for (std::size_t j = 0; j < i; ++j) platform_.cancelStream(request.streams[j].id);
            return fail(e->code, e->message);
        }
    }

    std::string said = c.prompt;
    if (c.capture && said.empty()) said = captureLabel(c.capture->intent);
    session_.addThreadItem({"t-" + request.id, fb::ThreadRole::User, said, std::nullopt, now});
    // Variations attach under this node; it stays while they stream.
    if (request.parentId) session_.pin(*request.parentId);
    // The instant sketch plays at once, while the model writes (P1-10).
    request.sketchId = makeSketch(c, plan, riff ? &*riff : nullptr, request.streams.front().seed, now, request.parentId);
    if (request.sketchId) request.sketchParts = session_.node(*request.sketchId)->score["parts"];
    const auto id = request.id;
    requests_.push_back(std::move(request));
    publishGenerations();
    emit(fb::GenerationStarted{id, fb::NodeKind::Initial});
    auto r = ok(true);
    r.requestId = id;
    return r;
}

std::optional<std::string> Controller::makeSketch(const fb::Generate& c, const fb::PlanRequest& plan, const nlohmann::json* riff,
                                                  std::int64_t seed, std::int64_t now, const std::optional<std::string>& parent) {
    // The plan's context: the session's, or a capture's key and length.
    SketchRequest req;
    req.context.tonic = fb::toString(plan.context.tonic);
    req.context.mode = modeFromString(fb::toString(plan.context.mode)).value_or(Mode::Major);
    req.context.tempo = plan.context.tempo;
    req.context.meterNumerator = plan.context.meterNumerator;
    req.context.meterDenominator = plan.context.meterDenominator;
    req.context.bars = plan.context.bars;
    req.context.swing = plan.context.swing;
    // Style hints: the current idea's tags, and the prompt itself (the sketch matches tags by substring, so
    // "dark trap beat" picks the trap groove).
    if (const auto* cur = session_.current())
        if (const auto cx = cur->score.find("context"); cx != cur->score.end() && cx->is_object())
            if (const auto st = cx->find("style"); st != cx->end() && st->is_array())
                for (const auto& tag : *st)
                    if (tag.is_string()) req.context.style.push_back(tag.get<std::string>());
    std::string prompt = c.prompt;
    std::transform(prompt.begin(), prompt.end(), prompt.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (!prompt.empty()) req.context.style.push_back(prompt);

    // Every requested role a locked part (or the kept riff) doesn't already play.
    const nlohmann::json* fixed = plan.keep ? &*plan.keep : riff;
    std::vector<std::string> kept;
    if (fixed != nullptr)
        for (const auto& p : (*fixed)["parts"]) kept.push_back(p.value("role", ""));
    const auto& roles = plan.roles ? plan.roles : c.roles;
    const std::vector<fb::Role> wanted = roles ? *roles : std::vector<fb::Role>{fb::Role::Chords, fb::Role::Bass, fb::Role::Melody, fb::Role::Drums};
    for (const auto r : wanted) {
        const std::string name = fb::toString(r);
        if (std::find(kept.begin(), kept.end(), name) != kept.end()) continue;
        if (const auto role = roleFromString(name)) req.roles.push_back(*role);
    }
    if (req.roles.empty()) return std::nullopt;
    req.seed = static_cast<std::uint64_t>(seed);

    nlohmann::json score;
    try {
        score = nlohmann::json::parse(sketchJson(req));
    } catch (const std::exception&) {
        return std::nullopt;  // never in practice: the sketch is tested valid for every context
    }
    const auto harmony = fixed != nullptr ? fixed->find("harmony") : nlohmann::json::const_iterator{};
    const bool hasHarmony = fixed != nullptr && harmony != fixed->end() && !harmony->is_null() &&
                            !(harmony->is_string() && harmony->get_ref<const std::string&>().empty()) &&
                            !(harmony->is_array() && harmony->empty());
    if (hasHarmony) {
        // Locked parts keep their idea's head (harmony, form, motifs), and so does a riff whose
        // analysis found chords; the sketch parts play against it.
        auto base = *fixed;
        for (auto& p : score["parts"]) base["parts"].push_back(std::move(p));
        base["title"] = "Sketch";
        score = std::move(base);
    } else if (fixed != nullptr) {
        // A riff without harmony (a melody, drums) is played notes: it joins the sketch's head as it is.
        for (const auto& p : (*fixed)["parts"]) score["parts"].push_back(p);
    }
    const auto id = session_.addNode(std::move(score), fb::NodeKind::Sketch, c.prompt, std::nullopt, seed, now, parent);
    session_.pin(id);
    return id;
}

void Controller::fillFromSketch(const Request& request, nlohmann::json& score) {
    std::vector<std::string> roles;
    for (const auto& p : score["parts"]) roles.push_back(p.value("role", ""));
    for (const auto& p : request.sketchParts)
        if (std::find(roles.begin(), roles.end(), p.value("role", "")) == roles.end()) score["parts"].push_back(p);
}

void Controller::dropSketch(Request& request) {
    if (!request.sketchId) return;
    session_.unpin(*request.sketchId);
    session_.removeNode(*request.sketchId);  // keeps it if something was made from it
    request.sketchId.reset();
}

std::vector<fb::HistoryStep> Controller::history() const {
    std::vector<fb::HistoryStep> steps;
    for (const auto* n = session_.current(); n != nullptr; n = n->parentId ? session_.node(*n->parentId) : nullptr) {
        fb::HistoryStep step;
        step.kind = n->kind;
        step.prompt = n->prompt.value_or("");
        // The assistant's note for the node: its thread text, unless that is only the title.
        const auto title = n->score.value("title", std::string());
        for (const auto& t : session_.thread())
            if (t.role == fb::ThreadRole::Assistant && t.nodeId == n->id && t.text != title) step.note = t.text;
        if (n->partIds) step.changed = *n->partIds;
        steps.push_back(std::move(step));
        if (steps.size() > 64) break;  // the service reads the last 8; never walk an unbounded path
    }
    std::reverse(steps.begin(), steps.end());
    return steps;
}

fb::Reply Controller::startEdit(fb::EditKind kind, const std::string& prompt, std::optional<std::vector<std::string>> partIds,
                                std::optional<fb::Role> role, const std::string& threadText) {
    if (!requests_.empty()) return fail(fb::ErrorCode::Busy, "A generation is already running.");
    const auto* cur = session_.current();
    if (cur == nullptr) return fail(fb::ErrorCode::BadRequest, "There's no idea to change yet. Generate one first.");
    const auto parts = cur->score.value("parts", nlohmann::json::array());
    const auto partName = [&](const std::string& id) {
        for (const auto& p : parts)
            if (p.value("id", "") == id) return p.value("name", id);
        return id;
    };
    const auto known = [&](const std::string& id) {
        return std::any_of(parts.begin(), parts.end(), [&](const nlohmann::json& p) { return p.value("id", "") == id; });
    };
    std::vector<std::string> locked;
    for (const auto& st : session_.partStates())
        if (st.locked && known(st.partId)) locked.push_back(st.partId);
    const auto isLocked = [&](const std::string& id) { return std::find(locked.begin(), locked.end(), id) != locked.end(); };

    // The parts the edit may change: never a locked one.
    if (kind == fb::EditKind::Edit) {
        std::vector<std::string> open;
        if (partIds) {
            for (const auto& id : *partIds) {
                if (!known(id)) return fail(fb::ErrorCode::UnknownPart, "No part " + id + " in the current idea.");
                if (!isLocked(id)) open.push_back(id);
            }
            if (open.empty()) return fail(fb::ErrorCode::BadRequest, "Every part you picked is locked. Unlock one to change it.");
            partIds = std::move(open);
        } else if (!locked.empty()) {
            for (const auto& p : parts)
                if (!isLocked(p.value("id", ""))) open.push_back(p.value("id", ""));
            if (open.empty()) return fail(fb::ErrorCode::BadRequest, "Every part is locked. Unlock one to change it.");
            partIds = std::move(open);
        }
    } else if (kind == fb::EditKind::Vary) {
        const auto& id = partIds->front();
        if (!known(id)) return fail(fb::ErrorCode::UnknownPart, "No part " + id + " in the current idea.");
        if (isLocked(id)) return fail(fb::ErrorCode::BadRequest, partName(id) + " is locked. Unlock it to vary it.");
    }

    fb::EditRequest edit;
    edit.kind = kind;
    edit.prompt = prompt;
    edit.score = cur->score;
    edit.partIds = partIds;
    edit.role = role;
    edit.history = history();
    edit.provider = session_.provider();

    Request request;
    request.id = "r" + std::to_string(nextRequestNumber_++);
    request.kind = kind == fb::EditKind::Vary ? fb::NodeKind::Vary : fb::NodeKind::Edit;
    request.prompt = prompt;
    request.parentId = cur->id;
    request.edit = true;
    request.baseParts = parts;
    Stream stream;
    stream.id = request.id + ".1";
    stream.seed = cur->seed;  // unchanged parts realize exactly as before
    request.streams.push_back(std::move(stream));
    if (auto e = platform_.startEdit(request.streams.front().id, edit, providerKey())) return fail(e->code, e->message);

    const auto now = platform_.nowMs();
    session_.addThreadItem({"t-" + request.id, fb::ThreadRole::User, threadText, std::nullopt, now});
    session_.pin(cur->id);
    const auto id = request.id;
    const auto nodeKind = request.kind;
    requests_.push_back(std::move(request));
    publishGenerations();
    emit(fb::GenerationStarted{id, nodeKind});
    auto r = ok(true);
    r.requestId = id;
    return r;
}

fb::Reply Controller::cancel(const std::string& requestId) {
    const auto it = std::find_if(requests_.begin(), requests_.end(), [&](const Request& r) { return r.id == requestId; });
    if (it == requests_.end()) return fail(fb::ErrorCode::BadRequest, "No running request " + requestId + ".");
    for (auto& stream : it->streams) {
        if (stream.finished) continue;
        platform_.cancelStream(stream.id);
        finishStream(*it, stream, fb::ErrorInfo{fb::ErrorCode::Cancelled, "Cancelled."});
    }
    finishRequestIfDone(requestId);
    return ok(true);
}

void Controller::cancelAll() {
    std::vector<std::string> ids;
    for (const auto& r : requests_) ids.push_back(r.id);
    for (const auto& id : ids) cancel(id);
}

std::pair<Controller::Request*, Controller::Stream*> Controller::findStream(const std::string& streamId) {
    for (auto& r : requests_)
        for (auto& s : r.streams)
            if (s.id == streamId) return {&r, s.finished ? nullptr : &s};
    return {nullptr, nullptr};
}

void Controller::serviceEvent(const std::string& streamId, const fb::ServiceEvent& event) {
    auto [request, stream] = findStream(streamId);
    if (stream == nullptr) return;
    std::visit(Overloaded{
                   [&](const fb::ScoreHeader& e) {
                       // A later header (a repair rewrote the plan) replaces the earlier one.
                       stream->header = e.score;
                       if (request->stage != fb::GenerationStage::Streaming) {
                           request->stage = fb::GenerationStage::Streaming;
                           publishGenerations();
                           changed();
                       }
                   },
                   [&](const fb::PartStarted&) {},
                   [&](const fb::PartDone& e) {
                       if (!stream->header || !e.part.is_object()) return;  // the service sends the header first
                       const auto partId = e.part.value("id", "");
                       auto& parts = stream->parts;
                       const auto same = std::find_if(parts.begin(), parts.end(),
                                                      [&](const nlohmann::json& p) { return p.value("id", "") == partId; });
                       if (same != parts.end()) *same = e.part;  // a repaired part replaces the first one
                       else parts.push_back(e.part);
                       if (std::find(request->partsDone.begin(), request->partsDone.end(), partId) == request->partsDone.end())
                           request->partsDone.push_back(partId);
                       if (std::find(stream->streamedIds.begin(), stream->streamedIds.end(), partId) == stream->streamedIds.end())
                           stream->streamedIds.push_back(partId);
                       partLanded(*request, *stream);
                       emit(fb::PartReady{request->id, partId});
                   },
                   [&](const fb::AssistantMessage& e) {
                       if (!stream->message.empty()) stream->message += "\n";
                       stream->message += e.text;
                   },
                   [&](const fb::ScoreDone& e) {
                       const auto requestId = request->id;
                       if (e.score) {
                           // The authoritative score, kept over anything streamed: nothing is filled in.
                           stream->complete = true;
                           stream->header = *e.score;
                           stream->parts = e.score->contains("parts") ? (*e.score)["parts"] : nlohmann::json::array();
                           partLanded(*request, *stream);
                       } else {
                           stream->textOnly = true;
                       }
                       finishStream(*request, *stream, std::nullopt);
                       finishRequestIfDone(requestId);
                   },
                   [&](const fb::ServiceError& e) {
                       const auto requestId = request->id;
                       finishStream(*request, *stream, e.error);
                       finishRequestIfDone(requestId);
                   },
               },
               event);
}

void Controller::serviceEnded(const std::string& streamId, std::optional<fb::ErrorInfo> error) {
    auto [request, stream] = findStream(streamId);
    if (stream == nullptr) return;
    const auto requestId = request->id;
    finishStream(*request, *stream,
                 error ? *error : fb::ErrorInfo{fb::ErrorCode::Network, "The agent service closed the stream before the plan was done."});
    finishRequestIfDone(requestId);
}

void Controller::partLanded(Request& request, Stream& stream) {
    auto score = *stream.header;
    score["parts"] = stream.parts;
    if (!stream.complete) {
        if (request.edit) {
            // Streamed parts replace the current node's part with the same id, or join it.
            auto parts = request.baseParts;
            for (const auto& p : stream.parts) {
                const auto id = p.value("id", "");
                const auto same = std::find_if(parts.begin(), parts.end(), [&](const nlohmann::json& b) { return b.value("id", "") == id; });
                if (same != parts.end()) *same = p;
                else parts.push_back(p);
            }
            score["parts"] = std::move(parts);
        } else {
            fillFromSketch(request, score);
        }
    }
    if (stream.nodeId) {
        session_.updateNodeScore(*stream.nodeId, std::move(score));
    } else {
        // Becomes current only if the user is still where the request started, or on its sketch.
        const auto* cur = session_.current();
        const bool onSketch = cur != nullptr && request.sketchId == cur->id;
        const bool stayed = onSketch || (cur == nullptr && !request.parentId) || (cur != nullptr && request.parentId == cur->id);
        stream.nodeId = session_.addNode(std::move(score), request.kind, request.prompt, std::nullopt, stream.seed,
                                         platform_.nowMs(), request.parentId, std::nullopt, stayed);
        session_.pin(*stream.nodeId);
        // With no idea when the request started, a variation is a root (addNode would put it under the sketch).
        if (!request.parentId) session_.reparent(*stream.nodeId, std::nullopt);
        // The first variation to land takes the sketch's place.
        // The sketch stays (hidden) until the request succeeds, so a failure can go back to it.
        if (onSketch) request.tookOverSketch = true;
    }
    publishGenerations();
    changed();
}

void Controller::finishStream(Request& request, Stream& stream, std::optional<fb::ErrorInfo> error) {
    (void)request;
    stream.finished = true;
    stream.error = std::move(error);
    if ((stream.error || stream.textOnly) && stream.nodeId) {
        // A partial idea from a failed plan doesn't stay, unless something was already made from it.
        session_.unpin(*stream.nodeId);
        if (session_.removeNode(*stream.nodeId)) stream.nodeId.reset();
    }
}

void Controller::finishRequestIfDone(const std::string& requestId) {
    const auto it = std::find_if(requests_.begin(), requests_.end(), [&](const Request& r) { return r.id == requestId; });
    if (it == requests_.end()) return;
    if (!std::all_of(it->streams.begin(), it->streams.end(), [](const Stream& s) { return s.finished; })) return;

    Request request = std::move(*it);
    requests_.erase(it);
    if (request.parentId) session_.unpin(*request.parentId);
    for (const auto& s : request.streams)
        if (s.nodeId) session_.unpin(*s.nodeId);
    const auto now = platform_.nowMs();
    std::vector<std::string> nodeIds;
    std::optional<fb::ErrorInfo> firstError;
    bool answered = false;
    for (const auto& s : request.streams) {
        if (s.nodeId && !s.error) {
            nodeIds.push_back(*s.nodeId);
            if (request.edit) {
                // What the edit changed: the parts that streamed in, and the ones done.score left out.
                std::vector<std::string> changed = s.streamedIds;
                if (const auto* n = session_.node(*s.nodeId))
                    for (const auto& b : request.baseParts) {
                        const auto id = b.value("id", "");
                        const auto& parts = n->score.value("parts", nlohmann::json::array());
                        const bool kept = std::any_of(parts.begin(), parts.end(), [&](const nlohmann::json& p) { return p.value("id", "") == id; });
                        if (!kept && std::find(changed.begin(), changed.end(), id) == changed.end()) changed.push_back(id);
                    }
                session_.setChanged(*s.nodeId, std::move(changed));
            }
            const auto* n = session_.node(*s.nodeId);
            const auto title = n != nullptr ? n->score.value("title", std::string()) : std::string();
            session_.addThreadItem({"t-" + s.id, fb::ThreadRole::Assistant, s.message.empty() ? title : s.message, s.nodeId, now});
        } else if (s.textOnly && !s.error) {
            answered = true;
            if (!s.message.empty()) session_.addThreadItem({"t-" + s.id, fb::ThreadRole::Assistant, s.message, std::nullopt, now});
        } else if (s.error && !firstError) {
            firstError = s.error;
        }
    }
    // The sketch goes once a variation succeeded, or when the answer was text only. After a failure it stays,
    // so there is still an idea to play; if a variation had taken over from it, playback goes back to it.
    if (request.sketchId) {
        if (!nodeIds.empty() || answered) {
            const auto* cur = session_.current();
            if (!nodeIds.empty() && cur != nullptr && cur->id == *request.sketchId) session_.select(nodeIds.front());
            dropSketch(request);
        } else {
            session_.unpin(*request.sketchId);
            const auto* cur = session_.current();
            const bool backAtStart = cur == nullptr ? !request.parentId : request.parentId == cur->id;
            if (request.tookOverSketch && backAtStart) session_.select(*request.sketchId);
        }
    }
    publishGenerations();
    changed();
    if (!nodeIds.empty() || answered) {
        emit(fb::GenerationDone{request.id, nodeIds});
        if (firstError && firstError->code != fb::ErrorCode::Cancelled)
            emit(fb::Notice{fb::NoticeLevel::Warning, "A variation failed: " + firstError->message});
    } else {
        emit(fb::GenerationFailed{request.id, firstError.value_or(fb::ErrorInfo{fb::ErrorCode::Internal, "No result."})});
    }
}

void Controller::publishGenerations() {
    std::vector<fb::Generation> gens;
    for (const auto& r : requests_) gens.push_back({r.id, r.kind, r.stage, r.partsDone});
    session_.setGenerations(std::move(gens));
}

void Controller::changed() {
    if (onChanged) onChanged();
}

void Controller::emit(fb::PluginEvent event) {
    if (onEvent) onEvent(event);
}

std::string Controller::handleJson(const std::string& commandJson) {
    fb::Reply reply;
    try {
        const auto parsed = nlohmann::json::parse(commandJson);
        fb::Command command;
        fb::from_json(parsed, command);
        reply = handle(command);
    } catch (const fb::ParseError& e) {
        reply = fail(fb::ErrorCode::BadRequest, std::string("Malformed command: ") + e.what());
    } catch (const nlohmann::json::exception&) {
        reply = fail(fb::ErrorCode::BadRequest, "Malformed command: not JSON.");
    } catch (const std::exception& e) {
        reply = fail(fb::ErrorCode::Internal, e.what());
    }
    nlohmann::json out;
    fb::to_json(out, reply);
    return out.dump();
}

}  // namespace flowstate::plugin
