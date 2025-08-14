#include "functions.hpp"

namespace LuaApps::LuaFunctions
{

    int luaPrintSerial(lua_State *L)
    {
        int nargs = lua_gettop(L);
        for (int i = 1; i <= nargs; ++i)
        {
            const char *msg = luaL_tolstring(L, i, NULL);
            String indent = "";
            for (int tab = 1; tab < i; ++tab)
                indent += "\t";
            const char *start = msg;
            while (*start)
            {
                const char *newline = strchr(start, '\n');
                if (newline)
                {
                    Serial.print(indent);
                    Serial.write(start, newline - start);
                    Serial.println();
                    start = newline + 1;
                }
                else
                {
                    Serial.print(indent);
                    Serial.println(start);
                    break;
                }
            }
            lua_pop(L, 1);
        }
        return 0;
    }

    int setLED(lua_State *L)
    {
        int state = luaL_checkinteger(L, 1) == 1 ? HIGH : LOW;
        pinMode(2, OUTPUT);
        digitalWrite(2, state);
        return 0;
    }

    int lua_RGB(lua_State *L)
    {
        int r = luaL_checkinteger(L, 1);
        int g = luaL_checkinteger(L, 2);
        int b = luaL_checkinteger(L, 3);

        uint16_t res = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3));

        lua_pushinteger(L, res);
        return 1;
    }

    int luaDelay(lua_State *L)
    {
        int time = luaL_checkinteger(L, 1);
        // delayMicroseconds(time * 1000);
        vTaskDelay(time / portTICK_PERIOD_MS); // this *does* yield to the RTOS
        return 0;
    }

    int luaHttpRequest(lua_State *L)
    {
        if (WiFi.status() != WL_CONNECTED)
        {
            lua_pushstring(L, "Not connected to WiFi");
            return lua_error(L);
        }

        luaL_checktype(L, 1, LUA_TTABLE);
        const char *method = "GET";
        const char *url = nullptr;
        String body = "";
        std::map<String, String> headers;

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

        lua_getfield(L, 1, "headers");
        if (lua_istable(L, -1))
        {
            lua_pushnil(L);
            while (lua_next(L, -2))
            {
                headers[luaL_checkstring(L, -2)] = luaL_checkstring(L, -1);
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);

        WiFiClient client;
        HTTPClient http;
        if (!http.begin(client, url))
        {
            lua_pushstring(L, "Failed to begin HTTP connection");
            return lua_error(L);
        }

        for (auto &pair : headers)
            http.addHeader(pair.first, pair.second);

        int code = -1;
        String response;

        if (strcmp(method, "GET") == 0)
            code = http.GET();
        else if (strcmp(method, "POST") == 0)
            code = http.POST(body);
        else if (strcmp(method, "PUT") == 0)
            code = http.PUT(body);
        else if (strcmp(method, "DELETE") == 0)
            code = http.sendRequest("DELETE", body);
        else
        {
            http.end();
            lua_pushstring(L, "Unsupported HTTP method");
            return lua_error(L);
        }

        response = (code > 0) ? http.getString() : String("Request failed: ") + http.errorToString(code);
        lua_newtable(L);
        lua_pushinteger(L, code);
        lua_setfield(L, -2, "status");
        lua_pushstring(L, response.c_str());
        lua_setfield(L, -2, "body");

        http.end();
        return 1;
    }

    int luaHttpsRequest(lua_State *L)
    {
        if (WiFi.status() != WL_CONNECTED)
        {
            lua_pushstring(L, "Not connected to WiFi");
            return lua_error(L);
        }

        luaL_checktype(L, 1, LUA_TTABLE);
        const char *method = "GET";
        const char *url = nullptr;
        String body = "";
        std::map<String, String> headers;

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

        lua_getfield(L, 1, "headers");
        if (lua_istable(L, -1))
        {
            lua_pushnil(L);
            while (lua_next(L, -2))
            {
                headers[luaL_checkstring(L, -2)] = luaL_checkstring(L, -1);
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);

        WiFiClientSecure client;
        client.setInsecure();

        HTTPClient http;
        http.begin(client, url);

        for (auto &pair : headers)
            http.addHeader(pair.first, pair.second);

        int code = -1;
        String response;

        if (strcmp(method, "GET") == 0)
            code = http.GET();
        else if (strcmp(method, "POST") == 0)
            code = http.POST(body);
        else if (strcmp(method, "PUT") == 0)
            code = http.PUT(body);
        else if (strcmp(method, "DELETE") == 0)
            code = http.sendRequest("DELETE", body);
        else
        {
            http.end();
            lua_pushstring(L, "Unsupported HTTP method");
            return lua_error(L);
        }

        response = (code > 0) ? http.getString() : String("Request failed: ") + http.errorToString(code);
        lua_newtable(L);
        lua_pushinteger(L, code);
        lua_setfield(L, -2, "status");
        lua_pushstring(L, response.c_str());
        lua_setfield(L, -2, "body");

        http.end();
        return 1;
    }

    void register_default_functions(lua_State *L)
    {
        lua_register(L, "print", luaPrintSerial);
        lua_register(L, "setLED", setLED);
        lua_register(L, "delay", luaDelay);
        lua_register(L, "httpReq", luaHttpRequest);
        lua_register(L, "httpsReq", luaHttpsRequest);
        lua_register(L, "RGB", lua_RGB);
        LuaApps::WinLib::register_win_functions(L);
    }

} // namespace LuaApps::LuaFunctions
