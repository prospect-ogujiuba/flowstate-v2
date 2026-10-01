// Dispatches bridge Commands (docs/bridge-spec.md) against the Session. JUCE-free: the plugin
// supplies OS actions (drag, export, focus) through Platform, and the tests supply a fake.
// Message thread only.
#pragma once

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
    // The bundled library, loaded on first use. Null (with libraryError_ set) when it is malformed.
    const Library* library();
    MidiMeta metaFor(const fb::LineageNode& node);

    Session& session_;
    Platform& platform_;
    std::optional<Library> library_;
    std::string libraryError_;
};

}  // namespace flowstate::plugin
