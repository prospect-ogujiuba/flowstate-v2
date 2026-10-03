#include "session/KeyStore.h"

namespace flowstate::plugin {

bool validProviderId(const std::string& provider) {
    if (provider.empty() || provider.size() > 64) return false;
    for (const char ch : provider)
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.')) return false;
    return true;
}

bool validKey(const std::string& key) {
    if (key.empty() || key.size() > kMaxKeyBytes) return false;
    for (const char ch : key)
        if (ch < 0x21 || ch > 0x7e) return false;
    return true;
}

}  // namespace flowstate::plugin
