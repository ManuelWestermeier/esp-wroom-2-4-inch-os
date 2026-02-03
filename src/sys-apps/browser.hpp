#pragma once

#include <Arduino.h>

namespace Browser
{
    struct Location
    {
        String domain;           // domain:port
        String state;            // like "lists|1234|edit"
        String session;          // max 1 KB storable and readable from {"browser", "sites", domain, "session.data"}
        static String sessionId; // uint32 every newstart random, global
    };
    static Location loc;
    static bool isRunning = false;

    void ReRender()
    {
        // Full Rerender based on state (not between sessions)
    }

    void Update()
    {
        // hand data, input
        // Create Websocket to the server, send seccion-id.
        // Server can: give tft commands (like drawString, fillRect, ...., draw Svg)
        // Can get sessionId, state (+set), session (+set), get color pallete by user
        // (fully server rendered)
    }

    void Start()
    {
        // get the domain, + state (from "domain:port@state") (port: 6767; default = "mw-search-server.onrender.app:6767@startpage")
    }

    void Exit()
    {
        // prompt (do you want?)
        isRunning = false;
    }

    void OnExit()
    {
        // Disconnect
    }
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