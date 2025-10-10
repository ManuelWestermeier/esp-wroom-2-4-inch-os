// wifi_manager.cpp
#include <Arduino.h>
#include <WiFi.h>

#include "../screen/index.hpp"
#include "../utils/hex.hpp"
#include "../styles/global.hpp"
#include "../fs/index.hpp"
#include "../fs/enc-fs.hpp"
#include "../wifi/index.hpp"
#include "../io/read-string.hpp"

#include <vector>

// visual constants
#define BTN_RADIUS 8
#define LIST_ITEM_HEIGHT 44
#define LIST_MARGIN 10
#define ITEM_BUTTON_W 84
#define ITEM_BUTTON_H 28
#define BTN_AREA_HEIGHT 64
#define ARROW_AREA_W 28

struct WiFiItem
{
    String ssid;
    bool secured = false;
    bool known = false;
    String source; // "scan" | "public" | "private"
};

static std::vector<WiFiItem> wifiList;
static int selectedIndex = 0;
static int viewOffset = 0;

// UI state
enum WiFiUIState
{
    WIFI_IDLE,
    WIFI_SCANNING,
    WIFI_CONNECTING,
    WIFI_CONNECTED,
    WIFI_FAILED
};

static WiFiUIState uiState = WIFI_IDLE;

// --- Simple viewport implementation (used to 'clip' text & drawing) ---
struct Viewport
{
    int x;
    int y;
    int w;
    int h;
};

static Viewport viewport = {0, 0, 0, 0};

static void setViewport(int x, int y, int w, int h)
{
    viewport.x = x;
    viewport.y = y;
    viewport.w = w;
    viewport.h = h;
}

static bool rectIntersectsViewport(int x, int y, int w, int h)
{
    if (viewport.w == 0 || viewport.h == 0)
        return true; // no viewport set => draw everything
    int x2 = x + w;
    int y2 = y + h;
    int vx2 = viewport.x + viewport.w;
    int vy2 = viewport.y + viewport.h;
    return !(x2 < viewport.x || x > vx2 || y2 < viewport.y || y > vy2);
}

// approximate text width in pixels for given text and size (safe portable fallback)
static int approxTextWidth(const String &s, int textSize)
{
    return (int)s.length() * (6 * textSize);
}

// truncate string to fit pixel width, append ellipsis
static String truncateToWidth(const String &s, int textSize, int maxPixels)
{
    if ((int)approxTextWidth(s, textSize) <= maxPixels)
        return s;
    int ellipsisPixels = 3 * (6 * textSize);
    int allowed = maxPixels - ellipsisPixels;
    if (allowed <= 0)
        return String("...");
    int chars = allowed / (6 * textSize);
    if (chars <= 0)
        return String("...");
    String out = s.substring(0, chars);
    out += "...";
    return out;
}

// --- helpers: drawing primitives (reuse style macros) ---
static void drawButtonRect(int x, int y, int w, int h, const String &label, uint16_t bgColor, uint16_t textColor, int textSize = 1)
{
    if (!rectIntersectsViewport(x, y, w, h))
        return;
    Screen::tft.fillRoundRect(x, y, w, h, BTN_RADIUS, bgColor);

    Screen::tft.setTextSize(textSize);
    Screen::tft.setTextColor(textColor);

    int padding = 6;
    int maxLabelPixels = w - padding * 2;
    String txt = truncateToWidth(label, textSize, maxLabelPixels);

    int txtW = approxTextWidth(txt, textSize);
    int txtH = 8 * textSize;
    int tx = x + (w / 2) - (txtW / 2);
    int ty = y + (h / 2) - (txtH / 2) + 2;

    Screen::tft.drawString(txt, tx, ty);
}

// small status overlay in middle
static void showStatusOverlay(const String &title, const String &msg, uint16_t bgColor, uint16_t textColor, unsigned long msDelay = 0)
{
    Screen::tft.fillScreen(bgColor);
    Screen::tft.setTextSize(2);
    Screen::tft.setTextColor(textColor);

    int tW = approxTextWidth(title, 2);
    Screen::tft.drawString(title, (Screen::tft.width() / 2) - (tW / 2), (Screen::tft.height() / 2) - 14);

    if (msg.length())
    {
        Screen::tft.setTextSize(1);
        int mW = approxTextWidth(msg, 1);
        Screen::tft.drawString(msg, (Screen::tft.width() / 2) - (mW / 2), (Screen::tft.height() / 2) + 12);
    }
    if (msDelay)
        delay(msDelay);
}

// spinner small overlay at top while connecting
static void drawConnectingTop(const String &ssid, int spinner)
{
    Screen::tft.fillRect(0, 0, Screen::tft.width(), 40, BG);
    Screen::tft.setTextSize(1);
    Screen::tft.setTextColor(TEXT);
    String msg = "Connecting: " + ssid;
    int avail = Screen::tft.width() - 40;
    String label = truncateToWidth(msg, 1, avail);
    Screen::tft.drawString(label, 12, 8);

    const char *spin = "|/-\\";
    String s = String(spin[spinner % 4]);
    int sw = approxTextWidth(s, 1);
    Screen::tft.drawString(s, Screen::tft.width() - 12 - sw / 2, 8);
}

// draw arrows for scrolling (top-right area)
static void drawScrollArrows(bool canUp, bool canDown)
{
    int ax = Screen::tft.width() - ARROW_AREA_W;
    int ay = 10;
    Screen::tft.fillRoundRect(ax, ay, ARROW_AREA_W - 4, 18, 4, canUp ? ACCENT : ACCENT2);
    Screen::tft.setTextColor(TEXT);
    Screen::tft.setTextSize(1);
    String up = "^";
    int upW = approxTextWidth(up, 1);
    Screen::tft.drawString(up, ax + ((ARROW_AREA_W - 4) / 2) - (upW / 2), ay + 6);

    Screen::tft.fillRoundRect(ax, ay + 22, ARROW_AREA_W - 4, 18, 4, canDown ? ACCENT : ACCENT2);
    String down = "v";
    int downW = approxTextWidth(down, 1);
    Screen::tft.drawString(down, ax + ((ARROW_AREA_W - 4) / 2) - (downW / 2), ay + 22 + 6);
}

// compute visible capacity
static int calcMaxVisible()
{
    int yStart = 10;
    int available = Screen::tft.height() - yStart - BTN_AREA_HEIGHT;
    return available / LIST_ITEM_HEIGHT;
}

// clamp view offset
static void clampViewOffset()
{
    int maxVisible = calcMaxVisible();
    int maxOffset = 0;
    if ((int)wifiList.size() > maxVisible)
        maxOffset = (int)wifiList.size() - maxVisible;
    if (viewOffset < 0)
        viewOffset = 0;
    if (viewOffset > maxOffset)
        viewOffset = maxOffset;

    if (selectedIndex < viewOffset)
        selectedIndex = viewOffset;
    if (selectedIndex >= viewOffset + maxVisible)
        selectedIndex = viewOffset + maxVisible - 1;
    if (selectedIndex < 0 && !wifiList.empty())
        selectedIndex = 0;
}

// --- storage loader ---
// we only load stored networks if no scan results are found
static void loadKnownWiFis()
{
    auto pubFiles = SD_FS::readDir("/public/wifi");
    for (auto &f : pubFiles)
    {
        if (!f.isDirectory())
        {
            WiFiItem item;
            String name = String(f.name());
            if (name.endsWith(".wifi"))
                name = name.substring(0, name.length() - 5);
            item.ssid = fromHex(name);
            item.secured = true;
            item.known = true;
            item.source = "public";
            wifiList.push_back(item);
        }
    }

    auto privFiles = ENC_FS::readDir({"wifi"});
    for (auto &n : privFiles)
    {
        String name = String(n);
        if (name.endsWith(".wifi"))
            name = name.substring(0, name.length() - 5);
        WiFiItem item;
        item.ssid = fromHex(name);
        item.secured = true;
        item.known = true;
        item.source = "private";
        wifiList.push_back(item);
    }
}

// --- scanning ---
static void drawScanningScreen(int spinner)
{
    Screen::tft.fillScreen(BG);
    Screen::tft.setTextColor(TEXT);
    Screen::tft.setTextSize(2);
    String s = "Scanning...";
    Screen::tft.drawString(s, Screen::tft.width() / 2 - approxTextWidth(s, 2) / 2, 24);
    const char *spin = "|/-\\";
    Screen::tft.setTextSize(1);
    Screen::tft.drawString(String(spin[spinner % 4]), Screen::tft.width() / 2 - 2, 56);
}

// perform scan and populate wifiList
static void scanWiFisAndShow()
{
    uiState = WIFI_SCANNING;
    wifiList.clear();
    selectedIndex = 0;
    viewOffset = 0;

    int spinner = 0;
    drawScanningScreen(spinner);
    delay(150);

    WiFi.scanDelete();
    int n = WiFi.scanNetworks();
    if (n < 0)
        n = 0;

    for (int i = 0; i < n; i++)
    {
        String ssid = WiFi.SSID(i);
        bool secured = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;

        bool already = false;
        for (auto &it : wifiList)
        {
            if (it.ssid == ssid)
            {
                it.secured = secured;
                already = true;
                break;
            }
        }
        if (already)
            continue;

        WiFiItem item;
        item.ssid = ssid;
        item.secured = secured;
        item.known = false;
        item.source = "scan";
        wifiList.push_back(item);
    }

    // fallback: if no scan results, show stored networks
    if ((int)wifiList.size() == 0)
    {
        loadKnownWiFis();
    }

    uiState = WIFI_IDLE;
    clampViewOffset();
}

// --- draw list UI ---
static void drawWiFiList()
{
    Screen::tft.fillRect(0, 0, Screen::tft.width(), Screen::tft.height() - BTN_AREA_HEIGHT, BG);

    int yStart = 10;
    int maxVisible = calcMaxVisible();
    int w = Screen::tft.width() - (LIST_MARGIN * 2) - ARROW_AREA_W;
    int buttonW = ITEM_BUTTON_W;
    int labelW = w - buttonW - 12;

    setViewport(LIST_MARGIN, 0, w, Screen::tft.height() - BTN_AREA_HEIGHT);

    Screen::tft.setTextSize(1);
    for (int i = 0; i < maxVisible && i + viewOffset < (int)wifiList.size(); i++)
    {
        int idx = i + viewOffset;
        int y = yStart + i * LIST_ITEM_HEIGHT;
        WiFiItem &item = wifiList[idx];

        bool isSelected = (idx == selectedIndex);
        uint16_t rowBg = isSelected ? PRIMARY : BG;
        if (rectIntersectsViewport(LIST_MARGIN, y, w, LIST_ITEM_HEIGHT - 6))
            Screen::tft.fillRoundRect(LIST_MARGIN, y, w, LIST_ITEM_HEIGHT - 6, 6, rowBg);

        String label = item.ssid;
        if (item.known)
            label += " (saved)";
        if (item.secured)
            label += " \xE2\x9C\x94"; // checkmark

        int textSize = 1;
        int availablePixels = labelW - 12;
        String labelToDraw = truncateToWidth(label, textSize, availablePixels);

        Screen::tft.setTextColor(TEXT);
        Screen::tft.setTextSize(textSize);
        int tx = LIST_MARGIN + 8;
        int ty = y + (LIST_ITEM_HEIGHT / 2) - 6;
        Screen::tft.drawString(labelToDraw, tx, ty);

        int btnX = LIST_MARGIN + w - buttonW - 8;
        int btnY = y + (LIST_ITEM_HEIGHT - ITEM_BUTTON_H) / 2;
        String bLabel;
        uint16_t bCol = ACCENT;
        if (item.known)
        {
            bLabel = "Connect";
            bCol = PRIMARY;
        }
        else if (!item.secured)
        {
            bLabel = "Open";
            bCol = ACCENT;
        }
        else
        {
            bLabel = "Pass";
            bCol = ACCENT2;
        }
        drawButtonRect(btnX, btnY - 2, buttonW, ITEM_BUTTON_H, bLabel, bCol, TEXT, 1);
    }

    setViewport(0, 0, 0, 0);

    bool canUp = viewOffset > 0;
    bool canDown = (viewOffset + maxVisible) < (int)wifiList.size();
    drawScrollArrows(canUp, canDown);

    int btnY = Screen::tft.height() - BTN_AREA_HEIGHT + 12;
    int thirdW = (Screen::tft.width() - (LIST_MARGIN * 2)) / 3;
    drawButtonRect(LIST_MARGIN, btnY, thirdW - 8, 40, "Connect", PRIMARY, TEXT);
    drawButtonRect(LIST_MARGIN + thirdW, btnY, thirdW - 8, 40, "Rescan", ACCENT2, TEXT);
    drawButtonRect(LIST_MARGIN + 2 * thirdW, btnY, thirdW, 40, "Cancel/OK", DANGER, TEXT);
}

// --- connection routine with visual feedback ---
static bool tryConnectWithPassOverlay(const String &ssid, const String &pass, unsigned long timeoutMs = 8000)
{
    WiFi.disconnect(true);
    delay(120);

    if (pass.length() == 0)
        WiFi.begin(ssid.c_str());
    else
        WiFi.begin(ssid.c_str(), pass.c_str());

    unsigned long start = millis();
    int spinner = 0;
    while (millis() - start < timeoutMs)
    {
        if (WiFi.status() == WL_CONNECTED)
        {
            return true;
        }
        drawConnectingTop(ssid, spinner++);
        delay(200);
    }
    return (WiFi.status() == WL_CONNECTED);
}

// prompt the user to save credentials (only when non-empty pass)
static void promptStoreOptions(const String &ssid, const String &pass)
{
    if (pass.length() == 0)
    {
        showStatusOverlay("Connected", "Open network - nothing to store", BG, TEXT, 900);
        return;
    }

    Screen::tft.fillScreen(BG);
    Screen::tft.setTextColor(TEXT);
    Screen::tft.setTextSize(1);
    String header = "Connected to " + ssid;
    Screen::tft.drawString(header, Screen::tft.width() / 2 - approxTextWidth(header, 1) / 2, 30);
    String q = "Save credentials?";
    Screen::tft.drawString(q, Screen::tft.width() / 2 - approxTextWidth(q, 1) / 2, 50);

    int w = Screen::tft.width() - 40;
    int btnH = 36;
    int gap = 8;
    int x = 20;
    int y = 80;

    drawButtonRect(x, y, (w / 2) - gap, btnH, "Public", ACCENT2, TEXT);
    drawButtonRect(x + (w / 2) + gap, y, (w / 2) - gap, btnH, "Private", ACCENT, TEXT);
    drawButtonRect(x, y + btnH + gap, (w / 2) - gap, btnH, "Both", PRIMARY, TEXT);
    drawButtonRect(x + (w / 2) + gap, y + btnH + gap, (w / 2) - gap, btnH, "Skip", DANGER, TEXT);

    while (true)
    {
        auto t = Screen::getTouchPos();
        if (t.clicked)
        {
            int tx = t.x;
            int ty = t.y;
            if (ty >= y && ty <= y + btnH)
            {
                if (tx >= x && tx <= x + (w / 2) - gap)
                {
                    SD_FS::writeFile("/public/wifi/" + toHex(ssid) + ".wifi", pass);
                    showStatusOverlay("Saved", "Saved as public", BG, TEXT, 900);
                    break;
                }
                else if (tx >= x + (w / 2) + gap && tx <= x + w)
                {
                    ENC_FS::writeFileString({"wifi", toHex(ssid) + ".wifi"}, pass);
                    showStatusOverlay("Saved", "Saved as private", BG, TEXT, 900);
                    break;
                }
            }
            if (ty >= y + btnH + gap && ty <= y + 2 * btnH + gap)
            {
                if (tx >= x && tx <= x + (w / 2) - gap)
                {
                    SD_FS::writeFile("/public/wifi/" + toHex(ssid) + ".wifi", pass);
                    ENC_FS::writeFileString({"wifi", toHex(ssid) + ".wifi"}, pass);
                    showStatusOverlay("Saved", "Saved both", BG, TEXT, 900);
                    break;
                }
                else if (tx >= x + (w / 2) + gap && tx <= x + w)
                {
                    showStatusOverlay("OK", "Not saved", BG, TEXT, 600);
                    break;
                }
            }
        }
        delay(20);
    }
}

// Show a 3-button dialog when connect fails: returns 0=Don't connect, 1=Enter new password, 2=Retry
static int showConnectFailOptions(const String &ssid)
{
    Screen::tft.fillScreen(DANGER);
    Screen::tft.setTextColor(TEXT);
    Screen::tft.setTextSize(1);
    String failMsg = "Failed to connect to " + ssid;
    Screen::tft.drawString(failMsg, Screen::tft.width() / 2 - approxTextWidth(failMsg, 1) / 2, 40);
    String prompt = "Choose:";
    Screen::tft.drawString(prompt, Screen::tft.width() / 2 - approxTextWidth(prompt, 1) / 2, 60);

    int w = Screen::tft.width() - 40;
    int btnH = 44;
    int gap = 12;
    int x = 20;
    int y = 100;

    // layout three equal buttons
    int third = (w - (2 * gap)) / 3;
    drawButtonRect(x, y, third, btnH, "Don't connect", PRIMARY, TEXT);
    drawButtonRect(x + third + gap, y, third, btnH, "Enter password", ACCENT, TEXT);
    drawButtonRect(x + 2 * (third + gap), y, third, btnH, "Retry", ACCENT2, TEXT);

    while (true)
    {
        auto t = Screen::getTouchPos();
        if (t.clicked)
        {
            int tx = t.x;
            int ty = t.y;
            if (ty >= y && ty <= y + btnH)
            {
                if (tx >= x && tx < x + third)
                    return 0;
                else if (tx >= x + third + gap && tx < x + 2 * third + gap)
                    return 1;
                else if (tx >= x + 2 * (third + gap) && tx <= x + 3 * third + 2 * gap)
                    return 2;
            }
        }
        delay(20);
    }
}

// connect to an index (handles private/public stored, open, or prompt for password)
static void connectToIndex(int idx)
{
    if (idx < 0 || idx >= (int)wifiList.size())
        return;

    WiFiItem &item = wifiList[idx];
    String ssid = item.ssid;

    // Helper lambda to attempt connection with a pass and handle post-success saving
    auto attemptWithPass = [&](const String &pass) -> bool
    {
        showStatusOverlay("Connecting", "Please wait...", BG, TEXT, 200);
        if (tryConnectWithPassOverlay(ssid, pass, 8000))
        {
            showStatusOverlay("Connected", ssid, BG, TEXT, 700);
            if (pass.length() > 0)
                promptStoreOptions(ssid, pass); // allow user to save only if pass non-empty
            return true;
        }
        return false;
    };

    // 1) try private stored
    ENC_FS::Path privPath = {"wifi", toHex(ssid) + ".wifi"};
    if (ENC_FS::exists(privPath))
    {
        String p = ENC_FS::readFileString(privPath);
        if (attemptWithPass(p))
            return;

        // failed: ask user what to do
        int choice = showConnectFailOptions(ssid);
        if (choice == 0) // don't connect
            return;
        if (choice == 2) // retry with same password
        {
            if (attemptWithPass(p))
                return;
            else
                return; // still failed — give up
        }
        if (choice == 1) // enter new password
        {
            String newPass = readString("New password for " + ssid + ":", "");
            if (newPass.length() == 0)
                return; // cancelled
            if (attemptWithPass(newPass))
                return;
            else
                return;
        }
    }

    // 2) try public stored
    String pubPath = "/public/wifi/" + toHex(ssid) + ".wifi";
    if (SD_FS::exists(pubPath))
    {
        String p = SD_FS::readFile(pubPath);
        if (attemptWithPass(p))
            return;

        int choice = showConnectFailOptions(ssid);
        if (choice == 0)
            return;
        if (choice == 2)
        {
            if (attemptWithPass(p))
                return;
            else
                return;
        }
        if (choice == 1)
        {
            String newPass = readString("New password for " + ssid + ":", "");
            if (newPass.length() == 0)
                return;
            if (attemptWithPass(newPass))
                return;
            else
                return;
        }
    }

    // 3) open network
    if (!item.secured)
    {
        if (attemptWithPass(""))
            return;

        // failed open (rare): let user choose to enter password or cancel or retry
        int choice = showConnectFailOptions(ssid);
        if (choice == 0)
            return;
        if (choice == 2)
        {
            if (attemptWithPass(""))
                return;
            else
                return;
        }
        if (choice == 1)
        {
            String newPass = readString("Password for " + ssid + ":", "");
            if (newPass.length() == 0)
                return;
            if (attemptWithPass(newPass))
                return;
            else
                return;
        }
    }

    // 4) secured & no stored credentials -> ask user for password
    if (item.secured)
    {
        String entered = readString("Password for " + ssid + ":", "");
        if (entered == "")
            return; // cancelled

        if (attemptWithPass(entered))
            return;

        // failed: show 3-way options (Don't connect / Enter new / Retry)
        while (true)
        {
            int choice = showConnectFailOptions(ssid);
            if (choice == 0) // don't connect
                return;
            else if (choice == 2) // retry with same entered password
            {
                if (attemptWithPass(entered))
                    return;
                else
                    continue; // show options again
            }
            else if (choice == 1) // enter a different password
            {
                String newPass = readString("New password for " + ssid + ":", "");
                if (newPass.length() == 0)
                    return;
                if (attemptWithPass(newPass))
                    return;
                else
                    continue; // show options again
            }
        }
    }
}

// --- UI update and touch handling ---
static bool updateWiFiManager()
{
    auto touch = Screen::getTouchPos();

    // only scroll when the user RELEASES (clicked == true) and movement was significant
    if (touch.clicked && abs(touch.move.y) > 4)
    {
        int deltaRows = (touch.move.y) / LIST_ITEM_HEIGHT;
        viewOffset -= deltaRows;
        clampViewOffset();
        drawWiFiList();
        return true; // treat this release as handled (do not also treat as a row click)
    }

    // if no click (release) and no pressed gesture to handle, nothing else to do
    if (!touch.clicked)
        return true;

    int width = Screen::tft.width();
    int height = Screen::tft.height();
    int btnAreaY = height - BTN_AREA_HEIGHT + 12;

    // bottom buttons
    if (touch.y >= btnAreaY && touch.y <= btnAreaY + 40)
    {
        int thirdW = (width - (LIST_MARGIN * 2)) / 3;
        if (touch.x >= LIST_MARGIN && touch.x <= LIST_MARGIN + thirdW - 8)
        {
            // connect selected
            connectToIndex(selectedIndex);
            drawWiFiList();
            return true;
        }
        else if (touch.x >= LIST_MARGIN + thirdW && touch.x <= LIST_MARGIN + 2 * thirdW - 8)
        {
            // rescan
            showStatusOverlay("Scanning", "Please wait...", BG, TEXT, 200);
            scanWiFisAndShow();
            drawWiFiList();
            return true;
        }
        else
        {
            // cancel/exit
            return false;
        }
    }

    // check arrow area (top right)
    int ax = width - ARROW_AREA_W;
    if (touch.x >= ax)
    {
        int ay = 10;
        if (touch.y >= ay && touch.y <= ay + 18)
        {
            viewOffset -= 1;
            clampViewOffset();
            drawWiFiList();
            return true;
        }
        else if (touch.y >= ay + 22 && touch.y <= ay + 40)
        {
            viewOffset += 1;
            clampViewOffset();
            drawWiFiList();
            return true;
        }
    }

    // select item or item button
    int yStart = 10;
    int maxVisible = calcMaxVisible();
    int wArea = width - (LIST_MARGIN * 2) - ARROW_AREA_W;
    int buttonW = ITEM_BUTTON_W;

    int clickedIndex = (touch.y - yStart) / LIST_ITEM_HEIGHT + viewOffset;
    if (clickedIndex >= 0 && clickedIndex < (int)wifiList.size())
    {
        int rowY = yStart + (clickedIndex - viewOffset) * LIST_ITEM_HEIGHT;
        int btnX = LIST_MARGIN + wArea - buttonW - 8;
        int btnY = rowY + (LIST_ITEM_HEIGHT - ITEM_BUTTON_H) / 2;
        if (touch.x >= btnX && touch.x <= btnX + buttonW && touch.y >= btnY && touch.y <= btnY + ITEM_BUTTON_H)
        {
            WiFiItem &item = wifiList[clickedIndex];
            if (item.known)
            {
                connectToIndex(clickedIndex);
                drawWiFiList();
                return true;
            }
            else if (!item.secured)
            {
                showStatusOverlay("Connecting", "Open network...", BG, TEXT, 200);
                if (tryConnectWithPassOverlay(item.ssid, "", 5000))
                {
                    showStatusOverlay("Connected", item.ssid, BG, TEXT, 700);
                }
                else
                {
                    showStatusOverlay("Failed", "Could not connect to open network", DANGER, TEXT, 900);
                }
                drawWiFiList();
                return true;
            }
            else
            {
                String entered = readString("Password for " + item.ssid + ":", "");
                if (entered.length() == 0)
                {
                    drawWiFiList();
                    return true;
                }
                showStatusOverlay("Connecting", "Using entered password...", BG, TEXT, 150);
                if (tryConnectWithPassOverlay(item.ssid, entered, 8000))
                {
                    showStatusOverlay("Connected", item.ssid, BG, TEXT, 700);
                    promptStoreOptions(item.ssid, entered);
                }
                else
                {
                    // on failure, present the 3-way dialog and act accordingly
                    while (true)
                    {
                        int choice = showConnectFailOptions(item.ssid);
                        if (choice == 0)
                            break;            // don't connect
                        else if (choice == 2) // retry with same entered password
                        {
                            if (tryConnectWithPassOverlay(item.ssid, entered, 8000))
                            {
                                showStatusOverlay("Connected", item.ssid, BG, TEXT, 700);
                                promptStoreOptions(item.ssid, entered);
                                break;
                            }
                        }
                        else if (choice == 1) // enter new password
                        {
                            String newPass = readString("New password for " + item.ssid + ":", "");
                            if (newPass.length() == 0)
                                break;
                            if (tryConnectWithPassOverlay(item.ssid, newPass, 8000))
                            {
                                showStatusOverlay("Connected", item.ssid, BG, TEXT, 700);
                                promptStoreOptions(item.ssid, newPass);
                                break;
                            }
                        }
                    }
                }
                drawWiFiList();
                return true;
            }
        }
        else
        {
            selectedIndex = clickedIndex;
            clampViewOffset();
            drawWiFiList();
            return true;
        }
    }

    return true;
}

// --- main entrypoint to open the manager ---
void openWifiManager()
{
    scanWiFisAndShow();
    drawWiFiList();

    while (true)
    {
        if (!updateWiFiManager())
            break;
        delay(20);
    }

    Screen::tft.fillScreen(BG);
}
