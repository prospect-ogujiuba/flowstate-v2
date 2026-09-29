#include "session/Controller.h"

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
        meta = metaOf(*node);
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
