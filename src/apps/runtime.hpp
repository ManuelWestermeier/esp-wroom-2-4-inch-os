#pragma once

#include <vector>
#include <WString.h>

namespace LuaApps::Runtime
{
    int runApp(String path, const std::vector<String> &args);
}
