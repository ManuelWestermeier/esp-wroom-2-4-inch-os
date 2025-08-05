#pragma once

#include <WiFi.h>
#include <time.h>

namespace UserTime
{
    int isConfigured = -1;

    void set(int off = 3600)
    {

        if (isConfigured == off)
            return;
        if (WiFi.status() != WL_CONNECTED)
            return;

        configTime(off, off, "pool.ntp.org"); // 3600 = +1h, 3600 DST offset
        isConfigured = off;
    }

    tm get()
    {
        tm timeinfo = {}; // initialisiere mit 0
        timeinfo.tm_hour = 0;
        timeinfo.tm_min = 0;
        getLocalTime(&timeinfo); // ignoriert Rückgabewert
        return timeinfo;         // könnte leer sein, wenn fehlgeschlagen
    }
}