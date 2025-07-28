#pragma once

#include <vector>
#include <WString.h>

namespace LuaApps::Runtime
{
    int runApp(const char *path, const std::vector<String> &args);
}
