#pragma once

#include <vector>
#include <WString.h>

namespace LuaApps
{

    class App
    {
    public:
        App(const String &name, const String &fromPath, const std::vector<String> &args);
        int run();
        int exitCode() const;

    private:
        String path;
        String origin;
        std::vector<String> arguments;
        int result = 0;
    };

}
