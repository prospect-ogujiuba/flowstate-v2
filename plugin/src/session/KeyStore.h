// BYOK keys in the OS keychain (P1-12): macOS Keychain, Windows Credential Manager. One key per
// provider id. A key goes from `setApiKey` straight into the store and is read back only to send
// one request's x-flowstate-provider-key header; it is never in the session, DAW state or logs.
// JUCE-free; the plugin supplies the OS store (OsKeyStore.h) and the tests a fake. Message thread.
#pragma once

#include <cstddef>
#include <optional>
#include <string>

namespace flowstate::plugin {

class KeyStore {
public:
    virtual ~KeyStore() = default;
    // Each returns an error for the UI (never holding the key), or nullopt on success.
    virtual std::optional<std::string> write(const std::string& provider, const std::string& key) = 0;
    // Removing a key that isn't there succeeds.
    virtual std::optional<std::string> remove(const std::string& provider) = 0;
    // The stored key, or nullopt when there is none or it can't be read.
    virtual std::optional<std::string> read(const std::string& provider) = 0;
    // Whether a key is stored, without reading it (no keychain prompt).
    virtual bool contains(const std::string& provider) = 0;
};

// The longest key accepted. Windows Credential Manager holds 2560 bytes; provider keys are far shorter.
inline constexpr std::size_t kMaxKeyBytes = 2048;

// Provider ids are pi-ai's ("openai", "openrouter", "google-vertex"): lowercase letters, digits, '-', '_', '.'.
bool validProviderId(const std::string& provider);
// A key is printable ASCII with no spaces, so it can't break the header it travels in.
bool validKey(const std::string& key);

}  // namespace flowstate::plugin
