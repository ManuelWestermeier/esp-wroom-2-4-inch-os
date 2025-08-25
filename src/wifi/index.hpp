#pragma once

#include <Arduino.h>
#include <WiFi.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"

#include "../fs/index.hpp"
#include "../utils/time.hpp"

#define LOG_ALL_WIFIS true

namespace UserWiFi
{
    extern TaskHandle_t WiFiConnectTaskHandle; // nur Deklaration!

    void logAllWifis(); // Funktions-Signaturen
    void WiFiConnectTask(void *param);
    void start();
}
