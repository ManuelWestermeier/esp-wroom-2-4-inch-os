#pragma once

#include <vector>
#include <WString.h>

#include "functions.hpp"

#include "../fs/index.hpp"

extern "C"
{
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

namespace LuaApps
{
    static lua_State *createRestrictedLuaState(const String &path);

    static int lua_exitApp(lua_State *L);

    class App
    {
    public:
        App(const String &name, const std::vector<String> &args);
        int run();
        int exitCode() const;
        int lastExitCode = 0;
        String path;
        std::vector<String> arguments;
    };

    static App *getApp(lua_State *L);
}
