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

// --- helpers: drawing primitives (reuse style macros) ---
static void drawButtonRect(int x, int y, int w, int h, const String &label, uint16_t bgColor, uint16_t textColor)
{
    Screen::tft.fillRoundRect(x, y, w, h, BTN_RADIUS, bgColor);
    Screen::tft.setTextColor(textColor);
    Screen::tft.setTextSize(1);
    // center text inside button
    Screen::tft.drawString(label, x + (w / 2), y + (h / 2));
}

// small status overlay in middle
static void showStatusOverlay(const String &title, const String &msg, uint16_t bgColor, uint16_t textColor, unsigned long msDelay = 0)
{
    Screen::tft.fillScreen(bgColor);
    Screen::tft.setTextSize(2);
    Screen::tft.setTextColor(textColor);
    Screen::tft.drawString(title, Screen::tft.width() / 2, Screen::tft.height() / 2 - 14);
    if (msg.length())
    {
        Screen::tft.setTextSize(1);
        Screen::tft.drawString(msg, Screen::tft.width() / 2, Screen::tft.height() / 2 + 12);
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
    Screen::tft.drawString(msg, 12, 8);
    const char *spin = "|/-\\";
    Screen::tft.drawString(String(spin[spinner % 4]), Screen::tft.width() - 12, 8);
}

// draw arrows for scrolling (top-right area)
static void drawScrollArrows(bool canUp, bool canDown)
{
    int ax = Screen::tft.width() - ARROW_AREA_W;
    int ay = 10;
    // up arrow
    Screen::tft.fillRoundRect(ax, ay, ARROW_AREA_W - 4, 18, 4, canUp ? ACCENT : ACCENT2);
    Screen::tft.setTextColor(TEXT);
    Screen::tft.setTextSize(1);
    Screen::tft.drawString("^", ax + (ARROW_AREA_W - 4) / 2, ay + 9);
    // down arrow
    Screen::tft.fillRoundRect(ax, ay + 22, ARROW_AREA_W - 4, 18, 4, canDown ? ACCENT : ACCENT2);
    Screen::tft.drawString("v", ax + (ARROW_AREA_W - 4) / 2, ay + 22 + 9);
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
}

// --- storage loader ---
static void loadKnownWiFis()
{
    // Append known (public first) so they appear at top of the list
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
    Screen::tft.drawString("Scanning...", Screen::tft.width() / 2, 24);
    const char *spin = "|/-\\";
    Screen::tft.setTextSize(1);
    Screen::tft.drawString(String(spin[spinner % 4]), Screen::tft.width() / 2, 56);
}

// perform scan and populate wifiList (keeps known at top)
static void scanWiFisAndShow()
{
    uiState = WIFI_SCANNING;
    wifiList.clear();
    selectedIndex = 0;
    viewOffset = 0;

    // show quick scanning UI
    int spinner = 0;
    drawScanningScreen(spinner);
    delay(150);

    loadKnownWiFis(); // known networks pre-populated

    WiFi.scanDelete();
    int n = WiFi.scanNetworks();
    // n can be -1 on error
    if (n < 0)
        n = 0;

    for (int i = 0; i < n; i++)
    {
        String ssid = WiFi.SSID(i);
        bool secured = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;

        // skip duplicates
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

    uiState = WIFI_IDLE;
    clampViewOffset();
}

// --- draw list UI ---
static void drawWiFiList()
{
    Screen::tft.fillRect(0, 0, Screen::tft.width(), Screen::tft.height() - BTN_AREA_HEIGHT, BG);
    Screen::tft.setTextSize(1);

    int yStart = 10;
    int maxVisible = calcMaxVisible();
    int w = Screen::tft.width() - (LIST_MARGIN * 2) - ARROW_AREA_W;
    int buttonW = ITEM_BUTTON_W;
    int labelW = w - buttonW - 12;

    for (int i = 0; i < maxVisible && i + viewOffset < (int)wifiList.size(); i++)
    {
        int idx = i + viewOffset;
        int y = yStart + i * LIST_ITEM_HEIGHT;
        WiFiItem &item = wifiList[idx];

        bool isSelected = (idx == selectedIndex);
        uint16_t rowBg = isSelected ? ACCENT : BG;
        Screen::tft.fillRoundRect(LIST_MARGIN, y, w, LIST_ITEM_HEIGHT - 6, 6, rowBg);

        // label and icons
        String label = item.ssid;
        if (item.known)
            label += " (saved)";
        if (item.secured)
            label += " 🔒";

        Screen::tft.setTextColor(TEXT);
        Screen::tft.setTextSize(1);
        // left area padding 8
        Screen::tft.drawString(label, LIST_MARGIN + 8, y + (LIST_ITEM_HEIGHT / 2) - 2);

        // item-specific button at right of row
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
        drawButtonRect(btnX, btnY, buttonW, ITEM_BUTTON_H, bLabel, bCol, TEXT);
    }

    // draw up/down arrows if needed
    bool canUp = viewOffset > 0;
    bool canDown = (viewOffset + maxVisible) < (int)wifiList.size();
    drawScrollArrows(canUp, canDown);

    // bottom buttons: Connect (connect selected), Rescan, Cancel
    int btnY = Screen::tft.height() - BTN_AREA_HEIGHT + 12;
    int thirdW = (Screen::tft.width() - (LIST_MARGIN * 2)) / 3;
    drawButtonRect(LIST_MARGIN, btnY, thirdW - 8, 40, "Connect", PRIMARY, TEXT);
    drawButtonRect(LIST_MARGIN + thirdW, btnY, thirdW - 8, 40, "Rescan", ACCENT2, TEXT);
    drawButtonRect(LIST_MARGIN + 2 * thirdW, btnY, thirdW - 8, 40, "Cancel", DANGER, TEXT);
}

// --- connection routine with visual feedback ---
static bool tryConnectWithPassOverlay(const String &ssid, const String &pass, unsigned long timeoutMs = 8000)
{
    // disconnect first
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
        // draw small top progress
        drawConnectingTop(ssid, spinner++);
        delay(200);
    }
    return (WiFi.status() == WL_CONNECTED);
}

// prompt the user to save credentials (same options as original)
static void promptStoreOptions(const String &ssid, const String &pass)
{
    if (pass.length() == 0)
    {
        showStatusOverlay("Connected", "Open network - nothing to store", BG, TEXT, 1000);
        return;
    }

    Screen::tft.fillScreen(BG);
    Screen::tft.setTextColor(TEXT);
    Screen::tft.setTextSize(1);
    Screen::tft.drawString("Connected to " + ssid, Screen::tft.width() / 2, 30);
    Screen::tft.drawString("Save credentials?", Screen::tft.width() / 2, 50);

    int w = Screen::tft.width() - 40;
    int btnH = 36;
    int gap = 8;
    int x = 20;
    int y = 80;

    drawButtonRect(x, y, (w / 2) - gap, btnH, "Public", ACCENT2, TEXT);
    drawButtonRect(x + (w / 2) + gap, y, (w / 2) - gap, btnH, "Private", ACCENT, TEXT);
    drawButtonRect(x, y + btnH + gap, (w / 2) - gap, btnH, "Both", PRIMARY, TEXT);
    drawButtonRect(x + (w / 2) + gap, y + btnH + gap, (w / 2) - gap, btnH, "Skip", DANGER, TEXT);

    // Wait for touch
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

// connect to an index (handles private/public stored, open, or prompt for password)
static void connectToIndex(int idx)
{
    if (idx < 0 || idx >= (int)wifiList.size())
        return;

    WiFiItem &item = wifiList[idx];
    String ssid = item.ssid;

    // 1) try private stored
    ENC_FS::Path privPath = {"wifi", toHex(ssid) + ".wifi"};
    if (ENC_FS::exists(privPath))
    {
        String p = ENC_FS::readFileString(privPath);
        showStatusOverlay("Connecting", "Using private storage...", BG, TEXT, 200);
        if (tryConnectWithPassOverlay(ssid, p, 8000))
        {
            showStatusOverlay("Connected", ssid, BG, TEXT, 700);
            promptStoreOptions(ssid, p);
            return;
        }
    }

    // 2) try public stored
    String pubPath = "/public/wifi/" + toHex(ssid) + ".wifi";
    if (SD_FS::exists(pubPath))
    {
        String p = SD_FS::readFile(pubPath);
        showStatusOverlay("Connecting", "Using public storage...", BG, TEXT, 200);
        if (tryConnectWithPassOverlay(ssid, p, 8000))
        {
            showStatusOverlay("Connected", ssid, BG, TEXT, 700);
            promptStoreOptions(ssid, p);
            return;
        }
    }

    // 3) if open network
    if (!item.secured)
    {
        showStatusOverlay("Connecting", "Open network...", BG, TEXT, 200);
        if (tryConnectWithPassOverlay(ssid, "", 5000))
        {
            showStatusOverlay("Connected", ssid, BG, TEXT, 700);
            return;
        }
        else
        {
            // failed open network
            uiState = WIFI_FAILED;
            // draw failure screen (Back + Retry)
            // We'll present retry (enter new pass) because maybe AP requires password despite being flagged open
        }
    }

    // 4) ask user for password
    if (item.secured)
    {
        String entered = readString("Password for " + ssid + ":", "");
        if (entered == "")
        {
            // cancelled
            return;
        }
        showStatusOverlay("Connecting", "Using entered password...", BG, TEXT, 150);
        if (tryConnectWithPassOverlay(ssid, entered, 8000))
        {
            showStatusOverlay("Connected", ssid, BG, TEXT, 700);
            promptStoreOptions(ssid, entered);
            return;
        }
        else
        {
            // failed: show Retry/Back screen (allow new password or go back)
            Screen::tft.fillScreen(DANGER);
            Screen::tft.setTextColor(TEXT);
            Screen::tft.setTextSize(1);
            Screen::tft.drawString("Failed to connect to " + ssid, Screen::tft.width() / 2, 40);
            Screen::tft.drawString("Retry with new password or Back", Screen::tft.width() / 2, 60);

            int w = Screen::tft.width() - 40;
            int btnH = 40;
            int gap = 12;
            int x = 20;
            int y = 100;
            drawButtonRect(x, y, (w / 2) - gap, btnH, "Retry", PRIMARY, TEXT);
            drawButtonRect(x + (w / 2) + gap, y, (w / 2) - gap, btnH, "Back", DANGER, TEXT);

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
                            // Retry: get new password
                            String newPass = readString("New password for " + ssid + ":", "");
                            if (newPass.length() == 0)
                            {
                                // cancelled
                                return;
                            }
                            showStatusOverlay("Connecting", "Retrying...", BG, TEXT, 150);
                            if (tryConnectWithPassOverlay(ssid, newPass, 8000))
                            {
                                showStatusOverlay("Connected", ssid, BG, TEXT, 700);
                                promptStoreOptions(ssid, newPass);
                                return;
                            }
                            else
                            {
                                showStatusOverlay("Failed", "Retry failed", DANGER, TEXT, 900);
                                return;
                            }
                        }
                        else if (tx >= x + (w / 2) + gap && tx <= x + w)
                        {
                            // Back pressed
                            return;
                        }
                    }
                }
                delay(20);
            }
        }
    }
}

// --- UI update and touch handling ---
static bool updateWiFiManager()
{
    auto touch = Screen::getTouchPos();

    // scroll with move
    if (abs(touch.move.y) > 4)
    {
        // convert pixel delta to rows (positive move.y = down finger movement -> scroll up)
        int deltaRows = (touch.move.y) / LIST_ITEM_HEIGHT;
        viewOffset -= deltaRows;
        clampViewOffset();
        drawWiFiList();
    }

    // no click -> nothing to do
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
            // cancel/exit - return false to caller
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
            // up arrow
            viewOffset -= 1;
            clampViewOffset();
            drawWiFiList();
            return true;
        }
        else if (touch.y >= ay + 22 && touch.y <= ay + 40)
        {
            // down arrow
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
        // determine if click on the row button
        int btnX = LIST_MARGIN + wArea - buttonW - 8;
        int btnY = rowY + (LIST_ITEM_HEIGHT - ITEM_BUTTON_H) / 2;
        if (touch.x >= btnX && touch.x <= btnX + buttonW && touch.y >= btnY && touch.y <= btnY + ITEM_BUTTON_H)
        {
            // button activated: Connect / Open / Pass
            WiFiItem &item = wifiList[clickedIndex];
            if (item.known)
            {
                connectToIndex(clickedIndex);
                drawWiFiList();
                return true;
            }
            else if (!item.secured)
            {
                // open network - try directly
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
                // secured, ask for password
                String entered = readString("Password for " + item.ssid + ":", "");
                if (entered.length() == 0)
                {
                    // cancelled
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
                    showStatusOverlay("Failed", "Could not connect", DANGER, TEXT, 900);
                }
                drawWiFiList();
                return true;
            }
        }
        else
        {
            // clicked the row -> select it
            selectedIndex = clickedIndex;
            // ensure selected visible
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
    // initial scan + draw
    scanWiFisAndShow();
    drawWiFiList();

    // loop until user cancels (updateWiFiManager returns false)
    while (true)
    {
        if (!updateWiFiManager())
            break;
        delay(20);
    }

    // restore basic screen state if needed
    Screen::tft.fillScreen(BG);
}
