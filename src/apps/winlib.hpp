#pragma once

#include <Arduino.h>
#include <lua.hpp>

#include "../io/read-string.hpp"
#include "../screen/svg.hpp"
#include "app.hpp"

namespace LuaApps::WinLib
{
    // Lua window management bindings
    int lua_createWindow(lua_State *L);
    int lua_WIN_setName(lua_State *L);
    int lua_WIN_getRect(lua_State *L);
    int lua_WIN_getLastEvent(lua_State *L);
    int lua_WIN_closed(lua_State *L);
    int lua_WIN_close(lua_State *L);
    int lua_WIN_fillBg(lua_State *L);
    int lua_WIN_writeText(lua_State *L);
    int lua_WIN_setIcon(lua_State *L);
    int lua_WIN_drawPixel(lua_State *L);
    int lua_WIN_drawImage(lua_State *L);
    int lua_WIN_canAccess(lua_State *L);
    int lua_WIN_isRendered(lua_State *L);
    int lua_WIN_readText(lua_State *L);

    // --- Neue TFT/TFT_eSPI Zeichen-Funktionen ---
    int lua_WIN_drawLine(lua_State *L);
    int lua_WIN_drawRect(lua_State *L); // Outline-Rect
    int lua_WIN_drawTriangle(lua_State *L);
    int lua_WIN_fillTriangle(lua_State *L);
    int lua_WIN_drawCircle(lua_State *L);
    int lua_WIN_fillCircle(lua_State *L);
    int lua_WIN_drawRoundRect(lua_State *L);
    int lua_WIN_fillRoundRect(lua_State *L);
    int lua_WIN_drawFastVLine(lua_State *L);
    int lua_WIN_drawFastHLine(lua_State *L);
    int lua_WIN_drawSVG(lua_State *L);
    // --- Ende neue Funktionen ---

    // Registration of functions to Lua
    void register_win_functions(lua_State *L);

} // namespace LuaApps::WinLib
