#pragma once

#include <unordered_set>
#include <WString.h>

#include "window.hpp"
#include "functions.hpp"

#include "../fs/index.hpp"
#include "../fs/enc-fs.hpp"

extern "C"
{
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

namespace LuaApps
{
    namespace LuaFunctions
    {
        void register_default_functions(lua_State *L);
    };

    static lua_State *createRestrictedLuaState();

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

        std::unordered_set<int> windows;
    };

    App *getApp(lua_State *L);
}
