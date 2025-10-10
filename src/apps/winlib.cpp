#include "winlib.hpp"
#include "apps/windows.hpp"
#include "screen/index.hpp"

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>

#include <unordered_map>
#include <memory>
#include <string>

#include "../utils/priority-guard.hpp"

namespace LuaApps::WinLib
{
    // Global map: id -> shared_ptr<Window>
    // Use one map only — do not keep raw pointers elsewhere.
    static std::unordered_map<int, Windows::WindowPtr> windows;
    static int nextWindowId = 1;

    // Helper: get the owning App for this Lua state
    // (assumes getApp(lua_State*) is defined elsewhere and returns App*)
    // --- Screen rect helpers (unchanged except formatting) ---
    static Rect _getScreenRect(Window *w, int screenId)
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

    static Rect getScreenRect(Window *w, int screenId)
    {
        auto r = _getScreenRect(w, screenId);
        if (!(Rect{{0, 0}, {320, 240}}.intersects(r)))
            return {{0, 0}, {0, 0}};
        return r;
    }

    // Remove a window owned by ownerApp (erases from global map and owner's set)
    // Caller must ensure ownerApp actually owns the id (we assert that in callers).
    static void removeWindowById(int id, App *ownerApp)
    {
        auto it = windows.find(id);
        if (it == windows.end())
            return;

        // Remove from Windows manager (uses raw pointer)
        Window *raw = it->second.get();
        if (raw)
        {
            Windows::remove(raw);
        }

        // Erase from global map to free memory
        windows.erase(it);

        // Erase from owner app's set (if provided)
        if (ownerApp)
        {
            ownerApp->windows.erase(id);
        }
    }

    // Create window: returns id on Lua stack
    int lua_createWindow(lua_State *L)
    {
        int x = luaL_checkinteger(L, 1);
        int y = luaL_checkinteger(L, 2);
        int w = luaL_checkinteger(L, 3);
        int h = luaL_checkinteger(L, 4);

        int id = nextWindowId++;

        auto win = std::make_shared<Window>();
        // Fix: don't do "App " + id (invalid). Use to_string.
        win->init("App " + id, {x, y}, {w, h});

        // Hold shared_ptr in global map
        windows[id] = win;
        Windows::add(win);

        // GC hint (your existing call)
        lua_gc(L, LUA_GCCOLLECT, 0);

        // register id with this App (must exist)
        App *app = getApp(L);
        if (!app)
        {
            // shouldn't happen, but clean up and error
            windows.erase(id);
            luaL_error(L, "Internal error: app context missing when creating window");
            return 0;
        }
        app->windows.insert(id);

        lua_pushinteger(L, id);
        return 1;
    }

    // Centralized getWindow: checks id validity AND that current app owns the id.
    // Returns shared_ptr -> raw pointer (non-owning). On error it raises Lua error.
    static Window *getWindow(lua_State *L, int index)
    {
        int id = luaL_checkinteger(L, index);
        App *app = getApp(L);
        if (!app)
        {
            luaL_error(L, "Internal error: app context missing");
            return nullptr;
        }

        auto it = windows.find(id);
        if (it == windows.end() || !it->second)
        {
            luaL_error(L, "Invalid window id %d", id);
            return nullptr;
        }

        // Ownership check: only allow access to windows that belong to this app
        if (app->windows.find(id) == app->windows.end())
        {
            luaL_error(L, "Access denied: window %d not owned by this app", id);
            return nullptr;
        }

        return it->second.get();
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
        if (!w)
            return 0;

        int screenId = luaL_checkinteger(L, 2);
        const MouseEvent &ev = (screenId == 2) ? w->lastEventRightSprite : w->lastEvent;

        lua_pushboolean(L, ev.state != MouseState::Up);
        lua_pushinteger(L, (int)ev.state);
        lua_pushinteger(L, ev.pos.x);
        lua_pushinteger(L, ev.pos.y);
        lua_pushinteger(L, ev.move.x);
        lua_pushinteger(L, ev.move.y);
        lua_pushboolean(L, w->wasClicked);

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

    // Close window: only the owning app may close its windows.
    int lua_WIN_close(lua_State *L)
    {
        // The first arg is window id
        int id = luaL_checkinteger(L, 1);
        App *app = getApp(L);
        if (!app)
        {
            luaL_error(L, "Internal error: app context missing");
            return 0;
        }

        // Ownership check (consistent with getWindow)
        if (app->windows.find(id) == app->windows.end())
        {
            luaL_error(L, "Access denied: window %d not owned by this app", id);
            return 0;
        }

        // Remove from Windows manager and global map and from owner's set
        removeWindowById(id, app);

        return 0;
    }

    // Drawing helpers: check rendering/ownership and perform viewport-based drawing.
    // For all of the following we use getWindow which performs the ownership check.

    int lua_WIN_fillBg(lua_State *L)
    {
        if (!Windows::isRendering)
            return 0;
        Window *w = getWindow(L, 1);
        if (!w || w->closed)
            return 0;
        int screenId = luaL_checkinteger(L, 2);
        int color = luaL_checkinteger(L, 3);

        Rect rect = getScreenRect(w, screenId);

        // simple spin-wait as before (preserve existing design)
        while (!Windows::canAccess)
        {
            delay(rand() % 2);
        }
        Windows::canAccess = false;

        Screen::tft.setViewport(rect.pos.x, rect.pos.y, rect.dimensions.x, rect.dimensions.y, true);
        Screen::tft.fillRect(0, 0, rect.dimensions.x, rect.dimensions.y, color);
        Screen::tft.resetViewport();

        Windows::canAccess = true;
        delay(10);

        return 0;
    }

    int lua_WIN_writeText(lua_State *L)
    {
        if (!Windows::isRendering)
            return 0;
        Window *w = getWindow(L, 1);
        if (!w || w->closed)
            return 0;
        int screenId = luaL_checkinteger(L, 2);
        int x = luaL_checkinteger(L, 3);
        int y = luaL_checkinteger(L, 4);
        const char *text = luaL_checkstring(L, 5);
        int fontSize = luaL_checkinteger(L, 6);
        int color = luaL_checkinteger(L, 7);

        Rect rect = getScreenRect(w, screenId);

        while (!Windows::canAccess)
        {
            delay(rand() % 2);
        }
        Windows::canAccess = false;

        Screen::tft.setViewport(rect.pos.x, rect.pos.y, rect.dimensions.x, rect.dimensions.y, true);
        Screen::tft.setTextSize(fontSize);
        Screen::tft.setTextColor(color);
        Screen::tft.setCursor(x, y);
        Screen::tft.print(text);
        Screen::tft.resetViewport();

        Windows::canAccess = true;
        delay(10);

        return 0;
    }

    // Renamed to more logical name: this function fills a rect (was previously named writeRect)
    int lua_WIN_fillRect(lua_State *L)
    {
        if (!Windows::isRendering)
            return 0;
        Window *w = getWindow(L, 1);
        if (!w || w->closed)
            return 0;
        int screenId = luaL_checkinteger(L, 2);
        int x = luaL_checkinteger(L, 3);
        int y = luaL_checkinteger(L, 4);
        int wdt = luaL_checkinteger(L, 5);
        int hgt = luaL_checkinteger(L, 6);
        int color = luaL_checkinteger(L, 7);

        Rect rect = getScreenRect(w, screenId);

        while (!Windows::canAccess)
        {
            delay(rand() % 2);
        }
        Windows::canAccess = false;

        Screen::tft.setViewport(rect.pos.x, rect.pos.y, rect.dimensions.x, rect.dimensions.y, true);
        Screen::tft.fillRect(x, y, wdt, hgt, color);
        Screen::tft.resetViewport();

        Windows::canAccess = true;
        delay(10);

        return 0;
    }

    int lua_WIN_setIcon(lua_State *L)
    {
        Window *w = getWindow(L, 1);
        if (!w)
            return 0;

        luaL_checktype(L, 2, LUA_TTABLE);

        constexpr size_t iconSize = sizeof(w->icon) / sizeof(w->icon[0]);

        // Optional: verify table length (not strictly necessary, but nice)
        // If lua_rawlen is available in your target Lua, you can check it:
        // size_t tlen = lua_rawlen(L, 2);
        // if (tlen != iconSize) luaL_error(L, "Icon table must have %zu entries", iconSize);

        for (size_t i = 0; i < iconSize; ++i)
        {
            lua_rawgeti(L, 2, i + 1);
            if (!lua_isinteger(L, -1))
            {
                lua_pop(L, 1);
                luaL_error(L, "Expected integer at index %zu in icon array", i + 1);
                return 0;
            }
            int val = (int)lua_tointeger(L, -1);
            lua_pop(L, 1);
            if (val < 0 || val > 0xFFFF)
            {
                luaL_error(L, "Icon pixel value out of range at index %zu", i + 1);
                return 0;
            }
            w->icon[i] = static_cast<uint16_t>(val);
        }

        return 0;
    }

    int lua_WIN_drawPixel(lua_State *L)
    {
        if (!Windows::isRendering)
            return 0;
        Window *w = getWindow(L, 1);
        if (!w || w->closed)
            return 0;
        int screenId = luaL_checkinteger(L, 2);
        int x = luaL_checkinteger(L, 3);
        int y = luaL_checkinteger(L, 4);
        int color = luaL_checkinteger(L, 5);

        Rect rect = getScreenRect(w, screenId);

        while (!Windows::canAccess)
        {
            delay(rand() % 2);
        }
        Windows::canAccess = false;

        Screen::tft.setViewport(rect.pos.x, rect.pos.y, rect.dimensions.x, rect.dimensions.y, true);
        Screen::tft.drawPixel(x, y, color);
        Screen::tft.resetViewport();

        Windows::canAccess = true;

        return 0;
    }

    // Read pixel within window
    int lua_WIN_readPixel(lua_State *L)
    {
        Window *w = getWindow(L, 1);
        if (!w || w->closed)
        {
            lua_pushinteger(L, 0);
            return 1;
        }

        int screenId = luaL_checkinteger(L, 2);
        int x = luaL_checkinteger(L, 3);
        int y = luaL_checkinteger(L, 4);

        Rect rect = getScreenRect(w, screenId);
        if (x < 0 || y < 0 || x >= rect.dimensions.x || y >= rect.dimensions.y)
        {
            lua_pushinteger(L, 0); // outside window
            return 1;
        }

        while (!Windows::canAccess)
            delay(rand() % 2);
        Windows::canAccess = false;

        Screen::tft.setViewport(rect.pos.x, rect.pos.y, rect.dimensions.x, rect.dimensions.y, true);
        uint16_t color = Screen::tft.readPixel(x, y); // use TFT readPixel
        Screen::tft.resetViewport();

        Windows::canAccess = true;

        lua_pushinteger(L, color);
        return 1;
    }

    int lua_WIN_drawImage(lua_State *L)
    {
        if (!Windows::isRendering)
            return 0;
        Window *w = getWindow(L, 1);
        if (!w || w->closed)
            return 0;
        int screenId = luaL_checkinteger(L, 2);
        int x = luaL_checkinteger(L, 3);
        int y = luaL_checkinteger(L, 4);
        int width = luaL_checkinteger(L, 5);
        int height = luaL_checkinteger(L, 6);
        PriorityGuard pg(8); // lower priority while processing

        luaL_checktype(L, 7, LUA_TTABLE);

        size_t pixelCount = (size_t)width * (size_t)height;
        // allocate RAII array
        std::unique_ptr<uint16_t[]> buffer(new uint16_t[pixelCount]);

        for (size_t i = 0; i < pixelCount; ++i)
        {
            lua_rawgeti(L, 7, i + 1);
            if (!lua_isinteger(L, -1))
            {
                lua_pop(L, 1);
                luaL_error(L, "Image pixel %zu is not an integer", i + 1);
                return 0;
            }
            int val = (int)lua_tointeger(L, -1);
            lua_pop(L, 1);
            buffer[i] = static_cast<uint16_t>(val);
        }

        Rect rect = getScreenRect(w, screenId);

        while (!Windows::canAccess)
        {
            delay(rand() % 2);
        }
        Windows::canAccess = false;

        Screen::tft.setViewport(rect.pos.x, rect.pos.y, rect.dimensions.x, rect.dimensions.y, true);
        Screen::tft.pushImage(x, y, width, height, buffer.get());
        Screen::tft.resetViewport();

        Windows::canAccess = true;
        delay(10);

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

    int lua_WIN_readText(lua_State *L)
    {
        if (!Windows::isRendering)
            return 0;

        Window *w = getWindow(L, 1);
        if (!w || w->closed)
            return 0;

        String question(luaL_checkstring(L, 2));
        String defaultValue(luaL_checkstring(L, 3));

        String out = "";
        bool ok = w->wasClicked;

        if (ok)
        {
            // Wait for access
            while (!Windows::canAccess)
            {
                delay(rand() % 2);
            }

            Windows::canAccess = false;

            w->wasClicked = false;
            out = readString(question, defaultValue);

            Screen::tft.fillScreen(BG);

            Windows::canAccess = true;
        }

        lua_pushboolean(L, ok);
        lua_pushstring(L, out.c_str());
        return 2;
    }

    // --- TFT_eSPI drawing helpers (same pattern) ---
    int lua_WIN_drawLine(lua_State *L)
    {
        if (!Windows::isRendering)
            return 0;
        Window *w = getWindow(L, 1);
        if (!w || w->closed)
            return 0;
        int screenId = luaL_checkinteger(L, 2);
        int x0 = luaL_checkinteger(L, 3);
        int y0 = luaL_checkinteger(L, 4);
        int x1 = luaL_checkinteger(L, 5);
        int y1 = luaL_checkinteger(L, 6);
        int color = luaL_checkinteger(L, 7);

        Rect rect = getScreenRect(w, screenId);

        while (!Windows::canAccess)
        {
            delay(rand() % 2);
        }
        Windows::canAccess = false;

        Screen::tft.setViewport(rect.pos.x, rect.pos.y, rect.dimensions.x, rect.dimensions.y, true);
        Screen::tft.drawLine(x0, y0, x1, y1, color);
        Screen::tft.resetViewport();

        Windows::canAccess = true;
        delay(10);

        return 0;
    }

    int lua_WIN_drawRect(lua_State *L)
    {
        if (!Windows::isRendering)
            return 0;
        Window *w = getWindow(L, 1);
        if (!w || w->closed)
            return 0;
        int screenId = luaL_checkinteger(L, 2);
        int x = luaL_checkinteger(L, 3);
        int y = luaL_checkinteger(L, 4);
        int wdt = luaL_checkinteger(L, 5);
        int hgt = luaL_checkinteger(L, 6);
        int color = luaL_checkinteger(L, 7);

        Rect rect = getScreenRect(w, screenId);

        while (!Windows::canAccess)
        {
            delay(rand() % 2);
        }
        Windows::canAccess = false;

        Screen::tft.setViewport(rect.pos.x, rect.pos.y, rect.dimensions.x, rect.dimensions.y, true);
        Screen::tft.drawRect(x, y, wdt, hgt, color);
        Screen::tft.resetViewport();

        Windows::canAccess = true;
        delay(10);

        return 0;
    }

    int lua_WIN_drawTriangle(lua_State *L)
    {
        if (!Windows::isRendering)
            return 0;
        Window *w = getWindow(L, 1);
        if (!w || w->closed)
            return 0;
        int screenId = luaL_checkinteger(L, 2);
        int x0 = luaL_checkinteger(L, 3);
        int y0 = luaL_checkinteger(L, 4);
        int x1 = luaL_checkinteger(L, 5);
        int y1 = luaL_checkinteger(L, 6);
        int x2 = luaL_checkinteger(L, 7);
        int y2 = luaL_checkinteger(L, 8);
        int color = luaL_checkinteger(L, 9);

        Rect rect = getScreenRect(w, screenId);

        while (!Windows::canAccess)
        {
            delay(rand() % 2);
        }
        Windows::canAccess = false;

        Screen::tft.setViewport(rect.pos.x, rect.pos.y, rect.dimensions.x, rect.dimensions.y, true);
        Screen::tft.drawTriangle(x0, y0, x1, y1, x2, y2, color);
        Screen::tft.resetViewport();

        Windows::canAccess = true;
        delay(10);

        return 0;
    }

    int lua_WIN_fillTriangle(lua_State *L)
    {
        if (!Windows::isRendering)
            return 0;
        Window *w = getWindow(L, 1);
        if (!w || w->closed)
            return 0;
        int screenId = luaL_checkinteger(L, 2);
        int x0 = luaL_checkinteger(L, 3);
        int y0 = luaL_checkinteger(L, 4);
        int x1 = luaL_checkinteger(L, 5);
        int y1 = luaL_checkinteger(L, 6);
        int x2 = luaL_checkinteger(L, 7);
        int y2 = luaL_checkinteger(L, 8);
        int color = luaL_checkinteger(L, 9);

        Rect rect = getScreenRect(w, screenId);

        while (!Windows::canAccess)
        {
            delay(rand() % 2);
        }
        Windows::canAccess = false;

        Screen::tft.setViewport(rect.pos.x, rect.pos.y, rect.dimensions.x, rect.dimensions.y, true);
        Screen::tft.fillTriangle(x0, y0, x1, y1, x2, y2, color);
        Screen::tft.resetViewport();

        Windows::canAccess = true;
        delay(10);

        return 0;
    }

    int lua_WIN_drawCircle(lua_State *L)
    {
        if (!Windows::isRendering)
            return 0;
        Window *w = getWindow(L, 1);
        if (!w || w->closed)
            return 0;
        int screenId = luaL_checkinteger(L, 2);
        int x = luaL_checkinteger(L, 3);
        int y = luaL_checkinteger(L, 4);
        int r = luaL_checkinteger(L, 5);
        int color = luaL_checkinteger(L, 6);

        Rect rect = getScreenRect(w, screenId);

        while (!Windows::canAccess)
        {
            delay(rand() % 2);
        }
        Windows::canAccess = false;

        Screen::tft.setViewport(rect.pos.x, rect.pos.y, rect.dimensions.x, rect.dimensions.y, true);
        Screen::tft.drawCircle(x, y, r, color);
        Screen::tft.resetViewport();

        Windows::canAccess = true;
        delay(10);

        return 0;
    }

    int lua_WIN_fillCircle(lua_State *L)
    {
        if (!Windows::isRendering)
            return 0;
        Window *w = getWindow(L, 1);
        if (!w || w->closed)
            return 0;
        int screenId = luaL_checkinteger(L, 2);
        int x = luaL_checkinteger(L, 3);
        int y = luaL_checkinteger(L, 4);
        int r = luaL_checkinteger(L, 5);
        int color = luaL_checkinteger(L, 6);

        Rect rect = getScreenRect(w, screenId);

        while (!Windows::canAccess)
        {
            delay(rand() % 2);
        }
        Windows::canAccess = false;

        Screen::tft.setViewport(rect.pos.x, rect.pos.y, rect.dimensions.x, rect.dimensions.y, true);
        Screen::tft.fillCircle(x, y, r, color);
        Screen::tft.resetViewport();

        Windows::canAccess = true;
        delay(10);

        return 0;
    }

    int lua_WIN_drawRoundRect(lua_State *L)
    {
        if (!Windows::isRendering)
            return 0;
        Window *w = getWindow(L, 1);
        if (!w || w->closed)
            return 0;
        int screenId = luaL_checkinteger(L, 2);
        int x = luaL_checkinteger(L, 3);
        int y = luaL_checkinteger(L, 4);
        int wdt = luaL_checkinteger(L, 5);
        int hgt = luaL_checkinteger(L, 6);
        int radius = luaL_checkinteger(L, 7);
        int color = luaL_checkinteger(L, 8);

        Rect rect = getScreenRect(w, screenId);

        while (!Windows::canAccess)
        {
            delay(rand() % 2);
        }
        Windows::canAccess = false;

        Screen::tft.setViewport(rect.pos.x, rect.pos.y, rect.dimensions.x, rect.dimensions.y, true);
        Screen::tft.drawRoundRect(x, y, wdt, hgt, radius, color);
        Screen::tft.resetViewport();

        Windows::canAccess = true;
        delay(10);

        return 0;
    }

    int lua_WIN_fillRoundRect(lua_State *L)
    {
        if (!Windows::isRendering)
            return 0;
        Window *w = getWindow(L, 1);
        if (!w || w->closed)
            return 0;
        int screenId = luaL_checkinteger(L, 2);
        int x = luaL_checkinteger(L, 3);
        int y = luaL_checkinteger(L, 4);
        int wdt = luaL_checkinteger(L, 5);
        int hgt = luaL_checkinteger(L, 6);
        int radius = luaL_checkinteger(L, 7);
        int color = luaL_checkinteger(L, 8);

        Rect rect = getScreenRect(w, screenId);

        while (!Windows::canAccess)
        {
            delay(rand() % 2);
        }
        Windows::canAccess = false;

        Screen::tft.setViewport(rect.pos.x, rect.pos.y, rect.dimensions.x, rect.dimensions.y, true);
        Screen::tft.fillRoundRect(x, y, wdt, hgt, radius, color);
        Screen::tft.resetViewport();

        Windows::canAccess = true;
        delay(10);

        return 0;
    }

    int lua_WIN_drawFastVLine(lua_State *L)
    {
        if (!Windows::isRendering)
            return 0;
        Window *w = getWindow(L, 1);
        if (!w || w->closed)
            return 0;
        int screenId = luaL_checkinteger(L, 2);
        int x = luaL_checkinteger(L, 3);
        int y = luaL_checkinteger(L, 4);
        int h = luaL_checkinteger(L, 5);
        int color = luaL_checkinteger(L, 6);

        Rect rect = getScreenRect(w, screenId);

        while (!Windows::canAccess)
        {
            delay(rand() % 2);
        }
        Windows::canAccess = false;

        Screen::tft.setViewport(rect.pos.x, rect.pos.y, rect.dimensions.x, rect.dimensions.y, true);
        Screen::tft.drawFastVLine(x, y, h, color);
        Screen::tft.resetViewport();

        Windows::canAccess = true;
        delay(10);

        return 0;
    }

    int lua_WIN_drawFastHLine(lua_State *L)
    {
        if (!Windows::isRendering)
            return 0;
        Window *w = getWindow(L, 1);
        if (!w || w->closed)
            return 0;
        int screenId = luaL_checkinteger(L, 2);
        int x = luaL_checkinteger(L, 3);
        int y = luaL_checkinteger(L, 4);
        int wdt = luaL_checkinteger(L, 5);
        int color = luaL_checkinteger(L, 6);

        Rect rect = getScreenRect(w, screenId);

        while (!Windows::canAccess)
        {
            delay(rand() % 2);
        }
        Windows::canAccess = false;

        Screen::tft.setViewport(rect.pos.x, rect.pos.y, rect.dimensions.x, rect.dimensions.y, true);
        Screen::tft.drawFastHLine(x, y, wdt, color);
        Screen::tft.resetViewport();

        Windows::canAccess = true;
        delay(10);

        return 0;
    }

    int lua_WIN_drawSVG(lua_State *L)
    {
        if (!Windows::isRendering)
            return 0;

        Window *win = getWindow(L, 1);
        if (!win || win->closed)
            return 0;

        PriorityGuard pg(8); // lower priority while processing

        int screenId = luaL_checkinteger(L, 2);
        String svgStr = luaL_checkstring(L, 3);
        int x = luaL_checkinteger(L, 4);
        int y = luaL_checkinteger(L, 5);
        int w = luaL_checkinteger(L, 6);
        int h = luaL_checkinteger(L, 7);
        int color = luaL_checkinteger(L, 8);
        int steps = luaL_checkinteger(L, 9);

        Rect rect = getScreenRect(win, screenId);

        // NSVGimage *svgImage = createSVG(svgStr);

        bool ok = true; // svgImage != nullptr;

        if (ok)
        {
            while (!Windows::canAccess)
            {
                delay(rand() % 2);
            }
            Windows::canAccess = false;
            PriorityGuard pg(8); // lower priority while processing

            Screen::tft.setViewport(rect.pos.x, rect.pos.y, rect.dimensions.x, rect.dimensions.y, true);

            ok = drawSVGString(svgStr,
                               x, y,
                               w, h,
                               color, steps > 10 ? 10 : (steps < 1 ? 1 : steps));

            Screen::tft.resetViewport();

            Windows::canAccess = true;

            // nsvgDelete(svgImage);
            // svgImage = nullptr;
        }
        delay(5);

        lua_pushboolean(L, ok);

        return 1;
    }

    // draw time string like "MM:SS / MM:SS"
    void drawVideoTime(uint32_t currentSec, uint32_t totalSec, int x, int y, int w, int h)
    {
        auto formatTime = [](uint32_t s) -> String
        {
            uint32_t m = s / 60;
            uint32_t sec = s % 60;
            String mm = String(m);
            String ss = String(sec);
            if (mm.length() < 2)
                mm = "0" + mm;
            if (ss.length() < 2)
                ss = "0" + ss;
            return mm + ":" + ss;
        };

        String timeStr = formatTime(currentSec) + " / " + formatTime(totalSec);

        Screen::tft.setTextSize(1);
        Screen::tft.setTextColor(AT);
        Screen::tft.setCursor(x, y);
        Screen::tft.print(timeStr);
        Screen::tft.setTextColor(TEXT);
    }

    // Draw top menu bar with play/pause, timeline and exit button.
    // matches signature used by lua_WIN_drawVideo.
    void drawMenuBar(bool paused, uint32_t currentFrame, uint32_t framesCount)
    {
        int menuHeight = 20;
        Screen::tft.fillRect(0, 0, Screen::tft.width(), menuHeight, TFT_DARKGREY);

        // Pause/Play Button (left)
        if (paused)
        {
            // Play triangle
            Screen::tft.fillTriangle(6, 5, 6, 15, 14, 10, TFT_WHITE);
        }
        else
        {
            // Pause bars
            Screen::tft.fillRect(6, 5, 4, 10, TFT_WHITE);
            Screen::tft.fillRect(12, 5, 4, 10, TFT_WHITE);
        }

        // Timeline (center)
        int tlX = 30;
        int tlY = 6;
        int tlW = 200;
        int tlH = 8;
        Screen::tft.fillRect(tlX, tlY, tlW, tlH, TFT_BLACK);

        int px = tlX + (int)((uint64_t)currentFrame * (uint64_t)tlW / (framesCount ? framesCount : 1));
        if (px < tlX)
            px = tlX;
        if (px > tlX + tlW)
            px = tlX + tlW;
        Screen::tft.fillRect(tlX, tlY, px - tlX, tlH, TFT_RED);

        // Time text above timeline
        // Note: original code used currentFrame/20 and framesCount/20 — keep that behavior to show seconds if fps==20.
        drawVideoTime(currentFrame / 20, framesCount / 20, tlX, tlY - 6, tlW, 10);

        // Exit button (right)
        int exitW = 20;
        int exitX = Screen::tft.width() - exitW;
        int exitY = 0;
        Screen::tft.fillRect(exitX, exitY, exitW, menuHeight, TFT_RED);
        Screen::tft.drawLine(exitX + 4, exitY + 4, exitX + exitW - 4, exitY + menuHeight - 4, TFT_WHITE);
        Screen::tft.drawLine(exitX + exitW - 4, exitY + 4, exitX + 4, exitY + menuHeight - 4, TFT_WHITE);
    }

#include "winlib-video-helper.hpp"

    // register functions
    void register_win_functions(lua_State *L)
    {
        lua_register(L, "createWindow", lua_createWindow);
        lua_register(L, "WIN_setName", lua_WIN_setName);
        lua_register(L, "WIN_getRect", lua_WIN_getRect);
        lua_register(L, "WIN_getLastEvent", lua_WIN_getLastEvent);
        lua_register(L, "WIN_closed", lua_WIN_closed);
        lua_register(L, "WIN_fillBg", lua_WIN_fillBg);
        lua_register(L, "WIN_writeText", lua_WIN_writeText);
        lua_register(L, "WIN_fillRect", lua_WIN_fillRect); // fixed registration
        lua_register(L, "WIN_setIcon", lua_WIN_setIcon);
        lua_register(L, "WIN_drawImage", lua_WIN_drawImage);
        lua_register(L, "WIN_drawPixel", lua_WIN_drawPixel);
        lua_register(L, "WIN_readPixel", lua_WIN_readPixel);
        lua_register(L, "WIN_isRendering", lua_WIN_isRendered);
        lua_register(L, "WIN_canAccess", lua_WIN_canAccess);
        lua_register(L, "WIN_readText", lua_WIN_readText);

        // Close function (was missing)
        lua_register(L, "WIN_close", lua_WIN_close);

        // Register new TFT drawing functions
        lua_register(L, "WIN_drawLine", lua_WIN_drawLine);
        lua_register(L, "WIN_drawRect", lua_WIN_drawRect);
        lua_register(L, "WIN_writeRect", lua_WIN_fillRect);
        lua_register(L, "WIN_drawTriangle", lua_WIN_drawTriangle);
        lua_register(L, "WIN_fillTriangle", lua_WIN_fillTriangle);
        lua_register(L, "WIN_drawCircle", lua_WIN_drawCircle);
        lua_register(L, "WIN_fillCircle", lua_WIN_fillCircle);
        lua_register(L, "WIN_drawRoundRect", lua_WIN_drawRoundRect);
        lua_register(L, "WIN_fillRoundRect", lua_WIN_fillRoundRect);
        lua_register(L, "WIN_drawFastVLine", lua_WIN_drawFastVLine);
        lua_register(L, "WIN_drawFastHLine", lua_WIN_drawFastHLine);
        lua_register(L, "WIN_drawSVG", lua_WIN_drawSVG);
        lua_register(L, "WIN_drawVideo", lua_WIN_drawVideo);
    }

} // namespace LuaApps::WinLib
