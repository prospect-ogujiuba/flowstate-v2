// Dispatches bridge Commands (docs/bridge-spec.md) against the Session. JUCE-free: the plugin
// supplies OS actions (drag, export, focus) through Platform, and the tests supply a fake.
// Message thread only.
#pragma once

#include "session/Audition.h"
#include "session/Library.h"
#include "session/Session.h"

#include <functional>
#include <string>

namespace flowstate::plugin {

// File metadata for drag and export: the idea's title, tempo and meter, and the credit of the
// library clip it came from (written as the file's copyright text).
struct MidiMeta {
    std::string title;
    std::string credit;
    double tempo = 120.0;
    int meterNumerator = 4;
    int meterDenominator = 4;
};

class Platform {
public:
    virtual ~Platform() = default;
    virtual std::int64_t nowMs() = 0;
    virtual HostSnapshot host() = 0;
    virtual int captureBars() = 0;
    // Drag and export get a realized clip. Return an error, or nullopt on success.
    virtual std::optional<fb::ErrorInfo> startDrag(const fb::Clip& clip, const MidiMeta& meta,
                                                   const std::optional<std::vector<std::string>>& partIds,
                                                   bool splitDrums) = 0;
    virtual std::optional<fb::ErrorInfo> exportMidi(const fb::Clip& clip, const MidiMeta& meta,
                                                    const std::optional<std::vector<std::string>>& partIds,
                                                    bool splitDrums) = 0;
    virtual void releaseFocus(fb::FocusReason reason) = 0;
    // A file of the bundled library (library/catalog): "catalog.json" or a clip's file name.
    // Nullopt when the build carries no library.
    virtual std::optional<std::vector<std::uint8_t>> libraryResource(const std::string& name) {
        (void)name;
        return std::nullopt;
    }
    // The agent service (docs/bridge-spec.md, "Plugin ↔ agent service"). startPlan sends one
    // PlanRequest and streams its events back on the message thread, through
    // Controller::serviceEvent and then Controller::serviceEnded. `streamId` is the plugin's id for
    // that one stream. Returns an error if the request can't be sent at all. Without a service,
    // generating answers `unavailable`.
    virtual std::optional<fb::ErrorInfo> startPlan(const std::string& streamId, const fb::PlanRequest& request) {
        (void)streamId;
        (void)request;
        return fb::ErrorInfo{fb::ErrorCode::Unavailable, "This build has no agent service."};
    }
    // Aborts the HTTP request; the service stops the provider stream. Nothing more is reported for it.
    virtual void cancelStream(const std::string& streamId) { (void)streamId; }
};

class Controller {
public:
    Controller(Session& session, Platform& platform) : session_(session), platform_(platform) {}

    // Called after any command or service event that changed the session (the processor
    // snapshots state, re-renders the audition and pushes a `session` event to the UI).
    std::function<void()> onChanged;
    // Generation progress for the UI: generationStarted, partReady, generationDone, generationFailed.
    std::function<void(const fb::PluginEvent&)> onEvent;

    // From the agent service, on the message thread (see Platform::startPlan). Events and ends of
    // streams that were cancelled or already finished are ignored. `error` is null when the stream
    // closed normally; closing before `done` or `error` is a `network` failure.
    void serviceEvent(const std::string& streamId, const fb::ServiceEvent& event);
    void serviceEnded(const std::string& streamId, std::optional<fb::ErrorInfo> error);
    // Cancels every running request (the processor is going away).
    void cancelAll();
    bool generating() const { return !requests_.empty(); }

    fb::Reply handle(const fb::Command& command);

    // JSON in, JSON out: parse failures become a `bad_request` reply with the ParseError path.
    std::string handleJson(const std::string& commandJson);

    // The session for the UI, with what this build can't do yet (Session.unavailable).
    fb::Session view() const;
    // Features this build answers `unavailable` (or accepts without effect), with the reason the
    // UI shows. The same reasons come back when such a command is sent anyway.
    static const std::vector<fb::FeatureGap>& featureGaps();

    // What this instance plays, and how: a catalog entry while one previews, else the audition
    // node (null = the current node), filtered by the loop range, mute and solo, and the MIDI-out
    // role. A preview plays as written, unfiltered. No clip when there is nothing to play.
    struct AuditionSource {
        std::optional<fb::Clip> clip;
        AuditionFilter filter;
    };
    AuditionSource audition();

private:
    fb::Reply ok(bool changed);
    static fb::Reply fail(fb::ErrorCode code, std::string message);
    // The bundled library, loaded on first use. Null (with libraryError_ set) when it is malformed.
    const Library* library();
    MidiMeta metaFor(const fb::LineageNode& node);

    // One model request (a `generate`) and its variations, one service stream each.
    struct Stream {
        std::string id;  // <requestId>.<variation>
        std::int64_t seed = 0;
        std::optional<nlohmann::json> header;
        nlohmann::json parts = nlohmann::json::array();
        std::optional<std::string> nodeId;  // made when the first part lands
        std::string message;
        bool finished = false;
        bool textOnly = false;
        std::optional<fb::ErrorInfo> error;
    };
    struct Request {
        std::string id;
        fb::NodeKind kind = fb::NodeKind::Initial;
        std::string prompt;
        std::optional<std::string> parentId;  // the current node when it started
        fb::GenerationStage stage = fb::GenerationStage::Planning;
        std::vector<std::string> partsDone;
        std::vector<Stream> streams;
    };

    fb::Reply generate(const fb::Generate& command);
    fb::Reply cancel(const std::string& requestId);
    std::pair<Request*, Stream*> findStream(const std::string& streamId);
    void partLanded(Request& request, Stream& stream);
    void finishStream(Request& request, Stream& stream, std::optional<fb::ErrorInfo> error);
    void finishRequestIfDone(const std::string& requestId);
    void publishGenerations();
    void changed();
    void emit(fb::PluginEvent event);

    Session& session_;
    Platform& platform_;
    std::vector<Request> requests_;
    std::uint64_t nextRequestNumber_ = 1;
    std::optional<Library> library_;
    std::string libraryError_;
};

}  // namespace flowstate::plugin
