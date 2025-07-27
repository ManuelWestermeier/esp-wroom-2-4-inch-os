#include "windows.hpp"

namespace Windows
{
    std::vector<WindowPtr> apps;

    void add(WindowPtr w)
    {
        apps.push_back(std::move(w));
    }

    void removeAt(int idx)
    {
        // apps[idx]->lastEvent = {MouseEvent::, rel, move};

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
            w.lastEvent = {state, rel, move};

            if (Rect{w.off, w.size}.isIn(pos))
            {
                w.onEvent({state, rel, move});
            }

            // drag
            if (state == MouseState::Held && w.dragArea().isIn(pos))
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
            if (state == MouseState::Held && w.resizeArea().isIn(pos))
            {
                w.size.x = constrain(w.size.x + move.x, Window::minSize.x, Window::maxSize.x);
                w.size.y = constrain(w.size.y + move.y, Window::minSize.y, Window::maxSize.y);
                w.resizeSprite();
                Screen::tft.fillScreen(RGB(245, 245, 255));
            }

            // close
            if (state == MouseState::Down && w.closeBtn().isIn(pos))
            {
                removeAt((int)apps.size() - 1);
                Screen::tft.fillScreen(RGB(245, 245, 255));
            }
        }
        else
        {
            for (auto &p : apps)
            {
                Window &w = *p;
                w.off += move;
            }
            if (move.x != 0 && move.y != 0)
                Screen::tft.fillScreen(RGB(245, 245, 255));
        }

        // render all
        for (auto &p : apps)
        {
            Window &w = *p;
            drawTitleBar(w);
            drawContent(w);
            drawResizeBox(w);
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

        // drag area
        Screen::tft.fillRectHGradient(
            d.pos.x + 12, d.pos.y,
            d.dimensions.x - Window::closeBtnSize, d.dimensions.y,
            RGB(200, 200, 250), RGB(220, 220, 250));

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
        w.sprite.fillSprite(TFT_BLACK);
        w.sprite.setTextColor(TFT_WHITE);
        w.sprite.drawString("HELLO", 10, 10, 2);

        w.rightSprite.fillSprite(TFT_BLACK);

        w.sprite.pushSprite(w.off.x, w.off.y);
        w.rightSprite.pushSprite(w.off.x + w.size.x, w.off.y);
    }

    void drawResizeBox(Window &w)
    {
        auto r = w.resizeArea();
        Screen::tft.fillRect(r.pos.x, r.pos.y, r.dimensions.x, r.dimensions.y, RGB(180, 180, 255));
        drawResizeIcon(r.pos.x, r.pos.y, TFT_BLACK);
    }

} // namespace Windows
