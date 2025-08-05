#include "windows.hpp"

namespace Windows
{
    std::vector<WindowPtr> apps;
    bool isRendering = true;

    void add(WindowPtr w)
    {
        apps.push_back(std::move(w));
    }

    void removeAt(int idx)
    {
        apps[idx]->closed = true;
        if (idx >= 0 && idx < (int)apps.size())
            apps.erase(apps.begin() + idx);
    }

    void bringToFront(int idx)
    {
        if (idx < 0 || idx >= (int)apps.size())
            return;
        auto it = apps.begin() + idx;
        WindowPtr tmp = std::move(*it);
        apps.erase(it);
        apps.push_back(std::move(tmp));
    }

    void loop()
    {
        drawTime();

        static MouseState lastState = MouseState::Up;

        auto touch = Screen::getTouchPos();

        MouseState state = touch.clicked
                               ? (lastState == MouseState::Up ? MouseState::Down : MouseState::Held)
                               : MouseState::Up;
        Vec pos = {touch.x, touch.y};
        Vec move = (state != MouseState::Up) ? touch.move : Vec{0, 0};
        lastState = state;

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
            }

            Rect rightSpriteArea = {w.off + Vec{w.size.x, 0}, {Window::resizeBoxSize, w.size.y - Window::resizeBoxSize}};
            if (rightSpriteArea.isIn(pos))
            {
                Vec relRight = {pos.x - rightSpriteArea.pos.x, pos.y - rightSpriteArea.pos.y};
                w.lastEventRightSprite = {state, relRight, move};
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
                    Screen::tft.fillScreen(RGB(245, 245, 255));
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
                    w.resizeSprite();
                    Screen::tft.fillScreen(RGB(245, 245, 255));
                }
            }

            // close
            if (state == MouseState::Down && w.closeBtn().isIn(pos))
            {
                removeAt((int)apps.size() - 1);
                auto area = Rect{w.off + Vec{-1, -13}, w.size + Vec{12 + 2, 14}};
                Screen::tft.fillRect(area.pos.x, area.pos.y, area.dimensions.x, area.dimensions.y, RGB(245, 245, 255));
            }
        }
        else
        {
            for (auto &p : apps)
            {
                Window &w = *p;
                w.off += move;
            }
            if (move.x != 0 || move.y != 0)
            {
                Screen::tft.fillScreen(RGB(245, 245, 255));
                drawTime();
            }
        }

        // render all
        for (auto &p : apps)
        {
            Window &w = *p;
            if (Rect{0, 0, 320, 240}.intersects(Rect{w.off + Vec{-1, -13}, w.size + Vec{12 + 2, 13}}))
            {
                drawTitleBar(w);
                drawContent(w);
                drawResizeBox(w);
                drawTime();
            }
        }
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
            TFT_BLACK);

        // icon
        Screen::tft.pushImage(d.pos.x, d.pos.y, 12, 12, w.icon);
        Screen::tft.drawLine(d.pos.x + 12, d.pos.y, d.pos.x + 12, d.pos.y + 12, TFT_BLACK);

        // drag area
        Screen::tft.fillRectHGradient(
            d.pos.x + 13, d.pos.y,
            d.dimensions.x - Window::closeBtnSize - 14, d.dimensions.y,
            RGB(200, 200, 250), RGB(220, 220, 250));
        Screen::tft.drawLine(d.pos.x + 12 + d.dimensions.x - Window::closeBtnSize - 13, d.pos.y, d.pos.x + 12 + d.dimensions.x - Window::closeBtnSize - 13, d.pos.y + 12, TFT_BLACK);

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

        Screen::tft.fillRect(c.pos.x, c.pos.y, c.dimensions.x, c.dimensions.y, RGB(255, 150, 150));
        drawCloseX(c.pos.x, c.pos.y, RGB(0, 0, 0)); // oder TFT_BLACK

        Screen::tft.setTextSize(2);
    }

    void drawContent(Window &w)
    {
        w.sprite.pushSprite(w.off.x, w.off.y);
        w.rightSprite.pushSprite(w.off.x + w.size.x, w.off.y);
    }

    void drawResizeBox(Window &w)
    {
        auto r = w.resizeArea();
        Screen::tft.fillRect(r.pos.x, r.pos.y, r.dimensions.x, r.dimensions.y, RGB(180, 180, 255));
        drawResizeIcon(r.pos.x, r.pos.y, TFT_BLACK);
    }

    void drawTime()
    {
        constexpr int w = 42;
        constexpr int h = 16;
        constexpr int x = 320 - w - 5;
        constexpr int y = 240 - h - 5;

        auto time = UserTime::get();
        String hour = String(time.tm_hour);
        String minute = String(time.tm_min);
        if (minute.length() < 2)
            minute = "0" + minute;
        if (hour.length() < 2)
            hour = "0" + hour;

        String timeStr = hour + ":" + minute;

        Screen::tft.setTextSize(1);
        Screen::tft.setTextColor(TFT_WHITE);
        Screen::tft.fillRoundRect(x, y, w, h, 4, RGB(30, 144, 255));
        Screen::tft.setCursor(x + 6, y + 4);
        Screen::tft.print(timeStr);
        Screen::tft.setTextColor(TFT_BLACK);
    }

} // namespace Windows
