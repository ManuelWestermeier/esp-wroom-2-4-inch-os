#pragma once

#include <Arduino.h>
#include <WebSocketsClient.h>
#include "../screen/index.hpp"
#include "../io/read-string.hpp"
#include "../styles/global.hpp"
#include "../fs/enc-fs.hpp"
#include "nanosvg.h"

namespace Browser
{
    struct Location
    {
        String domain = "mw-search-server-onrender-app.onrender.com";
        int port = 443;
        String state = "startpage";
        String session = "";
        static String sessionId;
    };

    extern Location loc;
    extern bool isRunning;
    extern WebSocketsClient webSocket;

    // Core
    void Start();
    void Update();
    void ReRender();
    void Exit();
    void OnExit();
    void handleCommand(const String &payload);

    // Utilities
    void drawText(int x, int y, const String &text, uint16_t color, int size = 2);
    void drawCircle(int x, int y, int r, uint16_t color);
    void drawSVG(const String &svgStr, int x, int y, int w, int h, uint16_t color);
    String promptText(const String &question, const String &defaultValue = "");
    void clearSettings();
    uint16_t getThemeColor(const String &name);
    void storeData(const String &key, const ENC_FS::Buffer &data);
    ENC_FS::Buffer loadData(const String &key);
}

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
