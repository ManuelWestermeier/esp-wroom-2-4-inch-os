#pragma once

#include <vector>
#include <WString.h>

namespace LuaApps {
    void initialize();
    int runApp(const String &path, const std::vector<String> &args = {});
}
