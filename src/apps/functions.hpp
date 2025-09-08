#pragma once

#include <Arduino.h>
#include <map>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

extern "C"
{
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#include "winlib.hpp"
#include "fslib.hpp"
#include "../auth/auth.hpp"
#include "../wifi/index.hpp"
#include "../fs/enc-fs.hpp"
#include "index.hpp"
#include "../styles/global.hpp"

namespace LuaApps::LuaFunctions
{
    int luaPrintSerial(lua_State *L);
    int luaExec(lua_State *L);
    int setLED(lua_State *L);
    int lua_RGB(lua_State *L);
    int lua_getTheme(lua_State *L);
    int luaDelay(lua_State *L);
    int luaHttpRequest(lua_State *L);
    int luaHttpsRequest(lua_State *L);
    void register_default_functions(lua_State *L);
}
