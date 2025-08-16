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

    bool login(String user, String pass)
    {
        if (!exitst(user))
            return false;
        String path = "/" + Crypto::HASH::sha256String(user) + "/" + Crypto::HASH::sha256String(user + "\n" + password) + ".auth";

        if (SD_FS::exists(path))
        {
            username = user;
            password = pass;
            return true;
        }
        return false;
    }

    void init()
    {
        using Screen::tft;

        tft.fillScreen(TFT_WHITE);
        tft.setTextColor(TFT_BLACK);

        bool first = true;
        int render = 50;
        while (1)
        {
            if (--render < 0)
            {
                render = 50;

                auto time = UserTime::get();
                if (time.tm_year > 124)
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

                tft.fillRoundRect(55, 130, 210, 60, 15, RGB(240, 240, 255));

                tft.fillRoundRect(70, 145, 225, 30, 5, TFT_WHITE);

                tft.setTextSize(3);

                tft.setCursor(80, 150);
                tft.print("LOGIN");

                tft.setCursor(180, 150);
                tft.print("CREATE ACCOUNT");
            }

            delay(20);
        }
    }
}