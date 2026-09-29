// Dispatches bridge Commands (docs/bridge-spec.md) against the Session. JUCE-free: the plugin
// supplies OS actions (drag, export, focus) through Platform, and the tests supply a fake.
// Message thread only.
#pragma once

#include "session/Session.h"

#include <functional>
#include <string>

namespace flowstate::plugin {

// File metadata for drag and export: the idea's title, tempo and meter.
struct MidiMeta {
    std::string title;
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
};

class Controller {
public:
    Controller(Session& session, Platform& platform) : session_(session), platform_(platform) {}

    // Called after any command that changed the session (the processor snapshots state and
    // pushes a `session` event to the UI).
    std::function<void()> onChanged;

    fb::Reply handle(const fb::Command& command);

    // JSON in, JSON out: parse failures become a `bad_request` reply with the ParseError path.
    std::string handleJson(const std::string& commandJson);

    fb::Session view() const { return session_.view(platform_.host(), platform_.captureBars()); }

private:
    fb::Reply ok(bool changed);
    static fb::Reply fail(fb::ErrorCode code, std::string message);

    Session& session_;
    Platform& platform_;
};

}  // namespace flowstate::plugin
