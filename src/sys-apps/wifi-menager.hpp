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
    int btnAreaHeight = 60;
    int maxVisible = (Screen::tft.height() - yStart - btnAreaHeight) / LIST_ITEM_HEIGHT;

    Screen::tft.fillRect(0, 0, Screen::tft.width(), Screen::tft.height() - btnAreaHeight, BG);

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

    // Draw buttons at the bottom
    int btnY = Screen::tft.height() - btnAreaHeight + 10;
    drawButton(10, btnY, Screen::tft.width() / 2 - 15, 40, "Connect", PRIMARY, TEXT);
    drawButton(Screen::tft.width() / 2 + 5, btnY, Screen::tft.width() / 2 - 15, 40, "Rescan", ACCENT2, TEXT);
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

        String file = "/public/wifi/" + toHex(item.ssid) + ".wifi";
        item.known = SD_FS::exists(file) || ENC_FS::exists({"wifi", toHex(item.ssid) + ".wifi"});

        wifiList.push_back(item);
    }
    selectedIndex = 0;
    viewOffset = 0;
}

void connectSelectedWiFi()
{
    if (selectedIndex < 0 || selectedIndex >= wifiList.size())
        return;

    WiFiItem &item = wifiList[selectedIndex];
    String pass;

    if (item.known && item.secured)
    {
        String file = "/public/wifi/" + toHex(item.ssid) + ".wifi";
        ENC_FS::Path encPath = {"wifi", toHex(item.ssid) + ".wifi"};
        if (ENC_FS::exists(encPath))
            pass = ENC_FS::readFileString(encPath);
        else if (SD_FS::exists(file))
            pass = SD_FS::readFile(file);
    }
    if (item.secured && !item.known)
    {
        pass = readString("Password for " + item.ssid + ":", "");
    }

    if (item.secured)
    {
        WiFi.begin(item.ssid.c_str(), pass.c_str());
        if (WiFi.waitForConnectResult(8000) == WL_CONNECTED)
        {
            Screen::tft.fillScreen(BG);
            Screen::tft.setTextColor(ACCENT);
            Screen::tft.setTextDatum(MC_DATUM);
            Screen::tft.drawString("Connected: " + item.ssid, Screen::tft.width() / 2, Screen::tft.height() / 2);

            // Ask user to save password
            String save = readString("Save password? (public/private/no)", "no");
            if (save == "public")
                UserWiFi::addPublicWifi(item.ssid, pass);
            else if (save == "private")
                UserWiFi::addPrivateWifi(item.ssid, pass);
        }
        else
        {
            Screen::tft.fillScreen(DANGER);
            Screen::tft.setTextColor(TEXT);
            Screen::tft.setTextDatum(MC_DATUM);
            Screen::tft.drawString("Failed to connect", Screen::tft.width() / 2, Screen::tft.height() / 2);
        }
    }
    else
    {
        WiFi.begin(item.ssid.c_str());
        Screen::tft.fillScreen(BG);
        Screen::tft.setTextColor(ACCENT);
        Screen::tft.setTextDatum(MC_DATUM);
        Screen::tft.drawString("Connected: " + item.ssid, Screen::tft.width() / 2, Screen::tft.height() / 2);
    }

    delay(2000);
    drawWiFiList(); // Redraw after connecting
}

void openWifiManager()
{
    Screen::tft.fillScreen(BG);
    scanWiFis();
    drawWiFiList();

    while (true)
    {
        auto touch = Screen::getTouchPos();
        if (!touch.clicked)
            continue;

        int btnAreaHeight = 60;
        int btnY = Screen::tft.height() - btnAreaHeight + 10;

        // Connect button
        if (touch.y >= btnY && touch.y <= btnY + 40)
        {
            if (touch.x < Screen::tft.width() / 2)
                connectSelectedWiFi();
            else
            {
                scanWiFis();
                drawWiFiList();
            }
            continue;
        }

        // List touch
        int idx = (touch.y - 10) / LIST_ITEM_HEIGHT + viewOffset;
        if (idx >= 0 && idx < wifiList.size())
        {
            selectedIndex = idx;
            drawWiFiList();
        }
    }
}
