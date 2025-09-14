#include "winlib.hpp"
#include "apps/windows.hpp"
#include "screen/index.hpp"

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>

#include <unordered_map>
#include <memory>
#include <string>

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

        int screenId = luaL_checkinteger(L, 2);
        String svgStr = luaL_checkstring(L, 3);
        int x = luaL_checkinteger(L, 4);
        int y = luaL_checkinteger(L, 5);
        int w = luaL_checkinteger(L, 6);
        int h = luaL_checkinteger(L, 7);
        int color = luaL_checkinteger(L, 8);
        int steps = luaL_checkinteger(L, 9);

        Rect rect = getScreenRect(win, screenId);

        NSVGimage *svgImage = createSVG(svgStr);

        bool ok = svgImage != nullptr;

        if (ok)
        {
            while (!Windows::canAccess)
            {
                delay(rand() % 2);
            }
            Windows::canAccess = false;

            Screen::tft.setViewport(rect.pos.x, rect.pos.y, rect.dimensions.x, rect.dimensions.y, true);

            ok = drawSVGString(svgImage,
                               x, y,
                               w, h,
                               color, steps > 10 ? 10 : (steps < 1 ? 1 : steps));

            Screen::tft.resetViewport();

            Windows::canAccess = true;

            nsvgDelete(svgImage);
            svgImage = nullptr;
        }
        delay(5);

        lua_pushboolean(L, ok);

        return 1;
    }

    int lua_WIN_drawVideo(lua_State *L)
    {
        Serial.println("[lua_WIN_drawVideo] called (seekable + centered + controls)");

        Window *w = getWindow(L, 1);
        if (!w || w->closed)
        {
            Serial.println("[lua_WIN_drawVideo] no window or window closed; returning");
            return 0;
        }
        if (!w->wasClicked)
        {
            Serial.println("[lua_WIN_drawVideo] window not clicked on top; returning");
            return 0;
        }
        w->wasClicked = false;

        if (!Windows::isRendering || !UserWiFi::hasInternet)
        {
            Serial.printf("[lua_WIN_drawVideo] rendering=%d, hasInternet=%d; returning\n",
                          Windows::isRendering ? 1 : 0, UserWiFi::hasInternet ? 1 : 0);
            return 0;
        }

        // Acquire single-threaded access
        Serial.println("[lua_WIN_drawVideo] waiting for Windows::canAccess...");
        int waitLoops = 0;
        while (!Windows::canAccess)
        {
            delay(rand() % 3);
            waitLoops++;
            if ((waitLoops & 127) == 0)
                Serial.printf("[lua_WIN_drawVideo] still waiting (loops=%d)\n", waitLoops);
            yield();
        }
        Windows::canAccess = false;
        Serial.println("[lua_WIN_drawVideo] acquired access");

        const char *url_c = luaL_checkstring(L, 2);
        String url = String(url_c);
        Serial.printf("[lua_WIN_drawVideo] original URL: %s\n", url_c);
        if (url.indexOf("https://github.com/") == 0 && url.indexOf("/raw/refs/heads/") != -1)
        {
            url.replace("https://github.com/", "https://raw.githubusercontent.com/");
            url.replace("/raw/refs/heads/", "/");
            Serial.printf("[lua_WIN_drawVideo] converted to raw URL: %s\n", url.c_str());
        }

        Serial.printf("[lua_WIN_drawVideo] WiFi.status()=%d, freeHeap=%u\n", WiFi.status(), (unsigned)ESP.getFreeHeap());

        WiFiClientSecure client;
        client.setInsecure();
        HTTPClient https;

        auto beginGetNoRange = [&](HTTPClient &hc, WiFiClientSecure &c, const String &u) -> int
        {
            if (!hc.begin(c, u))
                return -999;
            return hc.GET();
        };

        // First full GET to read the header and start streaming from frame 0
        if (!https.begin(client, url))
        {
            Serial.println("[lua_WIN_drawVideo] https.begin() failed");
            Windows::canAccess = true;
            return 0;
        }
        int httpCode = https.GET();
        Serial.printf("[lua_WIN_drawVideo] HTTP GET returned %d (initial)\n", httpCode);
        if (httpCode != HTTP_CODE_OK)
        {
            Serial.printf("[lua_WIN_drawVideo] HTTP not OK (%d); ending\n", httpCode);
            https.end();
            Windows::canAccess = true;
            return 0;
        }

        WiFiClient *stream = https.getStreamPtr();

        // read 8-byte header (width, height, framesCount) from start of file
        uint8_t header[8];
        if (stream->readBytes(header, 8) != 8)
        {
            Serial.println("[lua_WIN_drawVideo] failed to read header");
            https.end();
            Windows::canAccess = true;
            return 0;
        }

        uint16_t v_w = (uint16_t)((header[1] << 8) | header[0]);
        uint16_t v_h = (uint16_t)((header[3] << 8) | header[2]);
        uint32_t framesCount = ((uint32_t)header[7] << 24) | ((uint32_t)header[6] << 16) | ((uint32_t)header[5] << 8) | header[4];

        const uint16_t MAX_W = 320, MAX_H = 240;
        if (v_w == 0 || v_h == 0 || v_w > 2000 || v_h > 2000)
        {
            Serial.printf("[lua_WIN_drawVideo] suspicious dims %u x %u; abort\n", (unsigned)v_w, (unsigned)v_h);
            https.end();
            Windows::canAccess = true;
            return 0;
        }
        if (v_w > MAX_W)
        {
            Serial.printf("[lua_WIN_drawVideo] clamping width %u -> %u\n", (unsigned)v_w, (unsigned)MAX_W);
            v_w = MAX_W;
        }
        if (v_h > MAX_H)
        {
            Serial.printf("[lua_WIN_drawVideo] clamping height %u -> %u\n", (unsigned)v_h, (unsigned)MAX_H);
            v_h = MAX_H;
        }
        if (framesCount == 0 || framesCount > 2000000UL)
        {
            Serial.printf("[lua_WIN_drawVideo] suspicious frames=%u; abort\n", (unsigned)framesCount);
            https.end();
            Windows::canAccess = true;
            return 0;
        }

        Serial.printf("[lua_WIN_drawVideo] header parsed: width=%u, height=%u, frames=%u\n", (unsigned)v_w, (unsigned)v_h, (unsigned)framesCount);

        // compute centered offsets inside the window
        int winW = Screen::tft.width();
        int winH = Screen::tft.height();
        int winX = 0;
        int winY = 0;
        
        int srcXOffset = 0, srcYStart = 0, drawW = (int)v_w, drawH = (int)v_h;
        if (drawW > winW)
        {
            srcXOffset = (drawW - winW) / 2;
            drawW = winW;
        }
        if (drawH > winH)
        {
            srcYStart = (drawH - winH) / 2;
            drawH = winH;
        }
        int dstX = winX + (winW - drawW) / 2;
        int dstY = winY + (winH - drawH) / 2;

        Screen::tft.fillScreen(BG);

        // single aligned line buffer
        const size_t bytesPerLine = (size_t)v_w * 2;
        uint8_t *rawBuf = (uint8_t *)heap_caps_malloc(bytesPerLine + 2, MALLOC_CAP_8BIT);
        if (!rawBuf)
        {
            Serial.println("[lua_WIN_drawVideo] failed to allocate line buffer");
            https.end();
            Windows::canAccess = true;
            return 0;
        }
        uint8_t *lineBuf = rawBuf;
        if (((uintptr_t)lineBuf & 1) != 0)
            lineBuf++;

        // EMA timing for adaptive skipping
        double ema_read_ms = 5.0, ema_draw_ms = 5.0;
        const double EMA_ALPHA = 0.12;
        const unsigned long FULL_REDRAW_INTERVAL_MS = 5000UL;
        const unsigned long LINE_INTERVAL_MS = 50UL; // conceptual 20fps
        unsigned long startMs = millis(), nextFullRedraw = startMs + FULL_REDRAW_INTERVAL_MS;
        bool didInitialFull = false;

        // UI and seek state
        bool paused = false, exitRequested = false;
        bool showControls = true;
        unsigned long controlsLastShownMs = millis(), CONTROLS_HIDE_MS = 3000UL;
        const int BTN_SIZE = 28;
        int btnExitX = dstX + drawW - BTN_SIZE - 6, btnExitY = dstY + 6;
        int btnPlayX = btnExitX - (BTN_SIZE + 6), btnPlayY = btnExitY;
        int timelineX = dstX + 6, timelineW = drawW - 12, timelineY = dstY + drawH - 12, timelineH = 6;

        // seeking helpers
        const uint32_t bytesPerFrame = (uint32_t)v_h * (uint32_t)bytesPerLine;
        const uint32_t dataStartOffset = 8; // header size
        uint32_t currentFrame = 0;          // absolute frame index in file (we start at 0)
        bool seekRequested = false;
        uint32_t seekFrame = 0;

        // function to start a ranged or un-ranged GET at a byte offset
        auto startGetAtOffset = [&](uint32_t byteOffset, bool expectFullFileOn200, HTTPClient &hc, WiFiClientSecure &c, const String &u) -> int
        {
            // end any previous connection in hc
            hc.end();
            if (!hc.begin(c, u))
                return -999;
            if (byteOffset > 0)
            {
                String rangeHeader = "bytes=" + String(byteOffset) + "-";
                hc.addHeader("Range", rangeHeader);
            }
            int code = hc.GET();
            return code;
        };

        // At this point 'https' and 'stream' point to the start of the file *after* consuming header.
        // currentFrame is 0 and stream already points after the header -> ready to read frame 0 lines.

        unsigned long lastLog = millis();

        for (; currentFrame < framesCount; /* incremented in loop */)
        {
            if (w->closed)
            {
                Serial.printf("[lua_WIN_drawVideo] window closed at frame %u\n", (unsigned)currentFrame);
                break;
            }
            if (!Windows::isRendering || !UserWiFi::hasInternet)
            {
                Serial.printf("[lua_WIN_drawVideo] stopped/internet lost at frame %u\n", (unsigned)currentFrame);
                break;
            }
            if ((currentFrame & 63) == 0)
                Serial.printf("[lua_WIN_drawVideo] playing frame %u / %u\n", (unsigned)currentFrame, (unsigned)framesCount);

            unsigned long frameStart = millis();
            unsigned long now = frameStart;

            // UI hide timer
            if (showControls && (now - controlsLastShownMs) > CONTROLS_HIDE_MS)
                showControls = false;

            // handle click events (seek / play / exit). This part needs coordinate fields in Window.
            if (w->wasClicked)
            {
                w->wasClicked = false;
                int cx = -1, cy = -1;
                bool haveCoords = false;
#ifdef HAVE_WINDOW_CLICK_COORDS
                cx = w->clickX;
                cy = w->clickY;
                haveCoords = true;
#endif
                // If coordinates are available, interpret clicks precisely
                if (haveCoords)
                {
                    // Click on Play/Pause?
                    if (cx >= btnPlayX && cx < btnPlayX + BTN_SIZE && cy >= btnPlayY && cy < btnPlayY + BTN_SIZE)
                    {
                        paused = !paused;
                        showControls = true;
                        controlsLastShownMs = millis();
                    }
                    // Click on Exit?
                    else if (cx >= btnExitX && cx < btnExitX + BTN_SIZE && cy >= btnExitY && cy < btnExitY + BTN_SIZE)
                    {
                        exitRequested = true;
                    }
                    // Click on timeline -> seek
                    else if (cx >= timelineX && cx <= (timelineX + timelineW) && cy >= (timelineY - 8) && cy <= (timelineY + timelineH + 8))
                    {
                        float frac = (float)(cx - timelineX) / (float)timelineW;
                        if (frac < 0.0f)
                            frac = 0.0f;
                        if (frac > 1.0f)
                            frac = 1.0f;
                        uint32_t target = (uint32_t)round(frac * (float)(framesCount - 1));
                        seekRequested = true;
                        seekFrame = target;
                        showControls = true;
                        controlsLastShownMs = millis();
                        Serial.printf("[lua_WIN_drawVideo] timeline click -> seek to frame %u (frac=%.3f)\n", (unsigned)seekFrame, frac);
                    }
                    else
                    {
                        // Click elsewhere toggles controls/paused
                        paused = !paused;
                        showControls = true;
                        controlsLastShownMs = millis();
                    }
                }
                else
                {
                    // No coords available: toggle pause on click and show controls.
                    paused = !paused;
                    showControls = true;
                    controlsLastShownMs = millis();
                }
            }

            // handle seek if requested
            if (seekRequested)
            {
                seekRequested = false;
                if (seekFrame >= framesCount)
                    seekFrame = framesCount - 1;
                Serial.printf("[lua_WIN_drawVideo] performing seek to frame %u\n", (unsigned)seekFrame);

                // compute byte offset to the start of seekFrame
                uint32_t off = dataStartOffset + (uint32_t)seekFrame * bytesPerFrame;

                // perform ranged GET at offset
                // Close old connection and make a new request with Range header
                https.end(); // close existing

                // start new connection with Range header
                if (!https.begin(client, url))
                {
                    Serial.println("[lua_WIN_drawVideo] https.begin() failed on seek");
                    heap_caps_free(rawBuf);
                    Windows::canAccess = true;
                    return 0;
                }
                String rangeHeader = "bytes=" + String(off) + "-";
                https.addHeader("Range", rangeHeader);
                int code = https.GET();
                Serial.printf("[lua_WIN_drawVideo] range GET returned %d for offset %u\n", code, (unsigned)off);

                if (code == HTTP_CODE_PARTIAL_CONTENT) // 206 -> server responds with partial content starting at offset
                {
                    stream = https.getStreamPtr();
                    // stream starts at byte offset -> do NOT read header again
                    currentFrame = seekFrame;
                    // continue playback from currentFrame
                    Serial.printf("[lua_WIN_drawVideo] seek OK (206). Resuming at frame %u\n", (unsigned)currentFrame);
                }
                else if (code == HTTP_CODE_OK)
                {
                    // server ignored Range and returned full file. We have a full stream starting with header.
                    stream = https.getStreamPtr();
                    // read header from this stream
                    uint8_t tmpHeader[8];
                    if (stream->readBytes(tmpHeader, 8) != 8)
                    {
                        Serial.println("[lua_WIN_drawVideo] failed to read header after 200 OK on seek");
                        https.end();
                        heap_caps_free(rawBuf);
                        Windows::canAccess = true;
                        return 0;
                    }
                    // Now we need to skip frames from 0..seekFrame-1 to reach seekFrame
                    Serial.printf("[lua_WIN_drawVideo] server returned full file after Range -> skipping to frame %u by consuming bytes\n", (unsigned)seekFrame);
                    for (uint32_t sf = 0; sf < seekFrame; ++sf)
                    {
                        for (uint16_t yy = 0; yy < v_h; ++yy)
                        {
                            int got = stream->readBytes(lineBuf, bytesPerLine);
                            if (got != (int)bytesPerLine)
                            {
                                Serial.printf("[lua_WIN_drawVideo] failed while skipping (frame %u row %u) got=%d\n", (unsigned)sf, (unsigned)yy, got);
                                https.end();
                                heap_caps_free(rawBuf);
                                Windows::canAccess = true;
                                return 0;
                            }
                        }
                        // yield occasionally while skipping large chunks
                        if ((sf & 7) == 0)
                        {
                            yield();
                        }
                    }
                    currentFrame = seekFrame;
                    Serial.printf("[lua_WIN_drawVideo] skip done; resuming at frame %u\n", (unsigned)currentFrame);
                }
                else
                {
                    // ranged GET failed: try fallback to starting from beginning and skipping
                    Serial.printf("[lua_WIN_drawVideo] range GET failed (%d) - falling back to full GET and skip\n", code);
                    https.end();
                    if (!https.begin(client, url))
                    {
                        Serial.println("[lua_WIN_drawVideo] https.begin() failed (fallback)");
                        heap_caps_free(rawBuf);
                        Windows::canAccess = true;
                        return 0;
                    }
                    int code2 = https.GET();
                    if (code2 != HTTP_CODE_OK)
                    {
                        Serial.printf("[lua_WIN_drawVideo] fallback GET failed (%d)\n", code2);
                        https.end();
                        heap_caps_free(rawBuf);
                        Windows::canAccess = true;
                        return 0;
                    }
                    stream = https.getStreamPtr();
                    // read header
                    if (stream->readBytes(header, 8) != 8)
                    {
                        Serial.println("[lua_WIN_drawVideo] failed reading header on fallback");
                        https.end();
                        heap_caps_free(rawBuf);
                        Windows::canAccess = true;
                        return 0;
                    }
                    // skip until seekFrame as above
                    for (uint32_t sf = 0; sf < seekFrame; ++sf)
                    {
                        for (uint16_t yy = 0; yy < v_h; ++yy)
                        {
                            int got = stream->readBytes(lineBuf, bytesPerLine);
                            if (got != (int)bytesPerLine)
                            {
                                Serial.printf("[lua_WIN_drawVideo] fallback skip failed at frame %u row %u\n", (unsigned)sf, (unsigned)yy);
                                https.end();
                                heap_caps_free(rawBuf);
                                Windows::canAccess = true;
                                return 0;
                            }
                        }
                        if ((sf & 7) == 0)
                        {
                            yield();
                        }
                    }
                    currentFrame = seekFrame;
                }
            } // end handle seekRequested

            // Now play the currentFrame: for each frame we must consume v_h rows
            // Full redraw scheduling & time-target line computed from now
            unsigned long now2 = millis();
            bool needFull = false;
            if (!didInitialFull)
            {
                needFull = true;
                didInitialFull = true;
                nextFullRedraw = now2 + FULL_REDRAW_INTERVAL_MS;
            }
            else if (now2 >= nextFullRedraw)
            {
                needFull = true;
                nextFullRedraw = now2 + FULL_REDRAW_INTERVAL_MS;
            }

            float progress = (framesCount > 0) ? ((float)currentFrame / (float)(framesCount - 1)) : 0.0f;
            uint16_t timeTargetLine = (uint16_t)(((millis() - startMs) / LINE_INTERVAL_MS) % v_h);

            for (uint16_t y = 0; y < v_h; ++y)
            {
                unsigned long t1 = millis();
                int got = stream->readBytes(lineBuf, bytesPerLine);
                unsigned long t2 = millis();
                if (got != (int)bytesPerLine)
                {
                    Serial.printf("[lua_WIN_drawVideo] bytesRead mismatch frame %u row %u (got=%d)\n", (unsigned)currentFrame, (unsigned)y, got);
                    heap_caps_free(rawBuf);
                    https.end();
                    Windows::canAccess = true;
                    return 0;
                }
                unsigned long readDur = (t2 >= t1) ? (t2 - t1) : 1;
                ema_read_ms = (1.0 - EMA_ALPHA) * ema_read_ms + EMA_ALPHA * (double)readDur;

                // only draw rows that fall into the visible cropped window
                if ((int)y < srcYStart || (int)y >= srcYStart + drawH)
                {
                    if ((y & 15) == 0)
                        yield();
                    continue;
                }
                int visibleRowIndex = y - srcYStart;
                int destY = dstY + visibleRowIndex;

                bool mustDraw = (y == timeTargetLine);
                bool drawNow = false;
                if (needFull)
                    drawNow = true;
                else if (paused)
                    drawNow = false;
                else if (ema_draw_ms <= ema_read_ms * 1.02)
                    drawNow = true;
                else
                {
                    double drawProb = ema_read_ms / ema_draw_ms;
                    if (drawProb > 0.999)
                        drawProb = 0.999;
                    if (drawProb < 0.02)
                        drawProb = 0.02;
                    if (mustDraw)
                        drawNow = true;
                    else
                    {
                        int r = rand() & 0x7fff;
                        double rnd = (double)r / 32767.0;
                        if (rnd < drawProb)
                            drawNow = true;
                    }
                }

                if (drawNow)
                {
                    // crop horizontally using srcXOffset
                    uint16_t *pixels = (uint16_t *)lineBuf;
                    uint16_t *srcPtr = pixels + srcXOffset;
                    Screen::tft.pushImage(dstX, destY, drawW, 1, (uint16_t *)srcPtr);
                    unsigned long d2 = millis();
                    unsigned long drawDur = (d2 >= t2) ? (d2 - t2) : 1;
                    ema_draw_ms = (1.0 - EMA_ALPHA) * ema_draw_ms + EMA_ALPHA * (double)drawDur;
                }

                if ((y & 15) == 0)
                    yield();

                if (w->closed || !Windows::isRendering || !UserWiFi::hasInternet)
                    break;
            } // end rows for frame

            // overlay controls & timeline
            if (showControls)
            {
                // controls background
                Screen::tft.fillRect(dstX + 4, dstY + 4, drawW - 8, 40, TFT_BLACK);
                Screen::tft.drawRect(dstX + 4, dstY + 4, drawW - 8, 40, TFT_WHITE);

                // Play/Pause
                Screen::tft.fillRect(btnPlayX, btnPlayY, BTN_SIZE, BTN_SIZE, TFT_DARKGREY);
                Screen::tft.drawRect(btnPlayX, btnPlayY, BTN_SIZE, BTN_SIZE, TFT_WHITE);
                if (paused)
                {
                    int cx = btnPlayX + BTN_SIZE / 2, cy = btnPlayY + BTN_SIZE / 2, s = 7;
                    Screen::tft.fillTriangle(cx - s / 2, cy - s, cx - s / 2, cy + s, cx + s, cy, TFT_WHITE);
                }
                else
                {
                    int gap = 4, bw = 4;
                    Screen::tft.fillRect(btnPlayX + 7, btnPlayY + 7, bw, BTN_SIZE - 14, TFT_WHITE);
                    Screen::tft.fillRect(btnPlayX + 7 + bw + gap, btnPlayY + 7, bw, BTN_SIZE - 14, TFT_WHITE);
                }

                // Exit
                Screen::tft.fillRect(btnExitX, btnExitY, BTN_SIZE, BTN_SIZE, TFT_DARKGREY);
                Screen::tft.drawRect(btnExitX, btnExitY, BTN_SIZE, BTN_SIZE, TFT_WHITE);
                Screen::tft.drawLine(btnExitX + 6, btnExitY + 6, btnExitX + BTN_SIZE - 6, btnExitY + BTN_SIZE - 6, TFT_WHITE);
                Screen::tft.drawLine(btnExitX + BTN_SIZE - 6, btnExitY + 6, btnExitX + 6, btnExitY + BTN_SIZE - 6, TFT_WHITE);

                // Timeline
                Screen::tft.drawRect(timelineX - 1, timelineY - 1, timelineW + 2, timelineH + 2, TFT_WHITE);
                int progW = (int)(progress * (float)timelineW);
                if (progW < 0)
                    progW = 0;
                if (progW > timelineW)
                    progW = timelineW;
                Screen::tft.fillRect(timelineX, timelineY, progW, timelineH, TFT_GREEN);
                Screen::tft.fillCircle(timelineX + progW, timelineY + (timelineH / 2), 3, TFT_WHITE);
            }

            // small cooperative sleep
            unsigned long frameTime = millis() - frameStart;
            if (frameTime < 4)
                delay(1);

            // advance to next frame
            currentFrame++;

            if (exitRequested)
            {
                Serial.println("[lua_WIN_drawVideo] exitRequested -> breaking");
                break;
            }

            if (millis() - lastLog > 2000)
            {
                lastLog = millis();
                Serial.printf("[lua_WIN_drawVideo] EMA read=%.2fms draw=%.2fms freeHeap=%u\n", ema_read_ms, ema_draw_ms, (unsigned)ESP.getFreeHeap());
            }
        } // end play loop

        // cleanup
        heap_caps_free(rawBuf);
        https.end();
        // clear video area (optional)
        Screen::tft.fillRect(dstX, dstY, drawW, drawH, BG);
        Windows::canAccess = true;
        Serial.printf("[lua_WIN_drawVideo] finished, released access; freeHeap=%u\n", (unsigned)ESP.getFreeHeap());
        return 0;
    }

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
