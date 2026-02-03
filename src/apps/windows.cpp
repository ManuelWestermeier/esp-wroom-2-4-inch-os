#include "windows.hpp"

namespace Windows
{
    std::vector<WindowPtr> apps;
    bool isRendering = false;
    bool isUsingKeyBoard = false;
    bool canAccess = true;
    Rect timeButton{{320 - 42 - 5, 240 - 16 - 5}, {42, 16}};
    unsigned long lastRendered = 0;

    // Helper to mark all windows as needing redraw
    void markAllNeedRedraw()
    {
        lastRendered = millis();
        for (auto &p : apps)
        {
            if (p)
                p->needRedraw = true;
        }
    }

    void add(WindowPtr w)
    {
        while (!canAccess)
        {
            delay(3);
        }

        canAccess = false;
        apps.push_back(std::move(w));
        Serial.println("Filling screen with background color...");
        Screen::tft.fillScreen(BG);
        // screen was cleared -> all windows need redraw
        markAllNeedRedraw();
        Serial.println("=== Window::init completed ===");
        canAccess = true;
    }

    void removeAt(int idx)
    {
        if (idx >= 0 && idx < (int)apps.size())
        {
            apps[idx]->closed = true;
            apps.erase(apps.begin() + idx);
            // z-order changed / windows removed -> need redraw
            markAllNeedRedraw();
        }
    }

    void remove(Window *win)
    {
        while (!canAccess)
            delay(rand() % 3);
        canAccess = false;

        if (!win->closed)
        {
            win->closed = true;

            auto it = std::find_if(apps.begin(), apps.end(),
                                   [&](const WindowPtr &ptr)
                                   {
                                       return ptr.get() == win;
                                   });

            if (it != apps.end())
                apps.erase(it); // this deletes the Window automatically
        }

        Screen::tft.fillScreen(BG);
        // screen was cleared -> all windows need redraw
        markAllNeedRedraw();
        canAccess = true;
    }

    void bringToFront(int idx)
    {
        if (idx < 0 || idx >= (int)apps.size())
            return;
        auto it = apps.begin() + idx;
        WindowPtr tmp = std::move(*it);
        apps.erase(it);
        apps.push_back(std::move(tmp));
        // z-order changed -> all windows need redraw
        markAllNeedRedraw();
    }

    void drawWindows(Vec pos, Vec move, MouseState state)
    {
        // pick topmost window under cursor
        int activeIdx = -1;
        for (int i = (int)apps.size() - 1; i >= 0; --i)
        {
            Window &w = *apps[i];
            if (Rect{w.off + Vec{-1, -13}, w.size + Vec{12 + 2, 13}}.isIn(pos))
            {
                activeIdx = i;
                break;
            }
        }

        if (activeIdx != -1)
        {
            bringToFront(activeIdx);
            Window &w = *apps.back();

            Vec rel{pos.x - w.off.x, pos.y - w.off.y};

            if (Rect{w.off, w.size}.isIn(pos))
            {
                w.lastEvent = {state, rel, move};
                if (state != MouseState::Up)
                {
                    w.wasClicked = true;
                }
            }

            Rect rightSpriteArea = {w.off + Vec{w.size.x, 0}, {Window::resizeBoxSize, w.size.y - Window::resizeBoxSize}};
            if (rightSpriteArea.isIn(pos))
            {
                Vec relRight = {pos.x - rightSpriteArea.pos.x, pos.y - rightSpriteArea.pos.y};
                w.lastEventRightSprite = {state, relRight, move};
                if (state != MouseState::Up)
                {
                    w.wasClicked = true;
                }
            }

            // drag
            if (state == MouseState::Held && (w.dragArea().isIn(pos) || w.dragArea().isIn(pos - move)))
            {
                Vec proposedOff = w.off + move;

                // Collision detection
                bool collides = false;
                constexpr int margin = 3;

                Rect nextRect = Rect{Vec{0, -12} + proposedOff, w.size + Vec{12, 12}};
                Rect oldRect = Rect{Vec{0, -12} + w.off, w.size + Vec{12, 12}};

                for (size_t i = 0; i < apps.size() - 1; ++i)
                {
                    const Window &otherWin = *apps[i];
                    Rect otherRect = Rect{otherWin.off + Vec{0, -12}, otherWin.size + Vec{12, 12}};

                    if (oldRect.intersects(otherRect))
                    {
                        collides = false;
                        break;
                    }
                    if (nextRect.intersects(otherRect))
                    {
                        collides = true;
                        break;
                    }
                }

                if (!collides)
                {
                    w.off = proposedOff;
                    // this window moved -> it needs redraw
                    Screen::tft.fillScreen(BG);
                    // screen cleared -> all windows need redraw
                    markAllNeedRedraw();
                }
            }

            // resize
            if (state == MouseState::Held && (w.resizeArea().isIn(pos) || w.resizeArea().isIn(pos - move)))
            {
                Vec proposedSize = {
                    constrain(w.size.x + move.x, Window::minSize.x, Window::maxSize.x),
                    constrain(w.size.y + move.y, Window::minSize.y, Window::maxSize.y),
                };

                // Collision detection
                bool collides = false;
                Rect nextRect = Rect{Vec{0, -12} + w.off, proposedSize + Vec{12, 12}};
                Rect oldRect = Rect{Vec{0, -12} + w.off, w.size + Vec{12, 12}};

                for (size_t i = 0; i < apps.size() - 1; ++i)
                {
                    const Window &otherWin = *apps[i];
                    Rect otherRect = Rect{otherWin.off + Vec{0, -12}, otherWin.size + Vec{12, 12}};

                    if (oldRect.intersects(otherRect))
                    {
                        collides = false;
                        break;
                    }

                    if (nextRect.intersects(otherRect))
                    {
                        collides = true;
                        break;
                    }
                }

                if (!collides)
                {
                    w.size = proposedSize;
                    // this window resized -> it needs redraw
                    Screen::tft.fillScreen(BG);
                    // screen cleared -> all windows need redraw
                    markAllNeedRedraw();
                }
            }

            // close
            if (state == MouseState::Down && w.closeBtn().isIn(pos))
            {
                removeAt((int)apps.size() - 1);
                auto area = Rect{w.off + Vec{-1, -13}, w.size + Vec{12 + 2, 14}};
                Screen::tft.fillRect(area.pos.x, area.pos.y, area.dimensions.x, area.dimensions.y, BG);
                // area cleared -> all windows need redraw
                markAllNeedRedraw();
            }
        }
        else
        {
            for (auto &p : apps)
            {
                Window &w = *p;
                w.off += move;
                // each window moved -> needs redraw
            }
            if (move.x != 0 || move.y != 0)
            {
                Screen::tft.fillScreen(BG);
                // screen cleared -> all windows need redraw
                markAllNeedRedraw();
                drawTime();
            }
        }

        // render all
        for (auto &p : apps)
        {
            Window &w = *p;
            if (Rect{0, 0, 320, 240}.intersects(Rect{w.off + Vec{-1, -13}, w.size + Vec{12 + 2, 13}}))
            {
                // Only redraw title/resize/time if window intersects screen.
                // mark per-window redraw was already set where appropriate.
                drawTitleBar(w);
                drawResizeBox(w);
                drawTime();
            }
        }
    }

    void drawMenu(Vec pos, Vec move, MouseState state);

    void loop()
    {
        updateSVGList();
        while (!canAccess)
        {
            delay(5);
        }

        canAccess = false;
        drawTime();

        static MouseState lastState = MouseState::Up;

        auto touch = Screen::getTouchPos();

        MouseState state = touch.clicked
                               ? (lastState == MouseState::Up ? MouseState::Down : MouseState::Held)
                               : MouseState::Up;
        Vec pos = {touch.x, touch.y};
        Vec move = (state != MouseState::Up) ? touch.move : Vec{0, 0};
        lastState = state;

        // time button toggles rendering
        static bool lastBtnVal = HIGH;
        bool btnClick = digitalRead(0);
        if ((timeButton.isIn(pos) && state == MouseState::Down) || (btnClick == LOW && lastBtnVal != LOW))
        {
            Screen::tft.fillScreen(BG);
            isRendering = !isRendering;
            // isRendering changed -> all windows need redraw
            markAllNeedRedraw();
        }
        lastBtnVal = btnClick;

        if (isRendering)
            drawWindows(pos, move, state);
        else
            drawMenu(pos, move, state);

        canAccess = true;
    }

    void drawCloseX(int x, int y, uint16_t color)
    {
        x += 2;
        y += 2;
        // Draw a centered, bold X inside an 8x8 square
        for (int i = 0; i < 8; i++)
        {
            // Diagonal from top-left to bottom-right
            Screen::tft.drawPixel(x + i, y + i, color);
            Screen::tft.drawPixel(x + i, y + i + 1, color); // bold vertical
            Screen::tft.drawPixel(x + i + 1, y + i, color); // bold horizontal

            // Diagonal from top-right to bottom-left
            Screen::tft.drawPixel(x + 7 - i, y + i, color);
            Screen::tft.drawPixel(x + 7 - i, y + i + 1, color); // bold vertical
            Screen::tft.drawPixel(x + 6 - i, y + i, color);     // bold horizontal
        }
    }

    void drawResizeIcon(int x, int y, uint16_t color)
    {
        x++;
        y++;

        // Main diagonal (thicker, longer)
        Screen::tft.drawLine(x, y, x + 9, y + 9, color);
        Screen::tft.drawLine(x + 1, y, x + 9, y + 8, color);
        Screen::tft.drawLine(x, y + 1, x + 8, y + 9, color);

        // Top-left arrow head
        Screen::tft.drawLine(x, y, x + 4, y, color); // horizontal tip
        Screen::tft.drawLine(x, y, x, y + 4, color); // vertical tip
        Screen::tft.drawPixel(x + 1, y + 1, color);  // corner pixel for emphasis

        // Bottom-right arrow head (mirrored)
        Screen::tft.drawLine(x + 5, y + 9, x + 9, y + 9, color); // horizontal tip
        Screen::tft.drawLine(x + 9, y + 5, x + 9, y + 9, color); // vertical tip
        Screen::tft.drawPixel(x + 8, y + 8, color);              // corner pixel for emphasis
    }

    void drawTitleBar(Window &w)
    {
        auto d = w.dragArea();
        auto c = w.closeBtn();

        // full screen
        Screen::tft.drawRect(
            w.off.x - 1, w.off.y - Window::titleBarHeight - 1,
            w.size.x + 2 + 12, w.size.y + Window::titleBarHeight + 2,
            TEXT);

        // icon
        Screen::tft.pushImage(d.pos.x, d.pos.y, 12, 12, w.icon);
        Screen::tft.drawLine(d.pos.x + 12, d.pos.y, d.pos.x + 12, d.pos.y + 12, TEXT);

        // drag area
        Screen::tft.fillRectHGradient(
            d.pos.x + 13, d.pos.y,
            d.dimensions.x - Window::closeBtnSize - 14, d.dimensions.y,
            ACCENT2 - RGB(20, 20, 0), ACCENT2);
        Screen::tft.drawLine(d.pos.x + 12 + d.dimensions.x - Window::closeBtnSize - 13, d.pos.y, d.pos.x + 12 + d.dimensions.x - Window::closeBtnSize - 13, d.pos.y + 12, TEXT);

        // drag are text
        if (Rect{0, 0, 320, 240}.intersects(d))
        {
            Screen::tft.setTextSize(1);
            Screen::tft.setCursor(d.pos.x + 2 + 12, d.pos.y + 2);
            int maxC = (d.dimensions.x - 32) / 6;

            for (int i = 0; i < std::min((int)w.name.length(), maxC); ++i)
                if (Rect{0, 0, 320, 240}.intersects(Rect{d.pos.x + 2 + 12 + 6 * (i + 1), d.pos.y + 2, 6, 8}))
                    Screen::tft.print(w.name[i]);
        }

        Screen::tft.fillRect(c.pos.x, c.pos.y, c.dimensions.x, c.dimensions.y, DANGER);
        drawCloseX(c.pos.x, c.pos.y, TEXT); // oder TEXT

        Screen::tft.setTextSize(2);
    }

    void drawResizeBox(Window &w)
    {
        auto r = w.resizeArea();
        Screen::tft.fillRect(r.pos.x, r.pos.y, r.dimensions.x, r.dimensions.y, ACCENT3);
        drawResizeIcon(r.pos.x, r.pos.y, TEXT);
    }

    void drawTime()
    {
        int w = timeButton.dimensions.x;
        int h = timeButton.dimensions.y;
        int x = timeButton.pos.x;
        int y = timeButton.pos.y;

        auto time = UserTime::get();
        String hour = String(time.tm_hour);
        String minute = String(time.tm_min);
        if (minute.length() < 2)
            minute = "0" + minute;
        if (hour.length() < 2)
            hour = "0" + hour;

        String timeStr = hour + ":" + minute;

        if (time.tm_year == 0)
        {
            timeStr = "XX:XX";
        }

        Screen::tft.setTextSize(1);
        Screen::tft.setTextColor(AT);
        Screen::tft.fillRoundRect(x, y, w, h, 4, ACCENT);
        Screen::tft.setCursor(x + 6, y + 4);
        Screen::tft.print(timeStr);
        Screen::tft.setTextColor(TEXT);
    }

} // namespace Windows

#include "windows-menu.hpp"
