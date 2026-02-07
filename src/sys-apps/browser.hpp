#pragma once

#include <Arduino.h>
#include <vector>
#include <WebSocketsClient.h>

#include "../screen/index.hpp"
#include "../io/read-string.hpp"
#include "../styles/global.hpp"
#include "../fs/enc-fs.hpp"
#include "../utils/time.hpp"
#include "nanosvg.h"

namespace Browser
{
    struct Location
    {
        String domain = "mw-search-server-onrender-app.onrender.com";
        int port = 443;
        String state = "startpage";
        String session = "";
        String title;
        static String sessionId;
    };

    extern Location loc;
    extern bool isRunning;
    extern WebSocketsClient webSocket;

    // ---- Core lifecycle ----
    void Start();
    void Update();
    void ReRender();
    void Exit();
    void OnExit();
    void handleCommand(const String &payload);

    // ---- Utilities ----
    void drawText(int x, int y, const String &text, uint16_t color, int size = 2);
    void drawCircle(int x, int y, int r, uint16_t color);
    void drawSVG(const String &svgStr, int x, int y, int w, int h, uint16_t color);
    String promptText(const String &question, const String &defaultValue = "");
    void clearSettings();
    uint16_t getThemeColor(const String &name);
    void storeData(const String &domain, const ENC_FS::Buffer &data);
    ENC_FS::Buffer loadData(const String &domain);
    void handleTouch();
    void navigate(const String &domain, int port, const String &state);

    // ---- UI ----
    void showSettingsPage();
    void showOSSearchPage();
    void showInputPage();
    void showVisitedSites();
    void showHomeUI();
    void showWebsitePage();
    void renderTopBar();
    void saveVisitedSite(const String &domain);

    // Viewport helpers
    void enterViewport(int x, int y, int w, int h);
    void exitViewport();
}

// ---- Blocking browser runner ----
static inline void openBrowser()
{
    Browser::isRunning = true;
    Browser::Start();
    while (Browser::isRunning)
    {
        Browser::Update();
        vTaskDelay(10);
    }
    Browser::OnExit();
}