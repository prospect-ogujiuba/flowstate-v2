// macOS Keychain through the SecItem API: generic passwords, service "Flowstate", account = provider.
#include "OsKeyStore.h"

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

namespace flowstate::plugin {

namespace {

// Owns one CoreFoundation reference.
template <typename T>
struct CfRef {
    T ref = nullptr;
    explicit CfRef(T r) : ref(r) {}
    ~CfRef() {
        if (ref != nullptr) CFRelease(ref);
    }
    CfRef(const CfRef&) = delete;
    CfRef& operator=(const CfRef&) = delete;
};

CFStringRef cfString(const std::string& s) {
    return CFStringCreateWithBytes(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(s.data()),
                                   static_cast<CFIndex>(s.size()), kCFStringEncodingUTF8, false);
}

std::string failure(const char* what, OSStatus status) {
    return std::string("The keychain couldn't ") + what + " the key (error " + std::to_string(static_cast<int>(status)) + ").";
}

class MacKeyStore final : public KeyStore {
public:
    explicit MacKeyStore(std::string service) : service_(std::move(service)) {}

    std::optional<std::string> write(const std::string& provider, const std::string& key) override {
        CfRef query(itemQuery(provider));
        CfRef data(CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(key.data()), static_cast<CFIndex>(key.size())));
        CfRef update(CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
        CFDictionarySetValue(update.ref, kSecValueData, data.ref);
        auto status = SecItemUpdate(query.ref, update.ref);
        if (status == errSecItemNotFound) {
            CFDictionarySetValue(query.ref, kSecValueData, data.ref);
            status = SecItemAdd(query.ref, nullptr);
        }
        if (status != errSecSuccess) return failure("store", status);
        return std::nullopt;
    }

    std::optional<std::string> remove(const std::string& provider) override {
        CfRef query(itemQuery(provider));
        const auto status = SecItemDelete(query.ref);
        if (status != errSecSuccess && status != errSecItemNotFound) return failure("remove", status);
        return std::nullopt;
    }

    std::optional<std::string> read(const std::string& provider) override {
        CfRef query(itemQuery(provider));
        CFDictionarySetValue(query.ref, kSecReturnData, kCFBooleanTrue);
        CFDictionarySetValue(query.ref, kSecMatchLimit, kSecMatchLimitOne);
        CFTypeRef result = nullptr;
        if (SecItemCopyMatching(query.ref, &result) != errSecSuccess || result == nullptr) return std::nullopt;
        CfRef owned(result);
        if (CFGetTypeID(result) != CFDataGetTypeID()) return std::nullopt;
        const auto data = static_cast<CFDataRef>(result);
        return std::string(reinterpret_cast<const char*>(CFDataGetBytePtr(data)), static_cast<std::size_t>(CFDataGetLength(data)));
    }

    bool contains(const std::string& provider) override {
        // Attributes only: reading the secret could prompt for keychain access.
        CfRef query(itemQuery(provider));
        CFDictionarySetValue(query.ref, kSecReturnAttributes, kCFBooleanTrue);
        CFDictionarySetValue(query.ref, kSecMatchLimit, kSecMatchLimitOne);
        CFTypeRef result = nullptr;
        const auto status = SecItemCopyMatching(query.ref, &result);
        if (result != nullptr) CFRelease(result);
        return status == errSecSuccess;
    }

private:
    CFMutableDictionaryRef itemQuery(const std::string& provider) const {
        auto q = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CfRef service(cfString(service_));
        CfRef account(cfString(provider));
        CFDictionarySetValue(q, kSecClass, kSecClassGenericPassword);
        CFDictionarySetValue(q, kSecAttrService, service.ref);
        CFDictionarySetValue(q, kSecAttrAccount, account.ref);
        return q;
    }

    std::string service_;
};

}  // namespace

std::unique_ptr<KeyStore> makeOsKeyStore(const std::string& service) { return std::make_unique<MacKeyStore>(service); }

}  // namespace flowstate::plugin
