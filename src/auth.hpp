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

    bool exists(String user)
    {
        String path = "/" + Crypto::HASH::sha256String(user) + "/";
        return SD_FS::exists(path);
    }

    bool login(String user, String pass)
    {
        if (!exists(user))
            return false;

        String path = "/" + Crypto::HASH::sha256String(user) + "/" + Crypto::HASH::sha256String(user + "\n" + pass) + ".auth";
        if (SD_FS::exists(path))
        {
            username = user;
            password = pass;
            return true;
        }
        return false;
    }

    void createAccount(String user, String pass)
    {
        if (exists(user))
            return;

        String userDir = "/" + Crypto::HASH::sha256String(user) + "/";
        SD_FS::createDir(userDir);

        String authFile = userDir + Crypto::HASH::sha256String(user + "\n" + pass) + ".auth";
        SD_FS::writeFile(authFile, "AUTH"); // placeholder

        username = user;
        password = pass;
    }

    void init()
    {
        using namespace Screen;

        tft.fillScreen(TFT_WHITE);
        tft.setTextColor(TFT_BLACK);

        // Define vertical buttons with smaller font
        Rect loginBtn{{60, 140}, {200, 40}}; // x,y pos; w,h
        Rect createBtn{{60, 190}, {200, 40}};

        int render = 50; // control update speed

        while (true)
        {
            if (--render < 0)
            {
                render = 50;

                // Clock display
                auto time = UserTime::get();
                if (time.tm_year > 124)
                {
                    String hour = String(time.tm_hour);
                    String minute = String(time.tm_min);
                    if (minute.length() < 2)
                        minute = "0" + minute;
                    if (hour.length() < 2)
                        hour = "0" + hour;

                    // Clear previous clock
                    tft.fillRect(55, 40, 210, 55, TFT_WHITE);
                    tft.setTextSize(8);
                    tft.setCursor(55, 40);
                    tft.print(hour + ":" + minute);
                }

                // Draw buttons with minimal color
                tft.fillRoundRect(loginBtn.pos.x, loginBtn.pos.y, loginBtn.dimensions.x, loginBtn.dimensions.y, 10, RGB(255, 240, 255));
                tft.fillRoundRect(createBtn.pos.x, createBtn.pos.y, createBtn.dimensions.x, createBtn.dimensions.y, 10, RGB(255, 240, 255));

                // Draw button text
                tft.setTextSize(2); // smaller font
                tft.setCursor(loginBtn.pos.x + 10, loginBtn.pos.y + 10);
                tft.print("LOGIN");

                tft.setCursor(createBtn.pos.x + 10, createBtn.pos.y + 10);
                tft.print("CREATE ACCOUNT");
            }

            // Handle touch
            TouchPos touch = getTouchPos();
            if (touch.clicked)
            {
                Vec point{touch.x, touch.y};

                if (loginBtn.isIn(point))
                {
                    String user = readString("Username", "");
                    String pass = readString("Password", "");
                    if (login(user, pass))
                    {
                        tft.fillScreen(TFT_WHITE);
                        tft.setCursor(50, 100);
                        tft.setTextSize(3);
                        tft.print("Login successful!");
                    }
                    else
                    {
                        tft.fillScreen(TFT_WHITE);
                        tft.setCursor(50, 100);
                        tft.setTextSize(3);
                        tft.print("Login failed!");
                    }
                    delay(1500);
                    tft.fillScreen(TFT_WHITE);
                }
                else if (createBtn.isIn(point))
                {
                    String user = readString("New Username", "");
                    String pass = readString("New Password", "");
                    createAccount(user, pass);

                    tft.fillScreen(TFT_WHITE);
                    tft.setCursor(50, 100);
                    tft.setTextSize(3);
                    tft.print("Account created!");
                    delay(1500);
                    tft.fillScreen(TFT_WHITE);
                }
            }

            delay(50); // slower main loop
        }
    }
}
