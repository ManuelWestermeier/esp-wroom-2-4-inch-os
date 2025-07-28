#pragma once

extern "C"
{
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

namespace LuaApps::Sandbox
{
    lua_State *createRestrictedLuaState();
}
