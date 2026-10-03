// The OS keychain behind KeyStore (P1-12): macOS Keychain (generic passwords, service `service`,
// account = provider id) and Windows Credential Manager (generic credentials named
// "<service>/<provider>", persisted for this user on this machine). Null on Linux, whose builds are
// for development: there the Studio lists `apiKey` as unavailable.
#pragma once

#include "session/KeyStore.h"

#include <memory>
#include <string>

namespace flowstate::plugin {

inline constexpr const char* kKeyStoreService = "Flowstate";

std::unique_ptr<KeyStore> makeOsKeyStore(const std::string& service = kKeyStoreService);

}  // namespace flowstate::plugin
