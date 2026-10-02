// The plugin's client for the agent service (docs/bridge-spec.md, "Plugin ↔ agent service"):
// POST /v1/plan, then the SSE stream, one ServiceEvent per `data:` line.
//
// Threads (docs/threading.md rule 3): streams run on the process's network pool, never on the
// audio or message thread. Events and the end of each stream are posted to the message thread,
// in order. Cancelling aborts the HTTP request, which stops the provider stream on the service.
#pragma once

#include "flowstate/bridge.h"
#include "session/ServiceSettings.h"

#include <juce_core/juce_core.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace flowstate::plugin {

namespace fb = flowstate::bridge;

class ServiceClient {
public:
    using EventFn = std::function<void(const std::string& streamId, const fb::ServiceEvent&)>;
    using EndFn = std::function<void(const std::string& streamId, std::optional<fb::ErrorInfo> error)>;

    // The callbacks run on the message thread, and never after this client is destroyed.
    ServiceClient(std::string instanceId, EventFn onEvent, EndFn onEnd);
    // Cancels every stream. Message thread.
    ~ServiceClient();

    // Starts streaming one PlanRequest. Message thread.
    std::optional<fb::ErrorInfo> startPlan(const std::string& streamId, const fb::PlanRequest& request);
    // Aborts a stream; nothing more is reported for it. Any thread.
    void cancel(const std::string& streamId);

    // The service's URL and tester token: FLOWSTATE_SERVICE_URL / FLOWSTATE_SERVICE_TOKEN, else the
    // user's service.json (serviceFile()), else the local dev service (`npm run serve`) with no token.
    static ServiceSettings settings();
    static juce::String serviceUrl() { return settings().url; }
    // %APPDATA%\Flowstate\service.json on Windows, ~/Library/Application Support/Flowstate/service.json
    // on macOS, ~/.config/Flowstate/service.json on Linux.
    static juce::File serviceFile();

    struct Shared;
    struct Pool;

private:
    std::string instanceId_;
    std::shared_ptr<Shared> shared_;
    std::unique_ptr<Pool> pool_;
};

}  // namespace flowstate::plugin
