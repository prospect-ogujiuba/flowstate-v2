#include "session/Controller.h"

#include "flowstate/theory.h"

#include <algorithm>
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
    const auto needsService = [](const char* what) {
        return fail(fb::ErrorCode::Unavailable, std::string(what) + " needs the agent service, which isn't connected yet.");
    };

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
                fb::Reply r;
                r.ok = true;
                r.session = view();
                return r;
            },
            [&](const fb::Generate& c) { return generate(c); },
            [&](const fb::Edit&) { return needsService("Editing by prompt"); },
            [&](const fb::Vary&) { return needsService("Vary"); },
            [&](const fb::AddPart&) { return needsService("Adding a part"); },
            [&](const fb::Reroll&) {
                return fail(fb::ErrorCode::Unavailable, "Re-roll needs per-part seeds in core (P1-11).");
            },
            [&](const fb::Tweak&) {
                return fail(fb::ErrorCode::Unavailable, "Local transforms aren't in core yet.");
            },
            [&](const fb::EditNotes&) {
                return fail(fb::ErrorCode::Unavailable, "Note edits aren't in core yet.");
            },
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
            [&](const fb::SetApiKey&) {
                // The key is dropped here: nothing stores or logs it until the keychain lands (P1-12).
                return fail(fb::ErrorCode::Unavailable, "Key storage isn't available in this build yet.");
            },
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

fb::Reply Controller::generate(const fb::Generate& c) {
    if (!requests_.empty()) return fail(fb::ErrorCode::Busy, "A generation is already running.");
    if (c.capture)
        return fail(fb::ErrorCode::Unavailable,
                    "\"Use what I just played\" needs the planner to plan around a reference, which isn't built yet.");

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

    // Locked parts (and the harmony) go to the service as `keep`.
    if (cur != nullptr && cur->score.contains("parts")) {
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
    for (std::size_t i = 0; i < request.streams.size(); ++i) {
        if (auto e = platform_.startPlan(request.streams[i].id, plan)) {
            for (std::size_t j = 0; j < i; ++j) platform_.cancelStream(request.streams[j].id);
            return fail(e->code, e->message);
        }
    }

    session_.addThreadItem({"t-" + request.id, fb::ThreadRole::User, c.prompt, std::nullopt, now});
    const auto id = request.id;
    requests_.push_back(std::move(request));
    publishGenerations();
    emit(fb::GenerationStarted{id, fb::NodeKind::Initial});
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
                           // The authoritative score, kept over anything streamed.
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
    if (stream.nodeId) {
        session_.updateNodeScore(*stream.nodeId, std::move(score));
    } else {
        // Becomes current only if the user is still where the request started.
        const auto* cur = session_.current();
        const bool stayed = (cur == nullptr && !request.parentId) || (cur != nullptr && request.parentId == cur->id);
        stream.nodeId = session_.addNode(std::move(score), request.kind, request.prompt, std::nullopt, stream.seed,
                                         platform_.nowMs(), request.parentId, std::nullopt, stayed);
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
        if (session_.removeNode(*stream.nodeId)) stream.nodeId.reset();
    }
}

void Controller::finishRequestIfDone(const std::string& requestId) {
    const auto it = std::find_if(requests_.begin(), requests_.end(), [&](const Request& r) { return r.id == requestId; });
    if (it == requests_.end()) return;
    if (!std::all_of(it->streams.begin(), it->streams.end(), [](const Stream& s) { return s.finished; })) return;

    const Request request = std::move(*it);
    requests_.erase(it);
    const auto now = platform_.nowMs();
    std::vector<std::string> nodeIds;
    std::optional<fb::ErrorInfo> firstError;
    bool answered = false;
    for (const auto& s : request.streams) {
        if (s.nodeId && !s.error) {
            nodeIds.push_back(*s.nodeId);
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
