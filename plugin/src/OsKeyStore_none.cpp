#include "OsKeyStore.h"

namespace flowstate::plugin {

std::unique_ptr<KeyStore> makeOsKeyStore(const std::string&) { return nullptr; }

}  // namespace flowstate::plugin
