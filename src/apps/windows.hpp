#pragma once

#include <vector>
#include <memory>

#include "../screen/index.hpp"
#include "../utils/time.hpp"
#include "../utils/rect.hpp"
#include "../utils/vec.hpp"

struct Window;
enum class MouseState;

namespace Windows
{

    typedef std::unique_ptr<Window> WindowPtr;
    extern std::vector<WindowPtr> apps;
    extern bool isRendering;
    extern Rect timeButton;

    // core API
    void add(WindowPtr w);
    void removeAt(int idx);
    void bringToFront(int idx);
    void drawWindows(Vec pos, Vec move, MouseState state);
    void drawMenu(Vec pos, Vec move, MouseState state);
    void loop();

    // draw helpers
    void drawTitleBar(Window &w);
    void drawContent(Window &w);
    void drawResizeBox(Window &w);
    void drawTime();
}

#include "window.hpp"
