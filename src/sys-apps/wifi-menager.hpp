#include <Arduino.h>
#include <WiFi.h>

#include "../screen/index.hpp"
#include "../utils/hex.hpp"
#include "../styles/global.hpp"
#include "../fs/index.hpp"
#include "../fs/enc-fs.hpp"
#include "../wifi/index.hpp"
#include "../io/read-string.hpp"

#define BTN_RADIUS 8
#define LIST_ITEM_HEIGHT 40

struct WiFiItem
{
    String ssid;
    bool secured;
    bool known;
    String source; // "scan" | "public" | "private"
};

static std::vector<WiFiItem> wifiList;
static int selectedIndex = 0;
static int viewOffset = 0;

enum WiFiUIState
{
    WIFI_IDLE,
    WIFI_CONNECTING,
    WIFI_CONNECTED,
    WIFI_FAILED,
    WIFI_EXIT
};

static WiFiUIState uiState = WIFI_IDLE;
static unsigned long stateStart = 0;

void drawButton(int x, int y, int w, int h, const String &label, uint16_t bgColor, uint16_t textColor)
{
    Screen::tft.fillRoundRect(x, y, w, h, BTN_RADIUS, bgColor);
    Screen::tft.setTextColor(textColor);
    Screen::tft.drawString(label, x + w / 2, y + h / 2);
}

void showStatusScreen(const String &title, const String &msg, uint16_t bgColor, uint16_t textColor, unsigned long msDelay = 1000)
{
    Screen::tft.fillScreen(bgColor);
    Screen::tft.setTextSize(2);
    Screen::tft.setTextColor(textColor);
    Screen::tft.drawString(title, Screen::tft.width() / 2, Screen::tft.height() / 2 - 12);
    Screen::tft.setTextSize(1);
    Screen::tft.drawString(msg, Screen::tft.width() / 2, Screen::tft.height() / 2 + 12);
    if (msDelay)
        delay(msDelay);
}

void drawWiFiList()
{
    int yStart = 10;
    int btnAreaHeight = 60;
    int maxVisible = (Screen::tft.height() - yStart - btnAreaHeight) / LIST_ITEM_HEIGHT;

    Screen::tft.fillRect(0, 0, Screen::tft.width(), Screen::tft.height() - btnAreaHeight, BG);
    Screen::tft.setTextSize(1);

    for (int i = 0; i < maxVisible && i + viewOffset < (int)wifiList.size(); i++)
    {
        int y = yStart + i * LIST_ITEM_HEIGHT;
        WiFiItem &item = wifiList[i + viewOffset];

        uint16_t bg = (i + viewOffset == selectedIndex) ? ACCENT : BG;
        Screen::tft.fillRoundRect(10, y, Screen::tft.width() - 20, LIST_ITEM_HEIGHT - 5, BTN_RADIUS, bg);

        String label = item.ssid;
        if (item.known)
            label += " (saved)";
        if (item.secured)
            label += " 🔒";

        Screen::tft.setTextColor(TEXT);
        Screen::tft.drawString(label, 20, y + (LIST_ITEM_HEIGHT / 2) - 2);
    }

    // Buttons at bottom (fit for 320x240)
    int btnY = Screen::tft.height() - btnAreaHeight + 10;
    int thirdW = Screen::tft.width() / 3;
    drawButton(10, btnY, thirdW - 20, 40, "Connect", PRIMARY, TEXT);
    drawButton(10 + thirdW, btnY, thirdW - 20, 40, "Rescan", ACCENT2, TEXT);
    drawButton(10 + 2 * thirdW, btnY, thirdW - 20, 40, "Cancel", DANGER, TEXT);
}

void loadKnownWiFis()
{
    // Public stored networks
    auto pubFiles = SD_FS::readDir("/public/wifi");
    for (auto &f : pubFiles)
    {
        if (!f.isDirectory())
        {
            WiFiItem item;
            String name = String(f.name());
            // strip ".wifi"
            if (name.endsWith(".wifi"))
                name = name.substring(0, name.length() - 5);

            item.ssid = fromHex(name);
            item.secured = true; // assume secured if saved
            item.known = true;
            item.source = "public";
            wifiList.push_back(item);
        }
    }

    // Private stored networks
    auto privFiles = ENC_FS::readDir({"wifi"});
    for (auto &name : privFiles)
    {
        String sname = String(name);
        if (sname.endsWith(".wifi"))
            sname = sname.substring(0, sname.length() - 5);

        WiFiItem item;
        item.ssid = fromHex(sname);
        item.secured = true;
        item.known = true;
        item.source = "private";
        wifiList.push_back(item);
    }
}

void scanWiFis()
{
    WiFi.scanDelete();
    int n = WiFi.scanNetworks();

    wifiList.clear();
    loadKnownWiFis();

    for (int i = 0; i < n; i++)
    {
        String ssid = WiFi.SSID(i);
        bool secured = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;

        // Skip if already known
        bool already = false;
        for (auto &it : wifiList)
        {
            if (it.ssid == ssid)
            {
                it.secured = secured; // update encryption info
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

    selectedIndex = 0;
    viewOffset = 0;
    drawWiFiList();
}

// Try connecting with pass ("" for open). Shows a connecting status and returns true if connected
bool tryConnectWithPass(const String &ssid, const String &pass, unsigned long timeoutMs = 8000)
{
    WiFi.disconnect(true);
    delay(100);
    if (pass.length() == 0)
    {
        WiFi.begin(ssid.c_str());
    }
    else
    {
        WiFi.begin(ssid.c_str(), pass.c_str());
    }

    unsigned long start = millis();
    int spinner = 0;
    while (millis() - start < timeoutMs)
    {
        // draw a small connecting indicator
        Screen::tft.fillRect(0, 0, Screen::tft.width(), 40, BG);
        Screen::tft.setTextColor(TEXT);
        Screen::tft.setTextSize(1);
        String msg = "Connecting to: " + ssid + " ";
        Screen::tft.drawString(msg, 8, 8);
        // spinner
        const char *sp = "|/-\\";
        Screen::tft.drawString(String(sp[spinner % 4]), Screen::tft.width() - 12, 8);
        spinner++;
        if (WiFi.status() == WL_CONNECTED)
            return true;
        delay(200);
    }
    return (WiFi.status() == WL_CONNECTED);
}

// Prompt user to choose how to store the network after success
void promptStoreOptions(const String &ssid, const String &pass)
{
    // don't offer if open (pass empty)
    if (pass.length() == 0)
    {
        showStatusScreen("Connected", "Open network - nothing to store", BG, TEXT, 1000);
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

    // Buttons: Public | Private | Both | Skip
    drawButton(x, y, (w / 2) - gap, btnH, "Public", ACCENT2, TEXT);
    drawButton(x + (w / 2) + gap, y, (w / 2) - gap, btnH, "Private", ACCENT, TEXT);
    drawButton(x, y + btnH + gap, (w / 2) - gap, btnH, "Both", PRIMARY, TEXT);
    drawButton(x + (w / 2) + gap, y + btnH + gap, (w / 2) - gap, btnH, "Skip", DANGER, TEXT);

    // wait for touch and react
    while (true)
    {
        auto t = Screen::getTouchPos();
        if (t.clicked)
        {
            int tx = t.x;
            int ty = t.y;

            // first row
            if (ty >= y && ty <= y + btnH)
            {
                if (tx >= x && tx <= x + (w / 2) - gap)
                {
                    // Public
                    SD_FS::writeFile("/public/wifi/" + toHex(ssid) + ".wifi", pass);
                    showStatusScreen("Saved", "Saved as public", BG, TEXT, 900);
                    break;
                }
                else if (tx >= x + (w / 2) + gap && tx <= x + w)
                {
                    // Private
                    ENC_FS::writeFileString({"wifi", toHex(ssid) + ".wifi"}, pass);
                    showStatusScreen("Saved", "Saved as private", BG, TEXT, 900);
                    break;
                }
            }
            // second row
            if (ty >= y + btnH + gap && ty <= y + 2 * btnH + gap)
            {
                if (tx >= x && tx <= x + (w / 2) - gap)
                {
                    // Both
                    SD_FS::writeFile("/public/wifi/" + toHex(ssid) + ".wifi", pass);
                    ENC_FS::writeFileString({"wifi", toHex(ssid) + ".wifi"}, pass);
                    showStatusScreen("Saved", "Saved both", BG, TEXT, 900);
                    break;
                }
                else if (tx >= x + (w / 2) + gap && tx <= x + w)
                {
                    // Skip
                    showStatusScreen("OK", "Not saved", BG, TEXT, 600);
                    break;
                }
            }
        }
        delay(20);
    }
}

// Attempt to connect following: try private stored -> public stored -> open -> prompt password
void connectSelectedWiFi()
{
    if (selectedIndex < 0 || selectedIndex >= (int)wifiList.size())
        return;

    WiFiItem &item = wifiList[selectedIndex];

    String ssid = item.ssid;
    String pass;

    // Build candidate list: private stored, public stored
    bool triedAnyStored = false;

    // Check private
    ENC_FS::Path privPath = {"wifi", toHex(ssid) + ".wifi"};
    if (ENC_FS::exists(privPath))
    {
        String p = ENC_FS::readFileString(privPath);
        showStatusScreen("Connecting", "Using private storage...", BG, TEXT, 200);
        if (tryConnectWithPass(ssid, p, 8000))
        {
            // connected
            showStatusScreen("Connected", ssid, BG, TEXT, 700);
            promptStoreOptions(ssid, p); // still let user decide
            return;
        }
        triedAnyStored = true;
    }

    // Check public
    String pubPath = "/public/wifi/" + toHex(ssid) + ".wifi";
    if (SD_FS::exists(pubPath))
    {
        String p = SD_FS::readFile(pubPath);
        showStatusScreen("Connecting", "Using public storage...", BG, TEXT, 200);
        if (tryConnectWithPass(ssid, p, 8000))
        {
            // connected
            showStatusScreen("Connected", ssid, BG, TEXT, 700);
            promptStoreOptions(ssid, p);
            return;
        }
        triedAnyStored = true;
    }

    // If open network, try directly
    if (!item.secured)
    {
        showStatusScreen("Connecting", "Open network...", BG, TEXT, 200);
        if (tryConnectWithPass(ssid, "", 5000))
        {
            showStatusScreen("Connected", ssid, BG, TEXT, 700);
            // open - nothing to save
            return;
        }
        else
        {
            showStatusScreen("Failed", "Could not connect to open network.", DANGER, TEXT, 900);
            drawWiFiList();
            return;
        }
    }

    // If stored tries failed or none found -> prompt for password
    String prompt = "Password for " + ssid;
    String entered = readString(prompt + ":", "");
    if (entered == "")
    {
        // Cancelled by user
        drawWiFiList();
        return;
    }

    // Try the entered password
    showStatusScreen("Connecting", "Using entered password...", BG, TEXT, 150);
    if (tryConnectWithPass(ssid, entered, 8000))
    {
        showStatusScreen("Connected", ssid, BG, TEXT, 700);
        // Offer to save
        promptStoreOptions(ssid, entered);
        return;
    }
    else
    {
        // Failed: offer retry or cancel
        Screen::tft.fillScreen(DANGER);
        Screen::tft.setTextColor(TEXT);
        Screen::tft.setTextSize(1);
        Screen::tft.drawString("Failed to connect", Screen::tft.width() / 2, 40);
        Screen::tft.drawString("Retry with new password or Cancel", Screen::tft.width() / 2, 60);

        int w = Screen::tft.width() - 40;
        int btnH = 40;
        int gap = 12;
        int x = 20;
        int y = 100;
        drawButton(x, y, (w / 2) - gap, btnH, "Retry", PRIMARY, TEXT);
        drawButton(x + (w / 2) + gap, y, (w / 2) - gap, btnH, "Cancel", DANGER, TEXT);

        // wait for selection
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
                        // Retry -> prompt new password and try once
                        String newPass = readString("New password for " + ssid + ":", "");
                        if (newPass != "")
                        {
                            showStatusScreen("Connecting", "Retrying...", BG, TEXT, 150);
                            if (tryConnectWithPass(ssid, newPass, 8000))
                            {
                                showStatusScreen("Connected", ssid, BG, TEXT, 700);
                                promptStoreOptions(ssid, newPass);
                                return;
                            }
                            else
                            {
                                showStatusScreen("Failed", "Retry failed", DANGER, TEXT, 900);
                                drawWiFiList();
                                return;
                            }
                        }
                        else
                        {
                            // Cancelled entering new password
                            drawWiFiList();
                            return;
                        }
                    }
                    else if (tx >= x + (w / 2) + gap && tx <= x + w)
                    {
                        // Cancel
                        drawWiFiList();
                        return;
                    }
                }
            }
            delay(20);
        }
    }
}

bool updateWiFiManager()
{
    auto touch = Screen::getTouchPos();

    // Scroll handling
    if (abs(touch.move.y) > 3)
    {
        viewOffset -= (touch.move.y / LIST_ITEM_HEIGHT);
        if (viewOffset < 0)
            viewOffset = 0;
        if (viewOffset > std::max(0, (int)wifiList.size() - 1))
            viewOffset = std::max(0, (int)wifiList.size() - 1);
        drawWiFiList();
    }

    if (!touch.clicked)
        return true;

    int btnAreaHeight = 60;
    int btnY = Screen::tft.height() - btnAreaHeight + 10;

    // Bottom buttons
    if (touch.y >= btnY && touch.y <= btnY + 40)
    {
        if (touch.x < Screen::tft.width() / 3)
        {
            connectSelectedWiFi();
        }
        else if (touch.x < 2 * Screen::tft.width() / 3)
        {
            // rescan
            showStatusScreen("Scanning", "Please wait...", BG, TEXT, 200);
            scanWiFis();
        }
        else
        {
            uiState = WIFI_EXIT;
            return false; // exit loop
        }
        return true;
    }

    // Select list item
    int idx = (touch.y - 10) / LIST_ITEM_HEIGHT + viewOffset;
    if (idx >= 0 && idx < (int)wifiList.size())
    {
        selectedIndex = idx;
        drawWiFiList();
    }
    return true;
}

void openWifiManager()
{
    Screen::tft.fillScreen(BG);
    scanWiFis();

    while (true)
    {
        if (!updateWiFiManager())
            break; // exit on cancel
        delay(20);
    }
}
