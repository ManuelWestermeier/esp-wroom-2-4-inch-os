#include "index.hpp"

namespace UserWiFi
{
    TaskHandle_t WiFiConnectTaskHandle = NULL; // nur EINMAL definieren
    bool hasInternet = false;

    bool hasInternetFn()
    {
        if (WiFi.status() != WL_CONNECTED)
        {
            hasInternet = false;
            return false;
        }

        HTTPClient http;
        http.begin("http://clients3.google.com/generate_204"); // lightweight endpoint
        int httpCode = http.GET();
        http.end();

        hasInternet = httpCode == 204; // returns 204 if Internet is available

        return hasInternet;
    }

    void logAllWifis()
    {
        auto publicWiFiNames = SD_FS::readDir("/public/wifi");
        for (File wifiFile : publicWiFiNames)
        {
            if (!wifiFile.isDirectory())
            {
                String name = wifiFile.name();

                if (name == "README.md")
                    continue;

                // Remove last 5 characters ".wifi"
                if (name.length() > 5)
                {
                    name.remove(name.length() - 5);
                }

                Serial.println("WIFI FOUND: " + fromHex(name) + " | " + wifiFile.readString());
            }
        }
    }

    void WiFiConnectTask(void *)
    {
        esp_task_wdt_delete(NULL); // unregister this task

#if LOG_ALL_WIFIS
        logAllWifis();
#endif

        for (;;)
        {
            if (WiFi.status() != WL_CONNECTED)
            {
                Serial.println("\n[WiFi] Scanning...");
                WiFi.scanDelete();
                int n = WiFi.scanNetworks();

                if (n == 0)
                {
                    Serial.println("[WiFi] No networks found.");
                }
                else
                {
                    bool connected = false;

                    for (int i = 0; i < n && !connected; i++)
                    {
                        String ssid = WiFi.SSID(i);

                        if (LOG_ALL_WIFIS)
                        {
                            Serial.printf("[WiFi] Found: %s (%s)\n",
                                          ssid.c_str(),
                                          WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "open" : "secured");
                        }

                        // Check for known WiFi file
                        String wifiFile = "/public/wifi/" + toHex(ssid) + ".wifi";
                        ENC_FS::Path wifiEncFile = {"wifi", toHex(ssid) + ".wifi"};
                        if (SD_FS::exists(wifiFile))
                        {
                            String password = SD_FS::readFile(wifiFile);

                            Serial.printf("[WiFi] Known network %s, connecting...\n", ssid.c_str());
                            WiFi.begin(ssid.c_str(), password.c_str());

                            if (WiFi.waitForConnectResult(8000) == WL_CONNECTED)
                            {
                                Serial.printf("[WiFi] Connected to %s\n", ssid.c_str());
                                connected = true;
                                break;
                            }
                        }
                        else if (ENC_FS::exists(wifiEncFile))
                        {
                            String password = ENC_FS::readFileString(wifiEncFile);

                            Serial.printf("[WiFi] Enc Known network %s, connecting...\n", ssid.c_str());
                            WiFi.begin(ssid.c_str(), password.c_str());

                            if (WiFi.waitForConnectResult(8000) == WL_CONNECTED)
                            {
                                Serial.printf("[WiFi] Connected to %s\n", ssid.c_str());
                                connected = true;
                                break;
                            }
                        }
                        else if (WiFi.encryptionType(i) == WIFI_AUTH_OPEN)
                        {
                            Serial.printf("[WiFi] Trying open: %s\n", ssid.c_str());
                            WiFi.begin(ssid.c_str());

                            if (WiFi.waitForConnectResult(5000) == WL_CONNECTED)
                            {
                                Serial.printf("[WiFi] Connected to open network: %s\n", ssid.c_str());
                                connected = true;
                                break;
                            }
                        }
                    }

                    if (!connected)
                        Serial.println("[WiFi] Could not connect.");
                    else
                    {
                        // Print the ESP32 IP address
                        Serial.print("ESP32 IP Address: ");
                        Serial.println(WiFi.localIP());

                        if (hasInternetFn())
                            UserTime::set();
                    }
                }
            }

            delay(15000); // Retry every 15s if disconnected
        }
    }

    void start()
    {
        WiFi.mode(WIFI_STA);
        xTaskCreate(WiFiConnectTask, "WiFiConnectTask", 8192, NULL, 1, &WiFiConnectTaskHandle);
    }

    void addPublicWifi(String ssid, String pass)
    {
        SD_FS::writeFile("/public/wifi/" + toHex(ssid) + ".wifi", pass);
    }

    void addPrivateWifi(String ssid, String pass)
    {
        ENC_FS::writeFileString({"wifi", toHex(ssid) + ".wifi"}, pass);
    }
}
