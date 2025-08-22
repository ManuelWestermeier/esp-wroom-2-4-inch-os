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

    // Hilfsfunktion: liefert das Fenster-Rechteck (Hauptfenster oder rechter Bereich)
    static Rect getScreenRect(Window *w, int screenId)
    {
        if (screenId == 2)
        {
            return Rect{
                Vec{w->off.x + w->size.x, w->off.y},
                Vec{w->resizeBoxSize, w->size.y - w->resizeBoxSize}};
        }
        else
        {
            return Rect{w->off, w->size};
        }
    }

    // Clipping-Helfer: Rechtecke
    static bool clipRect(const Rect &bounds, Rect &r)
    {
        Rect inter = r.intersection(bounds);
        if (inter.dimensions.x <= 0 || inter.dimensions.y <= 0)
            return false;
        r = inter;
        return true;
    }

    // Clipping-Helfer: Punkte
    static bool clipPoint(const Rect &bounds, Vec &p)
    {
        return bounds.isIn(p);
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

        lua_gc(L, LUA_GCCOLLECT, 0);

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

    int lua_WIN_close(lua_State *L)
    {
        auto w = getWindow(L, 1);
        if (!w)
            return 0;

        Windows::remove(w);
        return 0;
    }

    int lua_WIN_fillBg(lua_State *L)
    {
        Window *w = getWindow(L, 1);
        int screenId = luaL_checkinteger(L, 2);
        int color = luaL_checkinteger(L, 3);
        if (!w)
            return 0;

        Rect rect = getScreenRect(w, screenId);
        Screen::tft.fillRect(rect.pos.x, rect.pos.y, rect.dimensions.x, rect.dimensions.y, color);

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

        Rect bounds = getScreenRect(w, screenId);
        Vec pos = {w->off.x + x, w->off.y + y};
        if (!clipPoint(bounds, pos))
            return 0;

        Screen::tft.setTextSize(fontSize);
        Screen::tft.setTextColor(color, TFT_BLACK);
        Screen::tft.setCursor(pos.x, pos.y);

        int maxWidth = bounds.pos.x + bounds.dimensions.x - pos.x;
        String clipped = String(text);
        while (Screen::tft.textWidth(clipped) > maxWidth && clipped.length() > 0)
        {
            clipped.remove(clipped.length() - 1);
        }

        if (clipped.length() > 0)
            Screen::tft.print(clipped);

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

        Rect bounds = getScreenRect(w, screenId);
        Rect rect{Vec{w->off.x + x, w->off.y + y}, Vec{wdt, hgt}};
        if (!clipRect(bounds, rect))
            return 0;

        Screen::tft.fillRect(rect.pos.x, rect.pos.y, rect.dimensions.x, rect.dimensions.y, color);

        return 0;
    }

    int lua_WIN_setIcon(lua_State *L)
    {
        Window *w = getWindow(L, 1);
        if (!w)
            return 0;

        luaL_checktype(L, 2, LUA_TTABLE);

        constexpr size_t iconSize = sizeof(w->icon) / sizeof(w->icon[0]);

        for (size_t i = 0; i < iconSize; ++i)
        {
            lua_rawgeti(L, 2, i + 1);
            if (!lua_isinteger(L, -1))
            {
                luaL_error(L, "Expected integer at index %zu in icon array", i + 1);
                return 0;
            }
            int val = lua_tointeger(L, -1);
            if (val < 0 || val > 0xFFFF)
            {
                luaL_error(L, "Icon pixel value out of range at index %zu", i + 1);
                return 0;
            }
            w->icon[i] = static_cast<uint16_t>(val);
            lua_pop(L, 1);
        }

        return 0;
    }

    int lua_WIN_drawPixel(lua_State *L)
    {
        Window *w = getWindow(L, 1);
        int screenId = luaL_checkinteger(L, 2);
        int x = luaL_checkinteger(L, 3);
        int y = luaL_checkinteger(L, 4);
        int color = luaL_checkinteger(L, 5);

        Rect bounds = getScreenRect(w, screenId);
        Vec p{w->off.x + x, w->off.y + y};
        if (!clipPoint(bounds, p))
            return 0;

        Screen::tft.drawPixel(p.x, p.y, color);
        return 0;
    }

    int lua_WIN_drawImage(lua_State *L)
    {
        Window *w = getWindow(L, 1);
        int screenId = luaL_checkinteger(L, 2);
        int x = luaL_checkinteger(L, 3);
        int y = luaL_checkinteger(L, 4);
        int width = luaL_checkinteger(L, 5);
        int height = luaL_checkinteger(L, 6);

        luaL_checktype(L, 7, LUA_TTABLE);

        Rect bounds = getScreenRect(w, screenId);
        Rect rect{Vec{w->off.x + x, w->off.y + y}, Vec{width, height}};
        if (!clipRect(bounds, rect))
            return 0;

        size_t pixelCount = width * height;
        std::unique_ptr<uint16_t[]> buffer(new uint16_t[pixelCount]);

        for (size_t i = 0; i < pixelCount; ++i)
        {
            lua_rawgeti(L, 7, i + 1);
            int val = lua_tointeger(L, -1);
            buffer[i] = static_cast<uint16_t>(val);
            lua_pop(L, 1);
        }

        Screen::tft.pushImage(rect.pos.x, rect.pos.y,
                              rect.dimensions.x, rect.dimensions.y,
                              buffer.get());

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
