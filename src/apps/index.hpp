
#pragma once

#include <Arduino.h>
#include <vector>
#include <FS.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <WiFiClient.h>

extern "C"
{
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

namespace LuaApps
{

    // -------- LuaSandbox --------
    namespace Sandbox
    {

        int luaPrint(lua_State *L)
        {
            const char *msg = luaL_checkstring(L, 1);
            Serial.println(msg);
            return 0;
        }

        lua_State *createRestrictedLuaState()
        {
            lua_State *L = luaL_newstate();
            luaL_openlibs(L);
            lua_pushnil(L);
            lua_setglobal(L, "io");
            lua_pushnil(L);
            lua_setglobal(L, "os");
            lua_pushnil(L);
            lua_setglobal(L, "package");
            lua_register(L, "print", luaPrint);
            return L;
        }
    }

    // -------- LuaNetwork --------
    namespace Network
    {
        bool connectWiFi(const char *ssid, const char *password)
        {
            WiFi.begin(ssid, password);
            unsigned long start = millis();
            while (WiFi.status() != WL_CONNECTED && millis() - start < 10000)
            {
                delay(500);
            }
            return WiFi.status() == WL_CONNECTED;
        }

        WiFiServer *createServer(uint16_t port)
        {
            WiFiServer *server = new WiFiServer(port);
            server->begin();
            return server;
        }

        WiFiClient connectToHost(const char *host, uint16_t port)
        {
            WiFiClient client;
            client.connect(host, port);
            return client;
        }
    }

    // -------- LuaRuntime --------
    namespace Runtime
    {
        static int lastExitCode = 0;

        int lua_exitApp(lua_State *L)
        {
            int code = luaL_checkinteger(L, 1);
            lastExitCode = code;
            lua_pushstring(L, "exit");
            return lua_error(L);
        }

        int runApp(const char *path, const std::vector<String> &args)
        {
            lua_State *L = LuaApps::Sandbox::createRestrictedLuaState();
            lua_register(L, "exitApp", lua_exitApp);

            File file = SPIFFS.open(path);
            if (!file)
                return -1;
            String content = file.readString();
            file.close();

            if (luaL_dostring(L, content.c_str()) != LUA_OK)
            {
                Serial.printf("Lua Error: %s\n", lua_tostring(L, -1));
                lua_close(L);
                return -2;
            }

            lua_close(L);
            return lastExitCode;
        }
    }

    // -------- LuaApp --------
    class App
    {
    public:
        App(const String &name, const String &fromPath, const std::vector<String> &args)
            : path(name), origin(fromPath), arguments(args) {}

        int run()
        {
            result = Runtime::runApp(path.c_str(), arguments);
            return result;
        }

        int exitCode() const
        {
            return result;
        }

    private:
        String path;
        String origin;
        std::vector<String> arguments;
        int result = 0;
    };

    // -------- System Interface --------
    void initialize()
    {
        Serial.begin(115200);
        SPIFFS.begin(true);
        Serial.println("LuaApps initialized.");
    }

    int runApp(const String &path, const std::vector<String> &args = {})
    {
        App app(path, "/system", args);
        return app.run();
    }
}
