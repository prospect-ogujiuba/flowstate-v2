#include "session/Sse.h"

#include <stdexcept>
#include <string_view>

namespace flowstate::plugin {

std::vector<std::string> SseParser::feed(const char* bytes, std::size_t size) {
    std::vector<std::string> out;
    pending_.append(bytes, size);
    std::size_t start = 0;
    for (;;) {
        const auto nl = pending_.find('\n', start);
        if (nl == std::string::npos) break;
        auto end = nl;
        if (end > start && pending_[end - 1] == '\r') --end;
        line(std::string_view(pending_).substr(start, end - start), out);
        start = nl + 1;
    }
    pending_.erase(0, start);
    if (pending_.size() + data_.size() > kMaxEventBytes) throw std::length_error("server-sent event too large");
    return out;
}

std::vector<std::string> SseParser::finish() {
    std::vector<std::string> out;
    if (!pending_.empty()) {
        const std::string last = std::move(pending_);
        pending_.clear();
        line(last, out);
    }
    dispatch(out);
    return out;
}

void SseParser::line(std::string_view text, std::vector<std::string>& out) {
    if (text.empty()) {
        dispatch(out);
        return;
    }
    if (text.front() == ':') return;  // comment (keepalive)
    const auto colon = text.find(':');
    const auto field = text.substr(0, colon);
    if (field != "data") return;  // event, id and retry aren't used by the service
    auto value = colon == std::string_view::npos ? std::string_view() : text.substr(colon + 1);
    if (!value.empty() && value.front() == ' ') value.remove_prefix(1);
    if (hasData_) data_ += '\n';
    data_.append(value);
    hasData_ = true;
}

void SseParser::dispatch(std::vector<std::string>& out) {
    if (!hasData_) return;
    out.push_back(std::move(data_));
    data_.clear();
    hasData_ = false;
}

}  // namespace flowstate::plugin
