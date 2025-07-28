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

    int lua_createWindow(lua_State *L)
    {
        int x = luaL_checkinteger(L, 1);
        int y = luaL_checkinteger(L, 2);
        int w = luaL_checkinteger(L, 3);
        int h = luaL_checkinteger(L, 4);

        auto win = Windows::WindowPtr(new Window());
        win->init("LuaWindow", {x, y}, {w, h});

        int id = nextWindowId++;

        // Temporär speichern, um danach Pointer zu extrahieren
        Windows::WindowPtr localWin = std::move(win);
        Window *raw = localWin.get();

        // In globale Maps eintragen
        windows[id] = std::move(localWin);
        Windows::add(std::move(windows[id])); // Übergabe an Windows::apps

        rawWindows[id] = Windows::apps.back().get(); // Zugriff auf endgültigen Pointer

        lua_pushinteger(L, id);
        return 1;
    }

    static Window *getWindow(lua_State *L, int index)
    {
        int id = luaL_checkinteger(L, index);
        auto it = rawWindows.find(id);
        if (it == rawWindows.end() || it->second == nullptr)
        {
            luaL_error(L, "Invalid window id %d", id);
            return nullptr;
        }
        return it->second;
    }

    int lua_WIN_setName(lua_State *L)
    {
        Window *w = getWindow(L, 1);
        if (!w)
            return 0;

        const char *name = luaL_checkstring(L, 2);
        w->name = String(name);
        return 0;
    }

    int lua_WIN_getRect(lua_State *L)
    {
        Window *w = getWindow(L, 1);
        if (!w)
            return 0;

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
        if (!w)
            return 0;

        const MouseEvent &ev = (screenId == 2) ? w->lastEventRightSprite : w->lastEvent;

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
        if (!w)
            return 0;

        lua_pushboolean(L, w->closed);
        return 1;
    }

    int lua_WIN_fillBg(lua_State *L)
    {
        Window *w = getWindow(L, 1);
        int screenId = luaL_checkinteger(L, 2);
        int color = luaL_checkinteger(L, 3);
        if (!w)
            return 0;

        if (screenId == 2)
            w->rightSprite.fillSprite(color);
        else
            w->sprite.fillSprite(color);

        return 0;
    }

    int lua_WIN_writeText(lua_State *L)
    {
        Window *w = getWindow(L, 1);
        int screenId = luaL_checkinteger(L, 2);
        int x = luaL_checkinteger(L, 3);
        int y = luaL_checkinteger(L, 4);
        const char *text = luaL_checkstring(L, 5);
        int fontSize = luaL_checkinteger(L, 6);
        int color = luaL_checkinteger(L, 7);

        TFT_eSprite &spr = (screenId == 2) ? w->rightSprite : w->sprite;
        spr.setTextColor(color);
        spr.setTextSize(fontSize);
        spr.drawString(String(text), x, y);

        return 0;
    }

    int lua_WIN_writeRect(lua_State *L)
    {
        Window *w = getWindow(L, 1);
        int screenId = luaL_checkinteger(L, 2);
        int x = luaL_checkinteger(L, 3);
        int y = luaL_checkinteger(L, 4);
        int wdt = luaL_checkinteger(L, 5);
        int hgt = luaL_checkinteger(L, 6);
        int color = luaL_checkinteger(L, 7);

        TFT_eSprite &spr = (screenId == 2) ? w->rightSprite : w->sprite;
        spr.fillRect(x, y, wdt, hgt, color);

        return 0;
    }

    void register_win_functions(lua_State *L)
    {
        lua_register(L, "createWindow", lua_createWindow);
        lua_register(L, "WIN_setName", lua_WIN_setName);
        lua_register(L, "WIN_getRect", lua_WIN_getRect);
        lua_register(L, "WIN_getLastEvent", lua_WIN_getLastEvent);
        lua_register(L, "WIN_closed", lua_WIN_closed);
        lua_register(L, "WIN_fillBg", lua_WIN_fillBg);
        lua_register(L, "WIN_writeText", lua_WIN_writeText);
        lua_register(L, "WIN_writeRect", lua_WIN_writeRect);
    }

} // namespace LuaApps::WinLib
