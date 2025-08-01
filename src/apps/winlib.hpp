#pragma once
#include <lua.hpp>

namespace LuaApps::WinLib
{

    int lua_createWindow(lua_State *L);
    int lua_WIN_setName(lua_State *L);
    int lua_WIN_getRect(lua_State *L);
    int lua_WIN_getLastEvent(lua_State *L);
    int lua_WIN_closed(lua_State *L);
    int lua_WIN_fillBg(lua_State *L);
    int lua_WIN_writeText(lua_State *L);
    int lua_WIN_writeRect(lua_State *L);
    int lua_WIN_setIcon(lua_State *L);
    int lua_WIN_drawImage(lua_State *L);
    int lua_WIN_isRendered(lua_State *L);

    void register_win_functions(lua_State *L);

} // namespace LuaApps::WinLib
