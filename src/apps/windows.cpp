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

        if (touch.clicked)
        {
            Screen::tft.fillScreen(TFT_WHITE);
        }

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

            // drag
            if (state == MouseState::Held && w.dragArea().isIn(pos))
            {
                Vec proposedOff = w.off + move;

                // Collision detection
                bool collides = false;
                constexpr int margin = 3;

                Rect nextRect = Rect{Vec{-1, -13} + proposedOff, w.size + Vec{12 + 2, 13}};

                for (size_t i = 0; i < apps.size() - 1; ++i)
                {
                    const Window &otherWin = *apps[i];
                    Rect otherRect = Rect{otherWin.off + Vec{-1, -13}, otherWin.size + Vec{12 + 2, 13}};

                    if (nextRect.intersects(otherRect))
                    {
                        collides = true;
                        break;
                    }
                }

                if (!collides)
                    w.off = proposedOff;
            }

            // resize
            if (state == MouseState::Held && w.resizeArea().isIn(pos))
            {
                w.size.x = constrain(w.size.x + move.x, Window::minSize.x, Window::maxSize.x);
                w.size.y = constrain(w.size.y + move.y, Window::minSize.y, Window::maxSize.y);
                w.resizeSprite();
            }

            // close
            if (state == MouseState::Down && w.closeBtn().isIn(pos))
            {
                removeAt((int)apps.size() - 1);
            }
        }
        else
        {
            for (auto &p : apps)
            {
                Window &w = *p;
                w.off += move;
            }
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

    void drawTitleBar(Window &w)
    {
        auto d = w.dragArea();
        auto c = w.closeBtn();

        Screen::tft.drawRect(
            w.off.x - 1, w.off.y - Window::titleBarHeight - 1,
            w.size.x + 2 + 12, w.size.y + Window::titleBarHeight + 2,
            TFT_BLACK);

        Screen::tft.fillRect(
            d.pos.x, d.pos.y,
            d.dimensions.x - Window::closeBtnSize, d.dimensions.y,
            RGB(220, 220, 250));

        Screen::tft.setTextSize(1);
        Screen::tft.setCursor(d.pos.x + 2, d.pos.y + 2);
        int maxC = (w.size.x - 20) / 6;
        for (int i = 0; i < std::min((int)w.name.length(), maxC); ++i)
            Screen::tft.print(w.name[i]);

        Screen::tft.fillRect(c.pos.x, c.pos.y, c.dimensions.x, c.dimensions.y, RGB(255, 150, 150));
        Screen::tft.setCursor(c.pos.x + 4, c.pos.y + 2);
        Screen::tft.print("X");

        Screen::tft.setTextSize(2);
    }

    void drawContent(Window &w)
    {
        w.sprite.fillSprite(TFT_BLACK);
        w.sprite.setTextColor(TFT_WHITE);
        w.sprite.drawString("HELLO", 10, 10, 2);
        w.sprite.pushSprite(w.off.x, w.off.y);
    }

    void drawResizeBox(Window &w)
    {
        auto r = w.resizeArea();
        Screen::tft.fillRect(r.pos.x, r.pos.y, r.dimensions.x, r.dimensions.y, RGB(180, 180, 255));
    }

} // namespace Windows
