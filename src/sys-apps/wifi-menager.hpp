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
    Screen::tft.setTextDatum(MC_DATUM);
    Screen::tft.drawString(label, x + w / 2, y + h / 2);
}

void drawWiFiList()
{
    int yStart = 10;
    int btnAreaHeight = 60;
    int maxVisible = (Screen::tft.height() - yStart - btnAreaHeight) / LIST_ITEM_HEIGHT;

    Screen::tft.fillRect(0, 0, Screen::tft.width(), Screen::tft.height() - btnAreaHeight, BG);

    for (int i = 0; i < maxVisible && i + viewOffset < wifiList.size(); i++)
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
        Screen::tft.setTextDatum(ML_DATUM);
        Screen::tft.drawString(label, 20, y + (LIST_ITEM_HEIGHT / 2) - 2);
    }

    // Buttons at bottom
    int btnY = Screen::tft.height() - btnAreaHeight + 10;
    drawButton(10, btnY, (Screen::tft.width() / 3) - 10, 40, "Connect", PRIMARY, TEXT);
    drawButton(Screen::tft.width() / 3 + 5, btnY, (Screen::tft.width() / 3) - 10, 40, "Rescan", ACCENT2, TEXT);
    drawButton(2 * Screen::tft.width() / 3 + 5, btnY, (Screen::tft.width() / 3) - 15, 40, "Cancel", DANGER, TEXT);
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
            item.ssid = fromHex(String(f.name()).substring(0, String(f.name()).length() - 5)); // strip ".wifi"
            item.secured = true;                                               // assume secured if saved
            item.known = true;
            item.source = "public";
            wifiList.push_back(item);
        }
    }

    // Private stored networks
    auto privFiles = ENC_FS::readDir({"wifi"});
    for (auto &name : privFiles)
    {
        WiFiItem item;
        item.ssid = fromHex(name.substring(0, name.length() - 5)); // strip ".wifi"
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
                it.secured = secured; // update
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

void connectSelectedWiFi()
{
    if (selectedIndex < 0 || selectedIndex >= wifiList.size())
        return;

    WiFiItem &item = wifiList[selectedIndex];
    String pass;

    if (item.known)
    {
        if (item.source == "private")
        {
            pass = ENC_FS::readFileString({"wifi", toHex(item.ssid) + ".wifi"});
        }
        else if (item.source == "public")
        {
            pass = SD_FS::readFile("/public/wifi/" + toHex(item.ssid) + ".wifi");
        }
    }
    else if (item.secured)
    {
        pass = readString("Password for " + item.ssid + ":", "");
        if (pass == "")
        { // cancelled
            drawWiFiList();
            return;
        }
    }

    if (item.secured)
    {
        WiFi.begin(item.ssid.c_str(), pass.c_str());
    }
    else
    {
        WiFi.begin(item.ssid.c_str());
    }

    uiState = WIFI_CONNECTING;
    stateStart = millis();
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
        if (viewOffset > (int)wifiList.size() - 1)
            viewOffset = wifiList.size() - 1;
        drawWiFiList();
    }

    if (uiState == WIFI_CONNECTING)
    {
        if (millis() - stateStart > 8000)
        {
            if (WiFi.status() == WL_CONNECTED)
            {
                uiState = WIFI_CONNECTED;
                Screen::tft.fillScreen(BG);
                Screen::tft.setTextColor(ACCENT);
                Screen::tft.setTextDatum(MC_DATUM);
                Screen::tft.drawString("Connected: " + wifiList[selectedIndex].ssid,
                                       Screen::tft.width() / 2, Screen::tft.height() / 2);
                delay(1200);
            }
            else
            {
                uiState = WIFI_FAILED;
                Screen::tft.fillScreen(DANGER);
                Screen::tft.setTextColor(TEXT);
                Screen::tft.setTextDatum(MC_DATUM);
                Screen::tft.drawString("Failed to connect",
                                       Screen::tft.width() / 2, Screen::tft.height() / 2);
                delay(1200);
            }
            drawWiFiList();
            uiState = WIFI_IDLE;
        }
        return true;
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
    if (idx >= 0 && idx < wifiList.size())
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
