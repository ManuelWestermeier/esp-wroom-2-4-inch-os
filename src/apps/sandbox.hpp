#pragma once

#include <Arduino.h>

extern "C"
{
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

namespace LuaApps::Sandbox
{
    lua_State *createRestrictedLuaState(const String &path);
}
