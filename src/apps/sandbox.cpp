#include "sandbox.hpp"
#include "functions.hpp"

namespace LuaApps::Sandbox {

    lua_State *createRestrictedLuaState() {
        lua_State *L = luaL_newstate();
        luaL_openlibs(L);

        // Entferne gefährliche libs
        lua_pushnil(L);
        lua_setglobal(L, "io");
        lua_pushnil(L);
        lua_setglobal(L, "os");
        lua_pushnil(L);
        lua_setglobal(L, "package");

        // Registriere sichere Funktionen
        LuaFunctions::register_default_functions(L);

        return L;
    }

}
