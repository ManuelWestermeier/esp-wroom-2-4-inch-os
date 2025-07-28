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

namespace LuaApps::LuaFunctions
{
    int luaPrintSerial(lua_State *L);
    int setLED(lua_State *L);
    int luaDelay(lua_State *L);
    int luaHttpRequest(lua_State *L);
    int luaHttpsRequest(lua_State *L);
    int connectToWofi(lua_State *L);
    void register_default_functions(lua_State *L);
}
