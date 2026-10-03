// Windows Credential Manager: generic credentials named "Flowstate/<provider>", persisted for this
// user on this machine (not roaming).
#include "OsKeyStore.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincred.h>

#include <vector>

namespace flowstate::plugin {

namespace {

std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

std::string failure(const char* what) {
    return std::string("Windows Credential Manager couldn't ") + what + " the key (error " + std::to_string(GetLastError()) + ").";
}

class WindowsKeyStore final : public KeyStore {
public:
    explicit WindowsKeyStore(std::string service) : service_(std::move(service)) {}

    std::optional<std::string> write(const std::string& provider, const std::string& key) override {
        auto target = targetName(provider);
        auto user = wide(service_);
        std::vector<BYTE> blob(key.begin(), key.end());
        CREDENTIALW cred{};
        cred.Type = CRED_TYPE_GENERIC;
        cred.TargetName = target.data();
        cred.UserName = user.data();
        cred.CredentialBlobSize = static_cast<DWORD>(blob.size());
        cred.CredentialBlob = blob.data();
        cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
        const bool written = CredWriteW(&cred, 0) != FALSE;
        const auto error = written ? std::optional<std::string>() : failure("store");
        SecureZeroMemory(blob.data(), blob.size());
        return error;
    }

    std::optional<std::string> remove(const std::string& provider) override {
        if (!CredDeleteW(targetName(provider).c_str(), CRED_TYPE_GENERIC, 0) && GetLastError() != ERROR_NOT_FOUND) return failure("remove");
        return std::nullopt;
    }

    std::optional<std::string> read(const std::string& provider) override {
        PCREDENTIALW cred = nullptr;
        if (!CredReadW(targetName(provider).c_str(), CRED_TYPE_GENERIC, 0, &cred)) return std::nullopt;
        std::string key(reinterpret_cast<const char*>(cred->CredentialBlob), cred->CredentialBlobSize);
        CredFree(cred);
        return key;
    }

    bool contains(const std::string& provider) override {
        PCREDENTIALW cred = nullptr;
        if (!CredReadW(targetName(provider).c_str(), CRED_TYPE_GENERIC, 0, &cred)) return false;
        CredFree(cred);
        return true;
    }

private:
    std::wstring targetName(const std::string& provider) const { return wide(service_ + "/" + provider); }

    std::string service_;
};

}  // namespace

std::unique_ptr<KeyStore> makeOsKeyStore(const std::string& service) { return std::make_unique<WindowsKeyStore>(service); }

}  // namespace flowstate::plugin
