#include "winlib.hpp"
#include "apps/windows.hpp"
#include "screen/index.hpp"

#include <vector>
#include <memory>

namespace LuaApps::WinLib
{
    static std::vector<Windows::WindowPtr> windows;
    static std::vector<Window *> rawWindows;

    int lua_createWindow(lua_State *L)
    {
        int x = luaL_checkinteger(L, 1);
        int y = luaL_checkinteger(L, 2);
        int w = luaL_checkinteger(L, 3);
        int h = luaL_checkinteger(L, 4);

        // std::make_unique evtl. nicht verfügbar -> manuell new + unique_ptr
        auto win = Windows::WindowPtr(new Window());
        win->init("LuaWindow", {x, y}, {w, h});

        windows.push_back(std::move(win));
        int id = (int)(windows.size() - 1);

        // Fenster an Windows::add übergeben (move), windows[id] ist danach nullptr
        Windows::add(std::move(windows[id]));

        // rohen Zeiger speichern, damit wir später wieder Zugriff auf das Fenster haben
        if (rawWindows.size() <= (size_t)id)
            rawWindows.resize(id + 1);
        rawWindows[id] = Windows::apps.back().get();

        // id zurückgeben
        lua_pushinteger(L, id);
        return 1;
    }

    static Window *getWindow(lua_State *L, int index)
    {
        int id = luaL_checkinteger(L, index);
        if (id < 0 || id >= (int)rawWindows.size() || rawWindows[id] == nullptr)
        {
            luaL_error(L, "Invalid window id %d", id);
            return nullptr;
        }
        return rawWindows[id];
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
