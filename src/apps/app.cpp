#include "app.hpp"

namespace LuaApps
{
    static lua_State *createRestrictedLuaState(const String &path)
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

    App::App(const String &name, const std::vector<String> &args)
        : path(name), arguments(args), lastExitCode(0) {}

    int App::run()
    {
        lua_State *L = createRestrictedLuaState(path);

        // Pass "this" to lua_exitApp
        lua_pushlightuserdata(L, this);
        
        lua_pushcclosure(L, [](lua_State *L) -> int
                         {
            int code = luaL_checkinteger(L, 1);
            App* app = reinterpret_cast<App*>(lua_touserdata(L, lua_upvalueindex(1)));
            if (app) app->lastExitCode = code;
            lua_pushstring(L, (String("exit with code: ") + code).c_str());
            return lua_error(L); }, 1);
        lua_setglobal(L, "exitApp");

        lua_newtable(L);
        for (size_t i = 0; i < arguments.size(); ++i)
        {
            lua_pushinteger(L, i + 1);
            lua_pushstring(L, arguments[i].c_str());
            lua_settable(L, -3);
        }
        lua_setglobal(L, "args");

        String content = SD_FS::readFile(path + "entry.lua");
        Serial.println("RUNNING: " + path + "entry.lua");

        if (luaL_dostring(L, content.c_str()) != LUA_OK)
        {
            Serial.printf("Lua Error: %s\n", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_close(L);
            if (lastExitCode == 0)
                lastExitCode = -1;
            return lastExitCode;
        }

        lua_close(L);
        return lastExitCode;
    }

    int App::exitCode() const { return lastExitCode; }
}
