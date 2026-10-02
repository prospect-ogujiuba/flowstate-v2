// Incremental parser for the agent service's server-sent events (docs/bridge-spec.md, "Plugin ↔
// agent service"). Bytes go in as they arrive from the network; each complete event's `data`
// comes out. Lines starting with `:` are keepalive comments and are skipped. JUCE-free.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace flowstate::plugin {

class SseParser {
public:
    // A stream that sends more than this without an event boundary is broken.
    static constexpr std::size_t kMaxEventBytes = 4 * 1024 * 1024;

    // Appends bytes; returns the data of every event they complete, in order. Throws
    // std::length_error if an event outgrows kMaxEventBytes.
    std::vector<std::string> feed(const char* bytes, std::size_t size);
    std::vector<std::string> feed(const std::string& bytes) { return feed(bytes.data(), bytes.size()); }

    // The data of an event the stream ended in the middle of (no blank line after it), if any.
    std::vector<std::string> finish();

private:
    void line(std::string_view text, std::vector<std::string>& out);
    void dispatch(std::vector<std::string>& out);

    std::string pending_;  // bytes after the last complete line
    std::string data_;     // data lines of the current event, joined by "\n"
    bool hasData_ = false;
};

}  // namespace flowstate::plugin
