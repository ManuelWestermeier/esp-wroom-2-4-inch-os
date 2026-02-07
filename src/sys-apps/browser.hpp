#pragma once

#include <Arduino.h>
#include <WebSocketsClient.h>
#include "../screen/index.hpp"
#include "../io/read-string.hpp"
#include "../styles/global.hpp"

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

    void Start();
    void Update();
    void ReRender();
    void Exit();
    void OnExit();
    void handleCommand(const String &payload);
}

static inline void openBrowser()
{
    Browser::isRunning = true;
    Browser::Start();
    while (Browser::isRunning)
    {
        Browser::Update();
        vTaskDelay(5);
    }
    Browser::OnExit();
}
