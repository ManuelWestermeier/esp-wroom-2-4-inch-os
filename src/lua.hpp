#pragma once

#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>

// Lua headers from mischief/arduino-lua
extern "C"
{
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

namespace LuaSandbox
{

  namespace
  {
    lua_State *L = nullptr;

    // --- C++ functions exposed to Lua ---
    int luaPrintSerial(lua_State *L)
    {
      const char *msg = luaL_checkstring(L, 1);
      Serial.println(msg);
      return 0;
    }

    int setLED(lua_State *L)
    {
      auto state = luaL_checkinteger(L, 1) == 1 ? HIGH : LOW;
      pinMode(2, OUTPUT);
      digitalWrite(2, state);
      return 0;
    }

    int luaDelay(lua_State *L)
    {
      int time = luaL_checkinteger(L, 1);
      delay(time);
      return 0;
    }

    void register_default_functions(lua_State *L)
    {
      lua_register(L, "print", luaPrintSerial);
      lua_register(L, "setLED", setLED);
      lua_register(L, "delay", luaDelay);
    }
  }

  // --- Utility Functions ---

  // Initialize Lua environment
  void init()
  {
    if (L)
    {
      lua_close(L);
    }
    L = luaL_newstate();
    luaL_openlibs(L); // base, string, etc.
    register_default_functions(L);
  }

  // Add a custom function to Lua
  void addFunction(const char *name, lua_CFunction fn)
  {
    if (L)
    {
      lua_register(L, name, fn);
    }
  }

  // Execute a Lua file from SPIFFS
  void runFile(const char *path)
  {
    if (!L)
    {
      Serial.println("Lua VM not initialized. Call LuaSandbox::init() first.");
      return;
    }

    File file = SPIFFS.open(path);
    if (!file || file.isDirectory())
    {
      Serial.println("Lua file missing or invalid");
      return;
    }

    String script = file.readString();
    file.close();

    if (luaL_dostring(L, script.c_str()) != LUA_OK)
    {
      Serial.print("Lua Error: ");
      Serial.println(lua_tostring(L, -1));
    }
  }

  // Clean up Lua state
  void cleanup()
  {
    if (L)
    {
      lua_close(L);
      L = nullptr;
    }
  }

  // Optional: Access the raw Lua state if needed
  lua_State *getState()
  {
    return L;
  }

} // namespace LuaSandbox
