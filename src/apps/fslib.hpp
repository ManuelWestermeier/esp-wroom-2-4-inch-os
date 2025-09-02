#pragma once

#include <Arduino.h>
#include <lua.hpp>

#include "app.hpp"
#include "../fs/enc-fs.hpp"

namespace LuaApps {
    namespace FsLib {
        int lua_FS_get(lua_State* L);
        int lua_FS_set(lua_State* L);
        int lua_FS_del(lua_State* L);
        
        void register_fs_functions(lua_State* L);
    }
}
