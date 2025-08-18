#include "functions.hpp"

namespace LuaApps::LuaFunctions
{

    int luaPrintSerial(lua_State *L)
    {
        // get hidden script path
        String scriptPath = String(lua_tostring(L, lua_upvalueindex(1)));
        Serial.println("PRINT: " + scriptPath);

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

    int luaExec(lua_State *L)
    {
        // get hidden script path
        String scriptPath = String(lua_tostring(L, lua_upvalueindex(1)));

        Serial.println("scriptPath: " + scriptPath);

        int mode = luaL_checkinteger(L, 1);
        String code;
        String err;

        switch (mode)
        {
        case 1:
        { // libraryId
            const char *libId = luaL_checkstring(L, 2);
            String path = "/" + String(Auth::username) + "/shared-libs/" + String(libId) + ".lua";
            if (!SD_FS::exists(path))
            {
                err = "library not found: " + path;
                lua_pushstring(L, err.c_str());
                return 1;
            }
            code = SD_FS::readFile(path);
            break;
        }
        case 2:
        { // fetchUrl (GitHub shorthand)
            const char *urlPath = luaL_checkstring(L, 2);
            if (WiFi.isConnected())
            {
                String url = "https://raw.githubusercontent.com/" + String(urlPath);
                url.replace(" ", ""); // sicherstellen kein whitespace
                HTTPClient http;
                if (http.begin(url))
                {
                    int httpCode = http.GET();
                    if (httpCode == HTTP_CODE_OK)
                    {
                        code = http.getString();
                    }
                    else
                    {
                        err = "http error: " + String(httpCode);
                    }
                    http.end();
                }
                else
                {
                    err = "http begin failed";
                }
            }
            else
            {
                err = "wifi not connected";
            }
            break;
        }
        case 3:
        { // fs path
            String filePath = scriptPath + luaL_checkstring(L, 2);
            if (!SD_FS::exists(filePath))
            {
                err = "file not found: " + String(filePath);
            }
            else
            {
                code = SD_FS::readFile(filePath);
            }
            break;
        }
        case 4:
        { // raw string
            const char *strCode = luaL_checkstring(L, 2);
            code = strCode;
            break;
        }
        default:
            err = "invalid mode for luaExec() use 1 libary-id, 2 https://raw.githubusercontent.com/+x, 3 programm relative path, 4 raw string execute;";
            break;
        }

        // wenn Fehler schon vorher
        if (err.length())
        {
            lua_pushstring(L, err.c_str());
            return 1;
        }

        // Lua Code ausführen
        int status = luaL_dostring(L, code.c_str());
        if (status != LUA_OK)
        {
            const char *luaErr = lua_tostring(L, -1);
            if (luaErr)
            {
                lua_pushstring(L, luaErr);
            }
            else
            {
                lua_pushstring(L, "unknown lua error");
            }
            return 1;
        }

        lua_pushstring(L, "ok");
        return 1;
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

    void register_default_functions(lua_State *L, const String &path)
    {
        // push helper macro for registering with path as upvalue
        auto register_with_path = [&](const char *name, lua_CFunction fn)
        {
            lua_pushstring(L, path.c_str()); // push path as upvalue
            lua_pushcclosure(L, fn, 1);      // bind with 1 upvalue
            lua_setglobal(L, name);          // assign global name
        };

        register_with_path("print", luaPrintSerial);
        register_with_path("exec", luaExec);
        register_with_path("setLED", setLED);
        register_with_path("delay", luaDelay);
        register_with_path("httpReq", luaHttpRequest);
        register_with_path("httpsReq", luaHttpsRequest);
        register_with_path("RGB", lua_RGB);

        // window functions — we can extend LuaApps::WinLib::register_win_functions
        LuaApps::WinLib::register_win_functions(L, path);
    }

} // namespace LuaApps::LuaFunctions
