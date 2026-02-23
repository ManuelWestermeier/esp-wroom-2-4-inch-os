#include "windows.hpp" // for Vec, MouseState, etc.

#include <Arduino.h>
#include <vector>

#include "../sys-apps/wifi-menager.hpp"
#include "../sys-apps/tft-file-manager.hpp"
#include "../sys-apps/app-menager.hpp"
#include "../sys-apps/settings.hpp"
#include "../sys-apps/browser.hpp"

#include "../icons/index.hpp"

extern bool executeApplication(const std::vector<String> &args);

struct AppRenderData
{
    String name;
    String id; // new: stores appId from appId/id.txt
    ENC_FS::Path path;
    uint16_t icon[20 * 20];
    bool hasIcon = false;

    bool loadMetaData()
    {
        // load name
        auto namePath = path;
        namePath.push_back("name.txt");
        name = ENC_FS::readFileString(namePath).substring(0, 16);
        name.replace("\n", "");
        name.trim();

        // load id from appId/id.txt (if present)
        id = "";
        auto idPath = path;
        idPath.push_back("appId");
        idPath.push_back("id.txt");
        if (ENC_FS::exists(idPath))
        {
            id = ENC_FS::readFileString(idPath);
            id.replace("\n", "");
            id.trim();
        }
        else
        {
            // fallback: try id.txt at root of app folder
            auto idPath2 = path;
            idPath2.push_back("id.txt");
            if (ENC_FS::exists(idPath2))
            {
                id = ENC_FS::readFileString(idPath2);
                id.replace("\n", "");
                id.trim();
            }
        }

        auto iconPath = path;
        iconPath.push_back("icon-20x20.raw");
        return loadIcon(iconPath);
    }

    bool loadIcon(const ENC_FS::Path &filename, uint16_t bgColor = PRIMARY, uint8_t radius = 5)
    {
        if (!ENC_FS::exists(filename))
            return false;
        if (ENC_FS::getFileSize(filename) != 804)
        {
            Serial.println("Icon size mismatch (expected 804): " + ENC_FS::path2Str(filename) + " " + ENC_FS::getFileSize(filename));
            return false;
        }

        auto data = ENC_FS::readFile(filename, 4, 804);
        if (data.size() != 800)
            return false;

        int bI = 0;
        for (int j = 0; j < 20; j++)
        {
            for (int i = 0; i < 20; i++)
            {
                int lo = data.at(bI++);
                int hi = data.at(bI++);
                icon[j * 20 + i] = (hi < 0 || lo < 0)
                                       ? bgColor
                                       : (uint16_t)((hi << 8) | lo);
            }
        }
        hasIcon = true;
        applyRoundMask(bgColor, radius);
        return true;
    }

    void applyRoundMask(uint16_t bgColor = PRIMARY, uint8_t radius = 5)
    {
        if (!hasIcon)
            return;
        if (radius > 10)
            radius = 10;

        for (int j = 0; j < 20; ++j)
        {
            for (int i = 0; i < 20; ++i)
            {
                bool visible = true;

                auto checkCorner = [&](int dx, int dy)
                {
                    return dx * dx + dy * dy >= radius * radius;
                };

                if (i < radius && j < radius)
                { // top-left
                    if (checkCorner(radius - 1 - i, radius - 1 - j))
                        visible = false;
                }
                else if (i >= 20 - radius && j < radius)
                { // top-right
                    if (checkCorner(i - (20 - radius), radius - 1 - j))
                        visible = false;
                }
                else if (i < radius && j >= 20 - radius)
                { // bottom-left
                    if (checkCorner(radius - 1 - i, j - (20 - radius)))
                        visible = false;
                }
                else if (i >= 20 - radius && j >= 20 - radius)
                { // bottom-right
                    if (checkCorner(i - (20 - radius), j - (20 - radius)))
                        visible = false;
                }

                if (!visible)
                    icon[j * 20 + i] = bgColor;
            }
        }
    }

    void drawIcon(int x, int y)
    {
        if (hasIcon)
            Screen::tft.pushImage(x, y, 20, 20, icon);
    }

    bool operator==(const AppRenderData &o) const
    {
        return path == o.path && name == o.name;
    }
};

struct ShortCut
{
    String name;
    String svg;
};

bool updatedAppList = false;

#define SCROLL_OFF_Y_MENU_START 20

static std::vector<ShortCut> shortCuts = {
    {"Settings", SVG::settings},
    {"WiFi", SVG::wifi},
    {"Apps", SVG::apps},
    {"Browser", SVG::browser},
    {"Folders", SVG::folder},
};

// Helper: update the app list (reads metadata & icons)
static void updateAppList(std::vector<AppRenderData> &apps,
                          std::vector<ENC_FS::Path> &lastPaths,
                          bool &appsChanged)
{
    auto entries = ENC_FS::readDir({"programs"});
    std::vector<ENC_FS::Path> newPaths;
    for (String &e : entries)
        newPaths.push_back({"programs", e});

    if (newPaths != lastPaths)
    {
        apps.clear();
        for (auto &p : newPaths)
        {
            AppRenderData app;
            app.path = p;
            if (app.loadMetaData())
            {
                apps.push_back(std::move(app));
            }
            else
            {
                Serial.println("App meta load failed: " + ENC_FS::path2Str(p));
            }
        }
        lastPaths = newPaths;
        appsChanged = true;
    }
}

void Windows::drawMenu(Vec pos, Vec move, MouseState state)
{
    using Screen::tft;

    static int scrollYOff = SCROLL_OFF_Y_MENU_START;
    static int scrollXOff = 0;
    const int itemHeight = 30;
    const int itemWidth = 320 - 20 - 42; // screen width - horizontal padding - time btn

    Rect screen = {{0, 0}, {320, 240}};
    Rect topSelect = {{10, 10}, {300, 60}};
    Rect programsView = {{10, topSelect.pos.y + topSelect.dimensions.y},
                         {itemWidth, screen.dimensions.y - topSelect.dimensions.y - topSelect.pos.y}};

    // position the new scroll button in the right gutter:
    // placed at the right edge, under topSelect, above the time button
    Rect scrollBtnRect = {{screen.dimensions.x - 5 - 42, programsView.pos.y + (programsView.dimensions.y - 46) / 2}, {42, 42}};

    // persistent app list + state
    static std::vector<AppRenderData> apps;
    static std::vector<ENC_FS::Path> lastPaths;
    static unsigned long lastMenuRender = 0;
    static unsigned long lastMenuRenderCall = 0;

    bool appsChanged = false;
    bool needRedraw = false;
    bool topRedraw = false, bottomRedraw = false;

    // periodic directory check (only every 10s)
    if (!updatedAppList)
    {
        updatedAppList = true;
        tft.fillScreen(BG);
        tft.setTextSize(2);
        tft.setTextColor(TEXT);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("Loading Apps...", 160, 120);
        updateAppList(apps, lastPaths, appsChanged);
        tft.fillScreen(BG);
    }

    // determine redraw needs
    if (lastMenuRender == 0 || lastMenuRenderCall == 0 || appsChanged)
        needRedraw = true;
    if (millis() - lastMenuRenderCall > 300)
        needRedraw = true;

    lastMenuRenderCall = millis();
    if (needRedraw)
    {
        // default: redraw both sections; later we refine when interacting
        topRedraw = bottomRedraw = true;
        tft.fillScreen(BG);
    }

    // compute scroll bounds for the app list (clamp scrollYOff between min/max)
    int visibleH = programsView.dimensions.y - 10; // we used +10 when setting viewport
    int totalH = ((int)apps.size() + 2) * itemHeight;
    int minScrollY = min(SCROLL_OFF_Y_MENU_START, visibleH - totalH); // typically negative if content taller than view

    // --- handle scroll gestures ---
    if (Rect{programsView.pos, {programsView.dimensions.x + 60, programsView.dimensions.y}}.isIn(pos) && state == MouseState::Held)
    {
        int newScroll = min(SCROLL_OFF_Y_MENU_START, scrollYOff + move.y);
        if (newScroll != scrollYOff)
        {
            scrollYOff = max(minScrollY, newScroll);
            needRedraw = bottomRedraw = true;
        }
    }

    if (topSelect.isIn(pos) && state == MouseState::Held)
    {
        int newScroll = min(0, scrollXOff + move.x);
        if (newScroll != scrollXOff)
        {
            scrollXOff = newScroll;
            needRedraw = topRedraw = true;
        }
    }

    // handle hold on our new scroll button for continuous scrolling
    if (scrollBtnRect.isIn(pos) && state == MouseState::Held)
    {
        // small continuous step while held
        const int step = 4;
        int halfY = scrollBtnRect.pos.y + (scrollBtnRect.dimensions.y / 2);
        if (pos.y < halfY)
        {
            int newScroll = min(SCROLL_OFF_Y_MENU_START, scrollYOff + step);
            if (newScroll != scrollYOff)
            {
                scrollYOff = max(minScrollY, newScroll);
                needRedraw = bottomRedraw = true;
            }
        }
        else
        {
            int newScroll = max(minScrollY, scrollYOff - step);
            if (newScroll != scrollYOff)
            {
                scrollYOff = newScroll;
                needRedraw = bottomRedraw = true;
            }
        }
    }

    if (topSelect.isIn(pos) && state == MouseState::Held)
    {
        int newScroll = min(0, scrollXOff + move.x);
        if (newScroll != scrollXOff)
        {
            scrollXOff = newScroll;
            needRedraw = topRedraw = true;
        }
    }

    // --- handle clicks (apps + shortcuts + scroll button) ---
    if (state == MouseState::Down)
    {
        if (scrollBtnRect.isIn(pos))
        {
            // Single tap behavior: top half = page/step up, bottom half = page/step down
            int halfY = scrollBtnRect.pos.y + (scrollBtnRect.dimensions.y / 2);
            const int step = itemHeight / 3; // step by one item
            if (pos.y < halfY)
            {
                int newScroll = min(SCROLL_OFF_Y_MENU_START, scrollYOff + step);
                if (newScroll != scrollYOff)
                {
                    scrollYOff = max(minScrollY, newScroll);
                    needRedraw = bottomRedraw = true;
                }
            }
            else
            {
                int newScroll = max(minScrollY, scrollYOff - step);
                if (newScroll != scrollYOff)
                {
                    scrollYOff = newScroll;
                    needRedraw = bottomRedraw = true;
                }
            }
        }
        else if (programsView.isIn(pos))
        {
            int i = 0;
            for (auto &app : apps)
            {
                i++;
                Rect appRect = {{10, (scrollYOff + (i + 1) * itemHeight)}, {itemWidth, itemHeight}};
                if (!appRect.intersects(programsView))
                    continue;

                // define update button area on the right side of the app card
                const int btnW = 60;
                Rect updateRect = {{appRect.pos.x + appRect.dimensions.x - btnW - 5, appRect.pos.y + 5}, {btnW, appRect.dimensions.y - 10}};

                // check update button click first
                if (updateRect.isIn(pos))
                {
                    // If no id present, log and skip
                    if (app.id.length() == 0)
                    {
                        Serial.println("No app id found for update: " + ENC_FS::path2Str(app.path));
                    }
                    else
                    {
                        Screen::tft.fillScreen(BG);
                        Screen::tft.setTextSize(2);
                        Screen::tft.setTextColor(TEXT);
                        Screen::tft.setTextDatum(MC_DATUM);
                        Screen::tft.drawString("Loading Updates...", 160, 120);
                        Screen::tft.setTextSize(1);
                        // extract folder name (last part of the path)
                        String folderName;
                        if (app.path.size() > 0)
                        {
                            folderName = app.path.back();
                        }
                        else
                        {
                            folderName = ENC_FS::path2Str(app.path);
                        }

                        Serial.println("Updating app: id=" + app.id + " folder=" + folderName);

                        // call installer (synchronous assumption)
                        AppManager::installApp(app.id, folderName, true);
                        updatedAppList = false;
                        Screen::tft.fillScreen(BG);
                        Screen::tft.setTextSize(2);
                        Screen::tft.setTextColor(TEXT);
                        Screen::tft.setTextDatum(MC_DATUM);
                        Screen::tft.drawString("Finished Updates...", 160, 120);
                        Screen::tft.setTextSize(1);
                        delay(1000);

                        // refresh metadata/icon for this app only
                        if (app.loadMetaData())
                        {
                            // trigger redraw of only bottom
                            needRedraw = bottomRedraw = true;
                        }
                        else
                        {
                            Serial.println("Failed to reload app metadata after update: " + folderName);
                        }
                    }

                    // consumed click
                    break;
                }

                // otherwise check whole app card click (open app)
                if (appRect.isIn(pos))
                {
                    // refresh metadata on entry to the app (only this app)
                    app.loadMetaData();

                    bool suceed = executeApplication({ENC_FS::path2Str(app.path)});
                    if (!suceed)
                    {
                        Screen::tft.fillScreen(BG);
                        tft.setTextDatum(CC_DATUM);
                        tft.setTextSize(1);
                        tft.setTextColor(TEXT);
                        tft.drawString("You have to colose one current app,", 160, 120 - 20);
                        tft.drawString("bevore opening a new", 160, 120 + 0);
                        tft.drawString("(or internal app error)", 160, 120 + 20);
                        delay(200);
                        while (!Screen::isTouched())
                        {
                            delay(10);
                        }
                        Screen::tft.fillScreen(BG);
                    }
                    Windows::isRendering = true;
                    break;
                }
            }
        }
        else if (topSelect.isIn(pos))
        {
            int scXPos = topSelect.pos.x + 5 + scrollXOff;
            for (const auto &shortCut : shortCuts)
            {
                int h = topSelect.dimensions.y - 10;
                int w = max(h, (int)(shortCut.name.length() * 6 + 10));
                Rect scPos = {{scXPos, topSelect.pos.y + 5}, {w, h}};

                if (scPos.isIn(pos))
                {
                    if (shortCut.name == "Settings")
                    {
                        Serial.println("Settings clicked");
                        return openSettings();
                    }
                    else if (shortCut.name == "Folders")
                    {
                        TFTFileManager::run();
                        return;
                    }
                    else if (shortCut.name == "WiFi")
                    {
                        return openWifiManager();
                    }
                    else if (shortCut.name == "Apps")
                    {
                        appManager();
                        Screen::tft.fillScreen(BG);
                        Screen::tft.setTextSize(2);
                        Screen::tft.setTextColor(TEXT);
                        Screen::tft.setTextDatum(MC_DATUM);
                        Screen::tft.drawString("Finished Updates...", 160, 120);
                        Screen::tft.setTextSize(1);
                        delay(1000);
                        updatedAppList = false;
                        return;
                    }
                    else if (shortCut.name == "Browser")
                    {
                        return openBrowser();
                    }
                    break;
                }
                scXPos += w + 5;
            }
        }
    }

    if (!needRedraw)
        return;

    // ---------- RENDER TOP (SHORTCUTS + SVGs) ----------
    if (topRedraw)
    {
        tft.setTextSize(1);

        // draw rounded container
        tft.fillRoundRect(topSelect.pos.x, topSelect.pos.y, topSelect.dimensions.x, topSelect.dimensions.y, 5, PRIMARY);

        // set viewport for inner area (so subsequent draws are clipped)
        tft.setViewport(topSelect.pos.x, topSelect.pos.y + 5, topSelect.dimensions.x, topSelect.dimensions.y - 5, false);

        int scXPos = topSelect.pos.x + 5 + scrollXOff;
        for (const auto &shortCut : shortCuts)
        {
            int h = topSelect.dimensions.y - 10;
            int w = max(h, (int)(shortCut.name.length() * 6 + 10));
            Rect scPos = {{scXPos, topSelect.pos.y + 5}, {w, h}};

            // background pill for each shortcut
            tft.fillRoundRect(scPos.pos.x, scPos.pos.y, scPos.dimensions.x, scPos.dimensions.y, 3, BG);

            // name (centered vertically)
            tft.drawCentreString(shortCut.name, scPos.pos.x + (w / 2), scPos.pos.y + 5, 1);

            // draw svg if present
            if (shortCut.svg)
            {
                int d = h - 20;
                // compute icon top-left inside scPos
                int iconX = scPos.pos.x + ((w / 2) - (d / 2));
                int iconY = scPos.pos.y + 15;

                drawSVGString(shortCut.svg, iconX, iconY, d, d, TEXT);
            }

            scXPos += w + 5;
        }

        Screen::tft.resetViewport();
    }

    // ---------- RENDER BOTTOM (PROGRAM LIST with icons, clipped & scrolled) ----------
    if (bottomRedraw)
    {
        // viewport offset +10 px top padding so items don't overlap top bar
        tft.setViewport(programsView.pos.x, programsView.pos.y + 10, programsView.dimensions.x, programsView.dimensions.y, false);

        // clear programs area (only inside viewport)
        tft.fillRect(programsView.pos.x, programsView.pos.y + 10, programsView.dimensions.x, programsView.dimensions.y, BG);

        int i = 0;
        for (auto &app : apps)
        {
            tft.setTextSize(2);
            tft.setTextDatum(TL_DATUM);
            i++;
            Rect appRect = {{10, (scrollYOff + (i + 1) * itemHeight)}, {itemWidth, itemHeight}};

            // skip items not visible in the programsView
            if (!appRect.intersects(programsView))
                continue;

            // draw item card
            tft.fillRoundRect(appRect.pos.x, appRect.pos.y, appRect.dimensions.x, appRect.dimensions.y - 5, 5, PRIMARY);

            // draw icon (if available) or placeholder
            if (app.hasIcon)
            {
                app.drawIcon(appRect.pos.x + 5, appRect.pos.y + 3);
            }
            else
            {
                tft.fillRoundRect(appRect.pos.x + 5, appRect.pos.y + 5, 20, 20, 5, PH);
            }

            // app name
            tft.setCursor(appRect.pos.x + 30, appRect.pos.y + 5);
            tft.print(app.name);

            // optionally show id under the name if present (small text)
            if (app.id.length() > 0)
            {
                // draw update button on the right
                const int btnW = 50;
                Rect updateRect = {{appRect.pos.x + appRect.dimensions.x - btnW - 5, appRect.pos.y + 5}, {btnW, appRect.dimensions.y - 20}};
                tft.fillRoundRect(updateRect.pos.x, updateRect.pos.y, updateRect.dimensions.x, updateRect.dimensions.y, 3, PH);
                tft.setTextSize(1);
                tft.setTextDatum(CC_DATUM);
                tft.drawString("Update", updateRect.pos.x + updateRect.dimensions.x / 2, updateRect.pos.y + (updateRect.dimensions.y / 2));
                tft.setTextDatum(CC_DATUM);
                String clippedId = app.id;

                if (clippedId.length() > 10)
                    clippedId = clippedId.substring(clippedId.length() - 10);

                tft.drawString(clippedId,
                               updateRect.pos.x + updateRect.dimensions.x / 2,
                               updateRect.pos.y + (updateRect.dimensions.y / 2) + 10);
            }
        }

        Screen::tft.resetViewport();
    }

    // draw the new scroll button in the right gutter (outside of viewports)
    {
        // draw up triangle (centered in upper half)
        int cx = scrollBtnRect.pos.x + (scrollBtnRect.dimensions.x / 2);
        int upCy = scrollBtnRect.pos.y + (scrollBtnRect.dimensions.y / 4);
        int triHalf = 6;
        // triangle points: (cx, upCy - triHalf) (cx - triHalf, upCy + triHalf) (cx + triHalf, upCy + triHalf)
        tft.fillTriangle(cx, upCy - triHalf,
                         cx - triHalf, upCy + triHalf,
                         cx + triHalf, upCy + triHalf, ACCENT);

        // draw down triangle (centered in lower half)
        int downCy = scrollBtnRect.pos.y + (3 * scrollBtnRect.dimensions.y / 4);
        // triangle points: (cx, downCy + triHalf) (cx - triHalf, downCy - triHalf) (cx + triHalf, downCy - triHalf)
        tft.fillTriangle(cx, downCy + triHalf,
                         cx - triHalf, downCy - triHalf,
                         cx + triHalf, downCy - triHalf, ACCENT);
    }

    drawTime();
    lastMenuRender = millis();
    delay(10);
}