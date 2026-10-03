#include "ServiceClient.h"

#include "session/Sse.h"

#include <juce_events/juce_events.h>

#include <map>
#include <mutex>
#include <set>

namespace flowstate::plugin {

namespace {

// One pool per plugin process, shared by every instance (docs/threading.md rule 3). Each running
// stream holds a thread while it waits on the network; a generate runs up to 4 variations.
struct NetworkPool {
    juce::ThreadPool pool{juce::ThreadPoolOptions{}.withThreadName("Flowstate network").withNumberOfThreads(8)};
};

// The service sends a keepalive comment every 15 s, so this much silence means it is gone.
constexpr int kIdleTimeoutMs = 45000;

fb::ErrorInfo error(fb::ErrorCode code, std::string message) { return fb::ErrorInfo{code, std::move(message)}; }

}  // namespace

struct ServiceClient::Shared {
    EventFn onEvent;
    EndFn onEnd;
    bool alive = true;  // message thread only

    std::mutex mutex;
    std::map<std::string, juce::WebInputStream*> open;  // streams being read, so cancel can abort them
    std::set<std::string> cancelled;

    bool isCancelled(const std::string& id) {
        const std::lock_guard lock(mutex);
        return cancelled.count(id) > 0;
    }

    // Posts to the message thread; dropped if the client is gone by then.
    static void post(std::shared_ptr<Shared> shared, std::function<void(Shared&)> fn) {
        juce::MessageManager::callAsync([shared = std::move(shared), fn = std::move(fn)] {
            if (shared->alive) fn(*shared);
        });
    }
};

struct ServiceClient::Pool {
    juce::SharedResourcePointer<NetworkPool> shared;
};

namespace {

class PlanJob final : public juce::ThreadPoolJob {
public:
    PlanJob(std::shared_ptr<ServiceClient::Shared> shared, std::string streamId, juce::URL url, juce::String headers)
        : juce::ThreadPoolJob("Flowstate plan " + streamId),
          shared_(std::move(shared)),
          streamId_(std::move(streamId)),
          url_(std::move(url)),
          headers_(std::move(headers)) {}

    JobStatus runJob() override {
        auto end = run();
        if (!shared_->isCancelled(streamId_))
            ServiceClient::Shared::post(shared_, [id = streamId_, end](ServiceClient::Shared& s) { s.onEnd(id, end); });
        const std::lock_guard lock(shared_->mutex);
        shared_->cancelled.erase(streamId_);
        return jobHasFinished;
    }

private:
    // Returns the error that ended the stream, or null if it closed normally.
    std::optional<fb::ErrorInfo> run() {
        juce::WebInputStream stream(url_, false);
        stream.withExtraHeaders(headers_);
        stream.withConnectionTimeout(kIdleTimeoutMs);
        {
            const std::lock_guard lock(shared_->mutex);
            if (shared_->cancelled.count(streamId_) > 0) return std::nullopt;
            shared_->open[streamId_] = &stream;
        }
        auto result = read(stream);
        const std::lock_guard lock(shared_->mutex);
        shared_->open.erase(streamId_);
        return result;
    }

    std::optional<fb::ErrorInfo> read(juce::WebInputStream& stream) {
        const auto unreachable = error(fb::ErrorCode::Unavailable,
                                       "Can't reach the agent service at " + ServiceClient::serviceUrl().toStdString() + ".");
        if (!stream.connect(nullptr) || stream.isError()) return unreachable;
        const int status = stream.getStatusCode();
        if (status == 0) return unreachable;

        // Before the stream starts, the service answers errors with HTTP 400 or 503 and a stream
        // holding one `error` event; read it the same way.
        SseParser parser;
        bool sawEnd = false;
        const auto deliver = [&](const std::vector<std::string>& payloads) -> std::optional<fb::ErrorInfo> {
            for (const auto& data : payloads) {
                fb::ServiceEvent event;
                try {
                    fb::from_json(nlohmann::json::parse(data), event);
                } catch (const std::exception& e) {
                    return error(fb::ErrorCode::Internal, std::string("The agent service sent an event this build can't read: ") + e.what());
                }
                sawEnd = std::holds_alternative<fb::ScoreDone>(event) || std::holds_alternative<fb::ServiceError>(event);
                ServiceClient::Shared::post(shared_, [id = streamId_, event](ServiceClient::Shared& s) { s.onEvent(id, event); });
                if (sawEnd) return std::nullopt;
            }
            return std::nullopt;
        };

        // WinINet's InternetReadFile, and JUCE's macOS stream (juce_Network_mac.mm, read() loops
        // until the buffer is full or the request ends), wait for the whole buffer, which would
        // hold events back; read byte by byte there. Linux's curl backend returns what has arrived.
#if JUCE_WINDOWS || JUCE_MAC
        constexpr int kChunk = 1;
#else
        constexpr int kChunk = 8192;
#endif
        char buffer[kChunk];
        auto lastData = juce::Time::getMillisecondCounter();
        while (!sawEnd && !shouldExit() && !shared_->isCancelled(streamId_)) {
            const int n = stream.read(buffer, kChunk);
            if (n > 0) {
                lastData = juce::Time::getMillisecondCounter();
                try {
                    if (auto e = deliver(parser.feed(buffer, static_cast<std::size_t>(n)))) return e;
                } catch (const std::length_error&) {
                    return error(fb::ErrorCode::Internal, "The agent service sent an event that is too large.");
                }
                continue;
            }
            if (stream.isExhausted() || stream.isError()) break;
            if (juce::Time::getMillisecondCounter() - lastData > static_cast<juce::uint32>(kIdleTimeoutMs))
                return error(fb::ErrorCode::Network, "The agent service stopped responding.");
            juce::Thread::sleep(5);
        }
        if (!sawEnd)
            if (auto e = deliver(parser.finish())) return e;
        if (!sawEnd && status != 200)
            return error(status >= 500 ? fb::ErrorCode::Unavailable : fb::ErrorCode::BadRequest,
                         "The agent service answered HTTP " + std::to_string(status) + ".");
        return std::nullopt;
    }

    std::shared_ptr<ServiceClient::Shared> shared_;
    std::string streamId_;
    juce::URL url_;
    juce::String headers_;
};

}  // namespace

ServiceClient::ServiceClient(std::string instanceId, EventFn onEvent, EndFn onEnd)
    : instanceId_(std::move(instanceId)), shared_(std::make_shared<Shared>()), pool_(std::make_unique<Pool>()) {
    shared_->onEvent = std::move(onEvent);
    shared_->onEnd = std::move(onEnd);
}

ServiceClient::~ServiceClient() {
    shared_->alive = false;
    {
        const std::lock_guard lock(shared_->mutex);
        for (auto& [id, stream] : shared_->open) {
            shared_->cancelled.insert(id);
            stream->cancel();
        }
    }
    // Running jobs keep their own reference to shared_ and end soon; if this is the last
    // client, the pool waits for them as it goes.
}

juce::File ServiceClient::serviceFile() {
    const auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
#if JUCE_MAC
    return base.getChildFile("Application Support/Flowstate/service.json");
#else
    return base.getChildFile("Flowstate/service.json");
#endif
}

ServiceSettings ServiceClient::settings() {
    std::optional<std::string> file;
    if (const auto f = serviceFile(); f.existsAsFile()) file = f.loadFileAsString().toStdString();
    return resolveServiceSettings(juce::SystemStats::getEnvironmentVariable("FLOWSTATE_SERVICE_URL", {}).toStdString(),
                                  juce::SystemStats::getEnvironmentVariable("FLOWSTATE_SERVICE_TOKEN", {}).toStdString(),
                                  file);
}

std::optional<fb::ErrorInfo> ServiceClient::startPlan(const std::string& streamId, const fb::PlanRequest& request) {
    nlohmann::json body;
    fb::to_json(body, request);
    const auto text = body.dump();
    const auto service = settings();
    const auto url = juce::URL(juce::String(service.url) + "/v1/plan").withPOSTData(juce::MemoryBlock(text.data(), text.size()));
    // The request id joins the plugin's and the service's logs. A BYOK key would go in
    // x-flowstate-provider-key, read from the keychain here (P1-12), never from the session.
    // The tester token (hosted service, P1-13) is read here per request and never stored or logged.
    auto headers = juce::String("Content-Type: application/json\r\nAccept: text/event-stream\r\n") +
                   "x-flowstate-request-id: " + juce::String(instanceId_.substr(0, 8)) + "-" + juce::String(streamId);
    if (!service.token.empty()) headers << "\r\nAuthorization: Bearer " << juce::String(service.token);
    {
        const std::lock_guard lock(shared_->mutex);
        shared_->cancelled.erase(streamId);
    }
    pool_->shared->pool.addJob(new PlanJob(shared_, streamId, url, headers), true);
    return std::nullopt;
}

void ServiceClient::cancel(const std::string& streamId) {
    const std::lock_guard lock(shared_->mutex);
    shared_->cancelled.insert(streamId);
    if (const auto it = shared_->open.find(streamId); it != shared_->open.end()) it->second->cancel();
}

}  // namespace flowstate::plugin
