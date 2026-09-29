// Shared helpers for tests.
#pragma once

#include "flowstate/realize.h"

#include <fstream>
#include <sstream>
#include <string>

inline std::string readFixture(const std::string& name) {
    std::ifstream f(std::string(FLOWSTATE_FIXTURES_DIR) + "/" + name, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
