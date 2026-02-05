#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "../screen/index.hpp"
#include "../io/read-string.hpp"
#include "../styles/global.hpp"
#include "../fs/enc-fs.hpp"

namespace Browser
{
    struct Location
    {
        String domain;           // domain:port
        String state;            // like "lists|1234|edit"
        String session;          // max 1 KB storable and readable from {"browser", "sites", domain, "session.data"}
        static String sessionId; // uint32 every newstart random, global
    };

    extern Location loc;
    extern bool isRunning;

    void ReRender();
    void Update();
    void Start();
    void Start(const String& url);
    void Exit();
    void OnExit();
}

// Main entry point
void openBrowser();
void openBrowser(const String& url);