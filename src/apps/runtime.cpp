#include "runtime.hpp"
#include "sandbox.hpp"
#include "functions.hpp"

#include "../fs/index.hpp"

extern "C"
{
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

namespace LuaApps::Runtime
{

    static int lastExitCode = 0;

    int lua_exitApp(lua_State *L)
    {
        int code = luaL_checkinteger(L, 1);
        lastExitCode = code;
        lua_pushstring(L, (String("exit with code: ") + code).c_str());
        return lua_error(L);
    }

    int runApp(String path, const std::vector<String> &args)
    {
        lua_State *L = Sandbox::createRestrictedLuaState();
        lua_register(L, "exitApp", lua_exitApp);

        lua_newtable(L);
        for (size_t i = 0; i < args.size(); ++i)
        {
            lua_pushnumber(L, i + 1);
            lua_pushstring(L, args[i].c_str());
            lua_settable(L, -3);
        }
        lua_setglobal(L, "args");

        String content = SD_FS::readFile(path);

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

}
