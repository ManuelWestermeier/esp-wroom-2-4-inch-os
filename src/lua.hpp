#include <Arduino.h>

#include <FS.h>
#include <SPIFFS.h>

// Lua headers from mischief/arduino-lua
extern "C" {
  #include "lua.h"
  #include "lauxlib.h"
  #include "lualib.h"
}

// --- C++ functions exposed to Lua ---
int lua_print_serial(lua_State* L) {
  const char* msg = luaL_checkstring(L, 1);
  Serial.println(msg);
  return 0;
}

int lua_toggle_led(lua_State* L) {
  int pin = luaL_checkinteger(L, 1);
  pinMode(pin, OUTPUT);
  int state = digitalRead(pin);
  digitalWrite(pin, !state);
  return 0;
}

void register_my_functions(lua_State* L) {
  lua_register(L, "print_serial", lua_print_serial);
  lua_register(L, "toggle_led", lua_toggle_led);
}

// --- Run a Lua script from SPIFFS ---
void run_lua_file(const char* path) {
  lua_State* L = luaL_newstate();
  luaopen_base(L);
  luaopen_string(L);
  register_my_functions(L);

  File file = SPIFFS.open(path);
  if (!file || file.isDirectory()) {
    Serial.println("Lua file missing or invalid");
    lua_close(L);
    return;
  }

  String script = file.readString();
  file.close();

  if (luaL_dostring(L, script.c_str()) != LUA_OK) {
    Serial.print("Lua Error: ");
    Serial.println(lua_tostring(L, -1));
  }
  lua_close(L);
}