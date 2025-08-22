#include "winlib.hpp"
#include "apps/windows.hpp"
#include "screen/index.hpp"

#include <unordered_map>
#include <memory>

namespace LuaApps::WinLib
{
    static std::unordered_map<int, Windows::WindowPtr> windows;
    static std::unordered_map<int, Window *> rawWindows;
    static int nextWindowId = 1;

    // --- clipping helper (window-local coords) ---
    static inline bool inWindow(Window *w, int x, int y)
    {
        return (x >= 0 && y >= 0 && x < w->size.x && y < w->size.y);
    }

    static inline void clipRect(Window *w, int &x, int &y, int &wgt, int &hgt)
    {
        int x2 = x + wgt;
        int y2 = y + hgt;

        if (x < 0)
            x = 0;
        if (y < 0)
            y = 0;
        if (x2 > w->size.x)
            x2 = w->size.x;
        if (y2 > w->size.y)
            y2 = w->size.y;

        wgt = x2 - x;
        hgt = y2 - y;
        if (wgt < 0)
            wgt = 0;
        if (hgt < 0)
            hgt = 0;
    }

    int lua_createWindow(lua_State *L)
    {
        int x = luaL_checkinteger(L, 1);
        int y = luaL_checkinteger(L, 2);
        int w = luaL_checkinteger(L, 3);
        int h = luaL_checkinteger(L, 4);

        auto win = Windows::WindowPtr(new Window());
        win->init("Win App", {x, y}, {w, h});

        int id = nextWindowId++;
        Windows::WindowPtr localWin = std::move(win);
        Window *raw = localWin.get();

        windows[id] = std::move(localWin);
        Windows::add(std::move(windows[id]));
        rawWindows[id] = Windows::apps.back().get();

        lua_pushinteger(L, id);
        return 1;
    }

    static Window *getWindow(lua_State *L, int index)
    {
        int id = luaL_checkinteger(L, index);
        auto it = rawWindows.find(id);
        if (it == rawWindows.end() || it->second == nullptr)
            luaL_error(L, "Invalid window id %d", id);
        return it->second;
    }

    int lua_WIN_setName(lua_State *L)
    {
        Window *w = getWindow(L, 1);
        const char *name = luaL_checkstring(L, 2);
        w->name = String(name);
        return 0;
    }

    int lua_WIN_getRect(lua_State *L)
    {
        Window *w = getWindow(L, 1);
        lua_pushinteger(L, w->off.x);
        lua_pushinteger(L, w->off.y);
        lua_pushinteger(L, w->size.x);
        lua_pushinteger(L, w->size.y);
        return 4;
    }

    int lua_WIN_getLastEvent(lua_State *L)
    {
        Window *w = getWindow(L, 1);
        int screenId = luaL_checkinteger(L, 2);
        const MouseEvent &ev = w->lastEvent;

        lua_pushboolean(L, ev.state != MouseState::Up);
        lua_pushinteger(L, (int)ev.state);
        lua_pushinteger(L, ev.pos.x);
        lua_pushinteger(L, ev.pos.y);
        lua_pushinteger(L, ev.move.x);
        lua_pushinteger(L, ev.move.y);

        return 6;
    }

    int lua_WIN_closed(lua_State *L)
    {
        Window *w = getWindow(L, 1);
        lua_pushboolean(L, w->closed);
        return 1;
    }

    int lua_WIN_fillBg(lua_State *L)
    {
        Window *w = getWindow(L, 1);
        int color = luaL_checkinteger(L, 3);
        if (!Windows::canAccess)
            return 0;
        Windows::canAccess = false;

        Screen::tft.fillRect(w->off.x, w->off.y, w->size.x, w->size.y, color);

        Windows::canAccess = true;
        return 0;
    }

    int lua_WIN_writeText(lua_State *L)
    {
        Window *w = getWindow(L, 1);
        int x = luaL_checkinteger(L, 3);
        int y = luaL_checkinteger(L, 4);
        const char *text = luaL_checkstring(L, 5);
        int fontSize = luaL_checkinteger(L, 6);
        int color = luaL_checkinteger(L, 7);

        if (!Windows::canAccess || !inWindow(w, x, y))
            return 0;
        Windows::canAccess = false;

        Screen::tft.setTextColor(color);
        Screen::tft.setTextSize(fontSize);
        Screen::tft.drawString(String(text), w->off.x + x, w->off.y + y);

        Windows::canAccess = true;
        return 0;
    }

    int lua_WIN_writeRect(lua_State *L)
    {
        Window *w = getWindow(L, 1);
        int x = luaL_checkinteger(L, 3);
        int y = luaL_checkinteger(L, 4);
        int wdt = luaL_checkinteger(L, 5);
        int hgt = luaL_checkinteger(L, 6);
        int color = luaL_checkinteger(L, 7);

        if (!Windows::canAccess)
            return 0;
        Windows::canAccess = false;

        clipRect(w, x, y, wdt, hgt);
        if (wdt > 0 && hgt > 0)
            Screen::tft.fillRect(w->off.x + x, w->off.y + y, wdt, hgt, color);

        Windows::canAccess = true;
        return 0;
    }

    int lua_WIN_setIcon(lua_State *L)
    {
        Window *w = getWindow(L, 1);
        luaL_checktype(L, 2, LUA_TTABLE);

        constexpr size_t iconSize = sizeof(w->icon) / sizeof(w->icon[0]);
        for (size_t i = 0; i < iconSize; ++i)
        {
            lua_rawgeti(L, 2, i + 1);
            int val = luaL_checkinteger(L, -1);
            if (val < 0 || val > 0xFFFF)
                luaL_error(L, "Icon pixel out of range at index %zu", i + 1);
            w->icon[i] = static_cast<uint16_t>(val);
            lua_pop(L, 1);
        }
        return 0;
    }

    int lua_WIN_drawPixel(lua_State *L)
    {
        Window *w = getWindow(L, 1);
        int x = luaL_checkinteger(L, 3);
        int y = luaL_checkinteger(L, 4);
        int color = luaL_checkinteger(L, 5);

        if (Windows::canAccess && inWindow(w, x, y))
        {
            Windows::canAccess = false;
            Screen::tft.drawPixel(w->off.x + x, w->off.y + y, color);
            Windows::canAccess = true;
        }
        return 0;
    }

    int lua_WIN_drawImage(lua_State *L)
    {
        Window *w = getWindow(L, 1);
        int x = luaL_checkinteger(L, 3);
        int y = luaL_checkinteger(L, 4);
        int width = luaL_checkinteger(L, 5);
        int height = luaL_checkinteger(L, 6);
        luaL_checktype(L, 7, LUA_TTABLE);

        size_t pixelCount = width * height;
        std::unique_ptr<uint16_t[]> buffer(new uint16_t[pixelCount]);

        for (size_t i = 0; i < pixelCount; ++i)
        {
            lua_rawgeti(L, 7, i + 1);
            int val = luaL_checkinteger(L, -1);
            if (val < 0 || val > 0xFFFF)
                luaL_error(L, "Pixel out of range at index %zu", i + 1);
            buffer[i] = static_cast<uint16_t>(val);
            lua_pop(L, 1);
        }

        if (!Windows::canAccess)
            return 0;
        Windows::canAccess = false;

        int wdt = width, hgt = height;
        int offsetX = 0, offsetY = 0;
        if (x < 0)
        {
            offsetX = -x;
            x = 0;
        }
        if (y < 0)
        {
            offsetY = -y;
            y = 0;
        }
        clipRect(w, x, y, wdt, hgt);

        if (wdt > 0 && hgt > 0)
        {
            uint16_t *cropped = buffer.get() + offsetY * width + offsetX;
            Screen::tft.pushImage(w->off.x + x, w->off.y + y, wdt, hgt, cropped);
        }

        Windows::canAccess = true;
        return 0;
    }

    int lua_WIN_isRendered(lua_State *L)
    {
        lua_pushboolean(L, Windows::isRendering);
        return 1;
    }

    int lua_WIN_canAccess(lua_State *L)
    {
        lua_pushboolean(L, Windows::canAccess);
        return 1;
    }

    void register_win_functions(lua_State *L, const String &path)
    {
        lua_register(L, "createWindow", lua_createWindow);
        lua_register(L, "WIN_setName", lua_WIN_setName);
        lua_register(L, "WIN_getRect", lua_WIN_getRect);
        lua_register(L, "WIN_getLastEvent", lua_WIN_getLastEvent);
        lua_register(L, "WIN_closed", lua_WIN_closed);
        lua_register(L, "WIN_fillBg", lua_WIN_fillBg);
        lua_register(L, "WIN_writeText", lua_WIN_writeText);
        lua_register(L, "WIN_writeRect", lua_WIN_writeRect);
        lua_register(L, "WIN_setIcon", lua_WIN_setIcon);
        lua_register(L, "WIN_drawImage", lua_WIN_drawImage);
        lua_register(L, "WIN_drawPixel", lua_WIN_drawPixel);
        lua_register(L, "WIN_isRendering", lua_WIN_isRendered);
        lua_register(L, "WIN_canAccess", lua_WIN_canAccess);
    }

} // namespace LuaApps::WinLib
