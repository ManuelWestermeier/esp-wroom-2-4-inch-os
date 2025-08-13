#include "time.hpp"
#include <WiFi.h>
#include <time.h>

namespace UserTime
{
    int isConfigured = -1; // << Nur HIER definieren

    void set(int off)
    {
        if (isConfigured == off)
            return;
        if (WiFi.status() != WL_CONNECTED)
            return;

        configTime(off, off, "pool.ntp.org");
        isConfigured = off;
    }

    tm get()
    {
        tm timeinfo = {};
        timeinfo.tm_hour = 0;
        timeinfo.tm_min = 0;
        timeinfo.tm_year = 0;
        getLocalTime(&timeinfo);
        return timeinfo;
    }
}
