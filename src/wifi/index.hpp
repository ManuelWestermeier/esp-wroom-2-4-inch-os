#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoOTA.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"

#include "../fs/index.hpp"
#include "../fs/enc-fs.hpp"
#include "../utils/time.hpp"
#include "../utils/hex.hpp"

#define LOG_ALL_WIFIS true

namespace UserWiFi
{
    extern TaskHandle_t WiFiConnectTaskHandle; // nur Deklaration!
    extern bool hasInternet;

    bool hasInternetFn();
    void logAllWifis(); // Funktions-Signaturen
    void WiFiConnectTask(void *param);
    void start();
    void addPublicWifi(String ssid, String pass);
}
