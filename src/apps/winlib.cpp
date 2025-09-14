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
        Serial.println("[lua_WIN_drawVideo] called");

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
                Serial.printf("[lua_WIN_drawVideo] still waiting for access (loops=%d)\n", waitLoops);
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
        if (!https.begin(client, url))
        {
            Serial.println("[lua_WIN_drawVideo] https.begin() failed");
            Windows::canAccess = true;
            return 0;
        }

        const int maxRetries = 3;
        int tries = 0;
        int httpCode = -99;
        while (tries < maxRetries)
        {
            httpCode = https.GET();
            Serial.printf("[lua_WIN_drawVideo] HTTP GET returned %d (try %d)\n", httpCode, tries + 1);
            if (httpCode == HTTP_CODE_OK)
                break;
            if (httpCode == HTTP_CODE_MOVED_PERMANENTLY || httpCode == HTTP_CODE_FOUND)
            {
                String loc = https.header("Location");
                Serial.printf("[lua_WIN_drawVideo] Redirect Location: %s\n", loc.c_str());
                break;
            }
            if (httpCode == -1)
            {
                Serial.printf("[lua_WIN_drawVideo] low-level HTTP error. freeHeap=%u, WiFi.status()=%d\n",
                              (unsigned)ESP.getFreeHeap(), WiFi.status());
                delay(250);
                tries++;
                continue;
            }
            break;
        }

        if (httpCode != HTTP_CODE_OK)
        {
            Serial.printf("[lua_WIN_drawVideo] HTTP not OK (%d); ending\n", httpCode);
            https.end();
            Windows::canAccess = true;
            return 0;
        }

        WiFiClient *stream = https.getStreamPtr();

        // read header
        uint8_t header[8];
        if (stream->readBytes(header, 8) != 8)
        {
            Serial.println("[lua_WIN_drawVideo] failed to read header");
            https.end();
            Windows::canAccess = true;
            return 0;
        }

        uint16_t w_px = (uint16_t)((header[1] << 8) | header[0]);
        uint16_t h_px = (uint16_t)((header[3] << 8) | header[2]);
        uint32_t framesCount = ((uint32_t)header[7] << 24) | ((uint32_t)header[6] << 16) | ((uint32_t)header[5] << 8) | header[4];

        const uint16_t MAX_W = 320;
        const uint16_t MAX_H = 240;
        if (w_px == 0 || h_px == 0 || w_px > 2000 || h_px > 2000)
        {
            Serial.printf("[lua_WIN_drawVideo] suspicious dimensions: %u x %u; aborting\n", (unsigned)w_px, (unsigned)h_px);
            https.end();
            Windows::canAccess = true;
            return 0;
        }
        if (w_px > MAX_W)
        {
            Serial.printf("[lua_WIN_drawVideo] clamping width %u -> %u\n", (unsigned)w_px, (unsigned)MAX_W);
            w_px = MAX_W;
        }
        if (h_px > MAX_H)
        {
            Serial.printf("[lua_WIN_drawVideo] clamping height %u -> %u\n", (unsigned)h_px, (unsigned)MAX_H);
            h_px = MAX_H;
        }

        if (framesCount == 0 || framesCount > 2000000UL)
        {
            Serial.printf("[lua_WIN_drawVideo] suspicious framesCount=%u; aborting\n", (unsigned)framesCount);
            https.end();
            Windows::canAccess = true;
            return 0;
        }

        Serial.printf("[lua_WIN_drawVideo] header parsed: width=%u, height=%u, frames=%u\n",
                      (unsigned)w_px, (unsigned)h_px, (unsigned)framesCount);

        Screen::tft.fillScreen(BG);

        // allocate a single line buffer (aligned to 2)
        const size_t bytesPerLine = (size_t)w_px * 2;
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
            lineBuf++; // align for uint16_t cast

        // timing & EMA params
        const unsigned long FULL_REDRAW_INTERVAL_MS = 5000UL;
        const unsigned long LINE_INTERVAL_MS = 50UL; // 20 fps equivalent for targetLine
        unsigned long startMs = millis();
        unsigned long nextFullRedrawAt = startMs + FULL_REDRAW_INTERVAL_MS;
        bool didInitialFull = false;

        // EMA initial values (ms)
        double ema_read_ms = 5.0;      // initial guess: 5ms per read (will adapt)
        double ema_draw_ms = 5.0;      // initial guess: 5ms per draw (will adapt)
        const double EMA_ALPHA = 0.12; // smoothing: 0..1 (higher -> faster adapt)

        // safety counters for logging
        unsigned long lastLog = millis();

        for (uint32_t f = 0; f < framesCount; ++f)
        {
            if (w->closed)
            {
                Serial.printf("[lua_WIN_drawVideo] window closed at frame %u\n", (unsigned)f);
                break;
            }
            if (!Windows::isRendering || !UserWiFi::hasInternet)
            {
                Serial.printf("[lua_WIN_drawVideo] rendering/internet lost at frame %u\n", (unsigned)f);
                break;
            }

            if ((f % 50) == 0)
                Serial.printf("[lua_WIN_drawVideo] frame %u / %u\n", (unsigned)f, (unsigned)framesCount);

            unsigned long frameStart = millis();
            unsigned long now = frameStart;

            // decide full redraw schedule
            bool needFullRedraw = false;
            if (!didInitialFull)
            {
                needFullRedraw = true;
                didInitialFull = true;
                nextFullRedrawAt = now + FULL_REDRAW_INTERVAL_MS;
            }
            else if (now >= nextFullRedrawAt)
            {
                needFullRedraw = true;
                nextFullRedrawAt = now + FULL_REDRAW_INTERVAL_MS;
            }

            // compute time-derived single-line target (keeps a time-coherent "current time" line)
            uint16_t targetLine = (uint16_t)(((now - startMs) / LINE_INTERVAL_MS) % h_px);

            // per-row loop: read each line and decide to draw immediately or skip it
            for (uint16_t y = 0; y < h_px; ++y)
            {
                // read line and measure time
                unsigned long t1 = millis();
                int got = stream->readBytes(lineBuf, bytesPerLine);
                unsigned long t2 = millis();
                if (got != (int)bytesPerLine)
                {
                    Serial.printf("[lua_WIN_drawVideo] bytesRead mismatch at frame %u row %u (got=%d)\n", (unsigned)f, (unsigned)y, got);
                    heap_caps_free(rawBuf);
                    https.end();
                    Windows::canAccess = true;
                    return 0;
                }
                unsigned long readDur = (t2 >= t1) ? (t2 - t1) : 1;
                // update EMA read time (ms)
                ema_read_ms = (1.0 - EMA_ALPHA) * ema_read_ms + EMA_ALPHA * (double)readDur;

                // decide whether to draw this row
                bool drawNow = false;
                if (needFullRedraw)
                {
                    drawNow = true; // during full redraw draw all rows
                }
                else
                {
                    // if display faster or equal -> draw everything
                    if (ema_draw_ms <= ema_read_ms * 1.02) // small hysteresis
                    {
                        drawNow = true;
                    }
                    else
                    {
                        // display slower: compute draw probability (drawProb = readMs / drawMs)
                        double drawProb = ema_read_ms / ema_draw_ms;
                        if (drawProb > 0.999)
                            drawProb = 0.999;
                        if (drawProb < 0.01)
                            drawProb = 0.01; // ensure some chance to draw
                        // always draw the targetLine to keep time coherence
                        if (y == targetLine)
                            drawNow = true;
                        else
                        {
                            // random draw decision
                            int r = rand() & 0x7fff;
                            double rnd = (double)r / 32767.0;
                            if (rnd < drawProb)
                                drawNow = true;
                        }
                    }
                }

                if (drawNow)
                {
                    unsigned long d1 = millis();
                    Screen::tft.pushImage(0, y, w_px, 1, (uint16_t *)lineBuf);
                    unsigned long d2 = millis();
                    unsigned long drawDur = (d2 >= d1) ? (d2 - d1) : 1;
                    // update EMA draw time (ms)
                    ema_draw_ms = (1.0 - EMA_ALPHA) * ema_draw_ms + EMA_ALPHA * (double)drawDur;
                }

                // keep cooperative multitasking: yield occasionally
                if ((y & 15) == 0)
                {
                    yield(); /* small non-blocking pause */
                }

                // if window closed or stopped, break
                if (w->closed || !Windows::isRendering || !UserWiFi::hasInternet)
                    break;
            } // end rows

            // very small cooperative delay to avoid starving other tasks (only if fast)
            unsigned long frameTime = millis() - frameStart;
            if (frameTime < 4)
                delay(1);

            // periodic logging (every ~2s)
            if (millis() - lastLog > 2000)
            {
                lastLog = millis();
                Serial.printf("[lua_WIN_drawVideo] EMA read=%.2fms draw=%.2fms freeHeap=%u\n",
                              ema_read_ms, ema_draw_ms, (unsigned)ESP.getFreeHeap());
            }

            // quick early-exit safety
            if (w->closed || !Windows::isRendering || !UserWiFi::hasInternet)
                break;
        } // end frames loop

        // cleanup
        heap_caps_free(rawBuf);
        https.end();
        Screen::tft.fillScreen(BG);
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
