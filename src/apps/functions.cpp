#include "functions.hpp"

namespace LuaApps::LuaFunctions
{

    int luaPrintSerial(lua_State *L)
    {
        // get hidden script path
        String scriptPath = LuaApps::getApp(L)->path;
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
        String scriptPath = LuaApps::getApp(L)->path;

        int mode = luaL_checkinteger(L, 1);
        String code;
        String err;

        switch (mode)
        {
        case 1:
        { // raw string
            const char *strCode = luaL_checkstring(L, 2);
            code = strCode;
            break;
        }
        case 2:
        { // libraryId
            String libId luaL_checkstring(L, 2);
            ENC_FS::Path path = {"shared-libs", libId};
            if (!ENC_FS::exists(path))
            {
                err = "library not found: " + libId;
                lua_pushstring(L, err.c_str());
                return 1;
            }
            code = ENC_FS::readFileString(path);
            break;
        }
        case 3:
        { // fs path
            String filePath = scriptPath + "/" + luaL_checkstring(L, 2);
            Serial.println(filePath);
            if (!ENC_FS::exists(ENC_FS::str2Path(filePath)))
            {
                err = "file not found: " + String(filePath);
            }
            else
            {
                code = ENC_FS::readFileString(ENC_FS::str2Path(filePath));
            }
            break;
        }
        case 4:
        { // fetchUrl (GitHub shorthand)
            const char *urlPath = luaL_checkstring(L, 2);
            if (WiFi.isConnected() && UserWiFi::hasInternet)
            {
                String url = "https://raw.githubusercontent.com/" + String(urlPath);
                url.replace(" ", "");  // sicherstellen kein whitespace
                url.replace("\n", ""); // sicherstellen kein whitespace
                url.replace("\r", ""); // sicherstellen kein whitespace
                url.replace("\t", ""); // sicherstellen kein whitespace
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
        default:
            err = "invalid mode for luaExec() use 2 libary-id, 4 https://raw.githubusercontent.com/+x, 3 programm relative path, 1 raw string execute;";
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

    int lua_getTheme(lua_State *L)
    {
        lua_newtable(L); // create a new table

        lua_pushinteger(L, Style::Colors::bg);
        lua_setfield(L, -2, "bg");

        lua_pushinteger(L, Style::Colors::primary);
        lua_setfield(L, -2, "primary");

        lua_pushinteger(L, Style::Colors::text);
        lua_setfield(L, -2, "text");

        lua_pushinteger(L, Style::Colors::placeholder);
        lua_setfield(L, -2, "placeholder");

        lua_pushinteger(L, Style::Colors::accent);
        lua_setfield(L, -2, "accent");

        lua_pushinteger(L, Style::Colors::accent2);
        lua_setfield(L, -2, "accent2");

        lua_pushinteger(L, Style::Colors::accent3);
        lua_setfield(L, -2, "accent3");

        lua_pushinteger(L, Style::Colors::accentText);
        lua_setfield(L, -2, "accentText");

        lua_pushinteger(L, Style::Colors::pressed);
        lua_setfield(L, -2, "pressed");

        lua_pushinteger(L, Style::Colors::danger);
        lua_setfield(L, -2, "danger");

        return 1; // return the table
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
        lua_register(L, "exec", luaExec);
        lua_register(L, "setLED", setLED);
        lua_register(L, "delay", luaDelay);
        lua_register(L, "httpReq", luaHttpRequest);
        lua_register(L, "httpsReq", luaHttpsRequest);
        lua_register(L, "RGB", lua_RGB);
        lua_register(L, "getTheme", lua_getTheme);

        // window functions — we can extend WinLib::register_win_functions
        LuaApps::WinLib::register_win_functions(L);
        LuaApps::FsLib::register_fs_functions(L);
    }
} // namespace LuaFunctions
