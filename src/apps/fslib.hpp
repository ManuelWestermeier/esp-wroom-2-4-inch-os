#pragma once

#include <Arduino.h>
#include <lua.hpp>

#include "app.hpp"

namespace LuaApps::FsLib
{
    int lua_FS_get(lua_State *L) {};
    int lua_FS_set(lua_State *L) {};

    // Registration of functions to Lua
    void register_fs_functions(lua_State *L) {};

} // namespace LuaApps::FsLib
