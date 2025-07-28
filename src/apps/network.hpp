#pragma once

#include <WiFi.h>

namespace LuaApps::Network
{
    bool connectWiFi(const char *ssid, const char *password);
    WiFiServer *createServer(uint16_t port);
    WiFiClient connectToHost(const char *host, uint16_t port);
}
