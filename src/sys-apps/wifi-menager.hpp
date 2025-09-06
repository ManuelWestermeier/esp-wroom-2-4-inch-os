#include <Arduino.h>
#include <WiFi.h>

#include "../screen/index.hpp"
#include "../styles/global.hpp"
#include "../fs/index.hpp"
#include "../fs/enc-fs.hpp"
#include "../wifi/index.hpp"
#include "../io/read-string.hpp"
#include "../utils/hex.hpp"

#define BTN_RADIUS 8
#define LIST_ITEM_HEIGHT 40

struct WiFiItem
{
    String ssid;
    bool secured;
    bool known;
};

static std::vector<WiFiItem> wifiList;
static int selectedIndex = 0;
static int viewOffset = 0;

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
    int maxVisible = (Screen::tft.height() - yStart - 20) / LIST_ITEM_HEIGHT;
    Screen::tft.fillScreen(BG);

    for (int i = 0; i < maxVisible && i + viewOffset < wifiList.size(); i++)
    {
        int y = yStart + i * LIST_ITEM_HEIGHT;
        WiFiItem &item = wifiList[i + viewOffset];

        uint16_t bg = (i + viewOffset == selectedIndex) ? ACCENT : BG;
        Screen::tft.fillRoundRect(10, y, Screen::tft.width() - 20, LIST_ITEM_HEIGHT - 5, BTN_RADIUS, bg);

        String label = item.ssid + (item.known ? " (known)" : "") + (item.secured ? " 🔒" : "");
        Screen::tft.setTextColor(TEXT);
        Screen::tft.setTextDatum(MC_DATUM);
        Screen::tft.drawString(label, Screen::tft.width() / 2, y + (LIST_ITEM_HEIGHT / 2) - 2);
    }

    drawButton(10, Screen::tft.height() - 50, Screen::tft.width() / 2 - 15, 40, "Connect", PRIMARY, TEXT);
    drawButton(Screen::tft.width() / 2 + 5, Screen::tft.height() - 50, Screen::tft.width() / 2 - 15, 40, "Rescan", ACCENT2, TEXT);
}

void scanWiFis()
{
    WiFi.scanDelete();
    int n = WiFi.scanNetworks();
    wifiList.clear();

    for (int i = 0; i < n; i++)
    {
        WiFiItem item;
        item.ssid = WiFi.SSID(i);
        item.secured = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;

        // check if known
        String file = "/public/wifi/" + toHex(item.ssid) + ".wifi";
        item.known = SD_FS::exists(file) || ENC_FS::exists({"wifi", toHex(item.ssid) + ".wifi"});

        wifiList.push_back(item);
    }
    selectedIndex = 0;
    viewOffset = 0;
}

void handleSelection()
{
    if (selectedIndex < 0 || selectedIndex >= wifiList.size())
        return;

    WiFiItem &item = wifiList[selectedIndex];
    String pass;

    if (item.secured && !item.known)
    {
        pass = readString("Password for " + item.ssid + ":");
    }

    // connect
    if (item.secured)
    {
        WiFi.begin(item.ssid.c_str(), pass.c_str());
        if (WiFi.waitForConnectResult(8000) == WL_CONNECTED)
        {
            Screen::tft.fillScreen(BG);
            Screen::tft.setTextColor(ACCENT);
            Screen::tft.drawString("Connected to " + item.ssid, Screen::tft.width() / 2, Screen::tft.height() / 2);

            // Ask user to save password
            int save = readString("Save password? (public/private/no)", "no") == "public" ? 1 : 0;
            if (save)
                UserWiFi::addPublicWifi(item.ssid, pass);
            else if (!save)
                UserWiFi::addPrivateWifi(item.ssid, pass);
        }
        else
        {
            Screen::tft.fillScreen(DANGER);
            Screen::tft.setTextColor(TEXT);
            Screen::tft.drawString("Failed to connect", Screen::tft.width() / 2, Screen::tft.height() / 2);
        }
    }
    else
    {
        WiFi.begin(item.ssid.c_str());
        Screen::tft.fillScreen(BG);
        Screen::tft.setTextColor(ACCENT);
        Screen::tft.drawString("Connected to " + item.ssid, Screen::tft.width() / 2, Screen::tft.height() / 2);
    }
}

void openWifiManager()
{
    scanWiFis();
    drawWiFiList();

    // main interaction loop
    while (true)
    {
        auto evt = Screen::getTouchPos(); // assuming you have a function that returns {x, y, pressed}

        if (!evt.clicked)
            continue;

        // check buttons
        if (evt.y > Screen::tft.height() - 50)
        {
            if (evt.x < Screen::tft.width() / 2)
                handleSelection();
            else
                scanWiFis();
            drawWiFiList();
            continue;
        }

        // check list
        int idx = (evt.y - 10) / LIST_ITEM_HEIGHT + viewOffset;
        if (idx >= 0 && idx < wifiList.size())
        {
            selectedIndex = idx;
            drawWiFiList();
        }
    }
}
