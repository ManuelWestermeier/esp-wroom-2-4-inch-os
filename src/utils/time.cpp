#include "time.hpp"

namespace UserTime
{
    int isConfigured = -1; // << Nur HIER definieren

    void set(int off)
    {
        if (isConfigured == off)
            return;

        if (WiFi.status() != WL_CONNECTED)
            return;

        if (!UserWiFi::hasInternet)
            return;

        // Start NTP configuration (non-blocking)
        configTime(off, off, "pool.ntp.org");
        isConfigured = off;
    }

    tm get()
    {
        tm timeinfo = {};
        timeinfo.tm_hour = 0;
        timeinfo.tm_min = 0;
        timeinfo.tm_year = 0;

        // Only try to get time if configured and internet is available
        if (isConfigured != -1 &&
            WiFi.status() == WL_CONNECTED &&
            UserWiFi::hasInternet)
        {
            // Non-blocking call: if time isn't ready, returns false
            getLocalTime(&timeinfo, 0); // 0 ms timeout
        }

        return timeinfo;
    }
}
