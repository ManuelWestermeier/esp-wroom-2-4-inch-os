#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <vector>

#include "../fs/index.hpp"
#include "../utils/time.hpp"

namespace UserWiFi
{
    // ---------------------- WiFi Config ----------------------
    struct KnownWiFi
    {
        String ssid;
        String password;
    };

    std::vector<KnownWiFi> knownNetworks = {
        {"LocalHost", "hhhhhhhy"},
        {"HomeWiFi", "password123"},
        {"OfficeNet", "officepass"},
    };

    TaskHandle_t WiFiConnectTaskHandle = NULL;

    void WiFiConnectTask(void *)
    {
        for (;;)
        {
            if (WiFi.status() != WL_CONNECTED)
            {
                Serial.println("\n[WiFi] Scanning for networks...");
                int n = WiFi.scanNetworks();
                if (n == 0)
                {
                    Serial.println("[WiFi] No networks found.");
                }
                else
                {
                    bool connected = false;

                    // Try known networks first
                    for (auto &net : knownNetworks)
                    {
                        if (connected)
                            break;
                        for (int j = 0; j < n; j++)
                        {
                            if (WiFi.SSID(j) == net.ssid)
                            {
                                Serial.printf("[WiFi] Found known network: %s, connecting...\n", net.ssid.c_str());
                                WiFi.begin(net.ssid.c_str(), net.password.c_str());

                                unsigned long start = millis();
                                while (WiFi.status() != WL_CONNECTED && millis() - start < 8000)
                                    delay(500);

                                if (WiFi.status() == WL_CONNECTED)
                                {
                                    Serial.printf("[WiFi] Connected to %s\n", net.ssid.c_str());
                                    connected = true;
                                    break;
                                }
                            }
                        }
                    }

                    // If no known network, try open ones
                    if (!connected)
                    {
                        for (int j = 0; j < n && !connected; j++)
                        {
                            if (WiFi.encryptionType(j) == WIFI_AUTH_OPEN)
                            {
                                Serial.printf("[WiFi] Trying open network: %s\n", WiFi.SSID(j).c_str());
                                WiFi.begin(WiFi.SSID(j).c_str());

                                unsigned long start = millis();
                                while (WiFi.status() != WL_CONNECTED && millis() - start < 5000)
                                    delay(500);

                                if (WiFi.status() == WL_CONNECTED)
                                {
                                    Serial.printf("[WiFi] Connected to open network: %s\n", WiFi.SSID(j).c_str());
                                    connected = true;
                                }
                            }
                        }
                    }

                    if (!connected)
                    {
                        Serial.println("[WiFi] Could not connect to any network.");
                    }

                    if (connected)
                        UserTime::set();
                }
                WiFi.scanDelete();
            }
            delay(15000); // Scan every 15 seconds if disconnected
        }
    }

    void start()
    {
        WiFi.mode(WIFI_STA);
        xTaskCreate(WiFiConnectTask, "WiFiConnectTask", 8192, NULL, 1, &WiFiConnectTaskHandle);
    }
}