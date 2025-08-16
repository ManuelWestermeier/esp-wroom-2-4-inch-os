#pragma once

#include <Arduino.h>

#include "utils/crypto.hpp"
#include "fs/index.hpp"
#include "io/read-string.hpp"
#include "screen/index.hpp"
#include "utils/rect.hpp"
#include "utils/time.hpp"

namespace Auth
{
    String username = "";
    String password = "";

    bool exitst(String user)
    {
        String path = "/" + Crypto::HASH::sha256String(user) + "/";
        return SD_FS::exists(path);
    }

    void init()
    {
        using Screen::tft;

        tft.fillScreen(TFT_WHITE);
        tft.setTextColor(TFT_BLACK);

        bool first = true;
        while (1)
        {
            auto time = UserTime::get();
            if (time.tm_year != 0 && (time.tm_sec % 10 == 0 || first))
            {
                String hour = String(time.tm_hour);
                String minute = String(time.tm_min);
                if (minute.length() < 2)
                    minute = "0" + minute;
                if (hour.length() < 2)
                    hour = "0" + hour;

                // Clear the background of the old time completely
                tft.fillRect(55, 40, 210, 55, TFT_WHITE); // width & height large enough for all text
                tft.setTextSize(8);
                tft.setCursor(55, 40);
                tft.print(hour + ":" + minute);
            }

            first = false;
            delay(20);
        }
    }
}