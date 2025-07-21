#pragma once

#include <Arduino.h>
#include <vector>
#include <map>

#include <FS.h>
#include <SPIFFS.h>

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

extern "C"
{
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

namespace LuaApps
{

    // --- C++ Funktionen für Lua ---
    namespace LuaFunctions
    {

        int luaPrintSerial(lua_State *L)
        {
            const char *msg = luaL_tolstring(L, 1, NULL);
            Serial.println(msg);
            lua_pop(L, 1); // remove the string result from the stack
            return 0;
        }

        int setLED(lua_State *L)
        {
            int state = luaL_checkinteger(L, 1) == 1 ? HIGH : LOW;
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

        int luaHttpRequest(lua_State *L)
        {
            if (WiFi.status() != WL_CONNECTED)
            {
                Serial.println("WLAN nicht verbunden!");
                lua_pushstring(L, "Not connected to WiFi");
                return lua_error(L);
            }

            luaL_checktype(L, 1, LUA_TTABLE);

            const char *method = "GET";
            const char *url = nullptr;
            String body = "";
            std::map<String, String> headers;

            // --- Extract fields from Lua table ---
            lua_getfield(L, 1, "method");
            if (!lua_isnil(L, -1))
                method = luaL_checkstring(L, -1);
            lua_pop(L, 1);

            lua_getfield(L, 1, "url");
            if (!lua_isnil(L, -1))
                url = luaL_checkstring(L, -1);
            else
            {
                lua_pushstring(L, "Missing 'url' field");
                return lua_error(L);
            }
            lua_pop(L, 1);

            lua_getfield(L, 1, "body");
            if (!lua_isnil(L, -1))
                body = luaL_checkstring(L, -1);
            lua_pop(L, 1);

            // headers: table<string, string>
            lua_getfield(L, 1, "headers");
            if (lua_istable(L, -1))
            {
                lua_pushnil(L); // Start iteration
                while (lua_next(L, -2))
                {
                    String key = luaL_checkstring(L, -2);
                    String value = luaL_checkstring(L, -1);
                    headers[key] = value;
                    lua_pop(L, 1); // Pop value, keep key
                }
            }
            lua_pop(L, 1);

            // --- HTTP request ---
            WiFiClientSecure client;
            client.setInsecure(); // Warnung: Nur für Tests!

            HTTPClient http;
            http.begin(client, url);

            for (auto &pair : headers)
            {
                http.addHeader(pair.first, pair.second);
            }

            int code = -1;
            String response;

            if (strcmp(method, "GET") == 0)
            {
                code = http.GET();
            }
            else if (strcmp(method, "POST") == 0)
            {
                code = http.POST(body);
            }
            else if (strcmp(method, "PUT") == 0)
            {
                code = http.PUT(body);
            }
            else if (strcmp(method, "DELETE") == 0)
            {
                code = http.sendRequest("DELETE", body);
            }
            else
            {
                http.end();
                lua_pushstring(L, "Unsupported HTTP method");
                return lua_error(L);
            }

            if (code > 0)
            {
                response = http.getString();
            }
            else
            {
                response = String("Request failed: ") + http.errorToString(code);
            }

            // Build Lua response table
            lua_newtable(L);

            lua_pushinteger(L, code);
            lua_setfield(L, -2, "status");

            lua_pushstring(L, response.c_str());
            lua_setfield(L, -2, "body");

            http.end();
            return 1;
        }

        int connectToWofi(lua_State *L)
        {
            const char *ssid = luaL_checkstring(L, 1);
            const char *password = luaL_checkstring(L, 2);
            unsigned long timeoutMs = 15000; // optional: Timeout festlegen

            Serial.printf("Verbinde mit WLAN: %s\n", ssid);
            WiFi.begin(ssid, password);

            unsigned long start = millis();
            while (WiFi.status() != WL_CONNECTED)
            {
                if (millis() - start > timeoutMs)
                {
                    Serial.println("WLAN-Verbindung fehlgeschlagen: Timeout");
                    lua_pushboolean(L, false);
                    return 1; // Rückgabe false (Verbindung fehlgeschlagen)
                }
                delay(500);
                Serial.print(".");
            }

            Serial.println("");
            Serial.print("WLAN verbunden, IP: ");
            Serial.println(WiFi.localIP());

            lua_pushboolean(L, true); // Rückgabe true (erfolgreich verbunden)
            return 1;
        }

        void register_default_functions(lua_State *L)
        {
            lua_register(L, "print", luaPrintSerial);
            lua_register(L, "setLED", setLED);
            lua_register(L, "delay", luaDelay);
            lua_register(L, "httpReq", luaHttpRequest);
            lua_register(L, "ConnectToWifi", connectToWofi);
        }

    } // namespace LuaFunctions

    // -------- LuaSandbox --------
    namespace Sandbox
    {

        lua_State *createRestrictedLuaState()
        {
            lua_State *L = luaL_newstate();
            luaL_openlibs(L);

            // Entferne gefährliche libs
            lua_pushnil(L);
            lua_setglobal(L, "io");
            lua_pushnil(L);
            lua_setglobal(L, "os");
            lua_pushnil(L);
            lua_setglobal(L, "package");

            // Registriere sichere Funktionen
            LuaFunctions::register_default_functions(L);

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
            lua_pushstring(L, (String("exit with code: ") + code).c_str());
            return lua_error(L);
        }

        int runApp(const char *path, const std::vector<String> &args)
        {
            lua_State *L = LuaApps::Sandbox::createRestrictedLuaState();
            lua_register(L, "exitApp", lua_exitApp);

            // Push a new Lua table onto the stack
            lua_newtable(L); // this will be the 'args' table

            // Fill the table with arguments
            for (size_t i = 0; i < args.size(); ++i)
            {
                lua_pushnumber(L, i + 1); // Lua arrays are 1-based
                lua_pushstring(L, args[i].c_str());
                lua_settable(L, -3);
            }

            // Set the table as a global variable called "args"
            lua_setglobal(L, "args");

            // Load Lua file content
            File file = SPIFFS.open(path);
            if (!file)
                return -1;
            String content = file.readString();
            file.close();

            // Run the Lua script
            if (luaL_dostring(L, content.c_str()) != LUA_OK)
            {
                Serial.printf("Lua Error: %s\n", lua_tostring(L, -1));
                lua_pop(L, 1); // remove error message
                lua_close(L);
                if (lastExitCode == 0)
                    lastExitCode = -1; // default error code
                return lastExitCode;
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
