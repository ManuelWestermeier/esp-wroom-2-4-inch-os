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

        while (1)
        {
            auto time = UserTime::get();
            if (time.tm_year != 0)
            {
                String hour = String(time.tm_hour);
                String minute = String(time.tm_min);
                if (minute.length() < 2)
                    minute = "0" + minute;
                if (hour.length() < 2)
                    hour = "0" + hour;

                tft.fillRect(30, 30, 150, 50, TFT_WHITE);
                tft.setTextSize(7);
                tft.setCursor(100, 30);
                tft.print(hour + ":" + minute);
            }

            delay(20);
        }
    }
}