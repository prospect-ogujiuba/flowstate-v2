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
            [&](const fb::Generate&) { return needsService("Generating"); },
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
            [&](const fb::Cancel& c) {
                return fail(fb::ErrorCode::BadRequest, "No running request " + c.requestId + ".");
            },
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
