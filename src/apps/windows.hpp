#pragma once
#include <vector>
#include <memory>
#include "../screen/index.hpp"
#include "window.hpp"

namespace Windows
{

    typedef std::unique_ptr<Window> WindowPtr;
    extern std::vector<WindowPtr> apps;

    // core API
    void add(WindowPtr w);
    void removeAt(int idx);
    void bringToFront(int idx);
    void loop();

    // draw helpers
    void drawTitleBar(Window &w);
    void drawContent(Window &w);
    void drawResizeBox(Window &w);

}
