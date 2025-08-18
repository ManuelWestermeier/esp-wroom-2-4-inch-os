#include "windows.hpp" // for Vec, MouseState, etc.

// extern void executeApplication(const std::vector<String> &args);

void Windows::drawMenu(Vec pos, Vec move, MouseState state)
{
    using Screen::tft;
    Rect screen = {{0, 0}, {320, 240}};
    static int scrollYOff = 10;
    int itemHeight = 25;
    int itemWidth = 250;
    Rect topSelect = {{10, 0}, {320, 50}};
    Rect programsView = {{programsView.pos.x, topSelect.dimensions.y + 10}, {itemWidth, screen.dimensions.y - topSelect.dimensions.y}};

    auto apps = SD_FS::readDir("/public/programs");

    tft.fillRect(topSelect.pos.x, topSelect.pos.y, topSelect.dimensions.x, topSelect.dimensions.y, RGB(255, 240, 255));

    tft.setTextSize(2);
    int i = 0;
    for (const auto app : apps)
    {
        i++;
        Rect appRect = {{10, scrollYOff + i * itemHeight}, {itemWidth, itemHeight}};

        if (appRect.intersects(topSelect))
            continue;
        if (!appRect.intersects(screen))
            continue;

        tft.fillRoundRect(appRect.pos.x, appRect.pos.y, appRect.dimensions.x, appRect.dimensions.y - 5, 5, RGB(255, 240, 255));

        tft.setCursor(appRect.pos.x + 30, appRect.pos.y + 3);
        // String namePath = String(app.path()) + "/name.txt";
        // tft.print(SD_FS::readFile(namePath));
        tft.print(app.name());

        String iconPath = String(app.path()) + "/icon-20x20.raw";
        // Screen::drawImageFromSD(iconPath.c_str(), appRect.pos.x + 5, appRect.pos.y);

        if (appRect.isIn(pos) && state == MouseState::Down)
        {
            executeApplication({String(app.path()) + "/", "Arg1", "Hi"});
            Windows::isRendering = true;
        }
    }

    if (programsView.isIn(pos) && state == MouseState::Held)
    {
        scrollYOff = max(0, scrollYOff + move.y);
    }

    drawTime();
    delay(10);
}
