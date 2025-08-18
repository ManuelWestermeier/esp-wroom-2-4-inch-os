#include "sandbox.hpp"
#include "functions.hpp"

namespace LuaApps::Sandbox
{

    lua_State *createRestrictedLuaState(const String &path)
    {
        lua_State *L = luaL_newstate();

        // Open only safe libraries
        luaL_requiref(L, "_G", luaopen_base, 1);
        lua_pop(L, 1);
        luaL_requiref(L, "table", luaopen_table, 1);
        lua_pop(L, 1);
        luaL_requiref(L, "string", luaopen_string, 1);
        lua_pop(L, 1);
        luaL_requiref(L, "math", luaopen_math, 1);
        lua_pop(L, 1);
        luaL_requiref(L, "coroutine", luaopen_coroutine, 1);
        lua_pop(L, 1);
        luaL_requiref(L, "utf8", luaopen_utf8, 1);
        lua_pop(L, 1);

        // remove unwanted base functions
        lua_pushnil(L);
        lua_setglobal(L, "print");
        lua_pushnil(L);
        lua_setglobal(L, "dofile");
        lua_pushnil(L);
        lua_setglobal(L, "loadfile");
        lua_pushnil(L);
        lua_setglobal(L, "load");
        lua_pushnil(L);
        lua_setglobal(L, "loadstring");

        // Registriere sichere Funktionen
        LuaFunctions::register_default_functions(L, path);

        return L;
    }

}
