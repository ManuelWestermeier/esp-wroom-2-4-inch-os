#pragma once

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

inline void startupCheck()
{
    esp_sleep_wakeup_cause_t reason = esp_sleep_get_wakeup_cause();

    switch (reason)
    {
    case ESP_SLEEP_WAKEUP_EXT0:
        Serial.println("Wakeup durch externes Signal (ext0) auf GPIO0");
        break;
    case ESP_SLEEP_WAKEUP_TIMER:
        Serial.println("Wakeup durch Timer");
        break;
    case ESP_SLEEP_WAKEUP_UNDEFINED:
    default:
        Serial.println("Normaler Start (kein Wakeup)");
        break;
    }
}