#include "fslib.hpp"
#include <iostream> // oder was du brauchst

namespace LuaApps
{
    namespace FsLib
    {
        int lua_FS_get(lua_State *L)
        {
            // dein Code hier
            return 1;
        }

        int lua_FS_set(lua_State *L)
        {
            // dein Code hier
            return 0;
        }

        void register_fs_functions(lua_State *L)
        {
            lua_register(L, "FS_get", lua_FS_get);
            lua_register(L, "FS_set", lua_FS_set);
        }
    }
}
