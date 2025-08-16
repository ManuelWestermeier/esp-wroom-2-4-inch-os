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

    bool createAccount(String user, String pass)
    {
        if (exists(user))
            return false;

        String userDir = "/" + Crypto::HASH::sha256String(user);
        bool SD_FS::createDir(userDir);

        String authFile = userDir + "/" + Crypto::HASH::sha256String(user + "\n" + pass) + ".auth";
        bool SD_FS::writeFile(authFile, "AUTH"); // placeholder

        username = user;
        password = pass;

        return true;
    }

    void init()
    {
        using namespace Screen;

        tft.fillScreen(TFT_WHITE);
        tft.setTextColor(TFT_BLACK);

        Rect loginBtn{{60, 140}, {200, 40}};
        Rect createBtn{{60, 190}, {200, 40}};

        for (const auto f : SD_FS::readDir("/"))
            if (f.isDirectory() && strcmp(f.name(), "System Volume Information") != 0)
                Serial.println("USER: " + f.name());

        int render = 50;

        while (true)
        {
            if (--render < 0)
            {
                render = 50;

                // Clock display
                auto time = UserTime::get();
                String hour = String(time.tm_hour);
                String minute = String(time.tm_min);
                if (minute.length() < 2)
                    minute = "0" + minute;
                if (hour.length() < 2)
                    hour = "0" + hour;

                tft.fillRect(55, 40, 210, 55, TFT_WHITE);
                tft.setTextSize(8);
                tft.setCursor(55, 40);
                tft.print(time.tm_year > 124 ? hour + ":" + minute : "..:..");

                // Buttons
                tft.fillRoundRect(loginBtn.pos.x, loginBtn.pos.y, loginBtn.dimensions.x, loginBtn.dimensions.y, 10, RGB(255, 240, 255));
                tft.fillRoundRect(createBtn.pos.x, createBtn.pos.y, createBtn.dimensions.x, createBtn.dimensions.y, 10, RGB(255, 240, 255));

                tft.setTextSize(2);
                tft.setCursor(loginBtn.pos.x + 10, loginBtn.pos.y + 10);
                tft.print("LOGIN");
                tft.setCursor(createBtn.pos.x + 10, createBtn.pos.y + 10);
                tft.print("CREATE ACCOUNT");
            }

            TouchPos touch = getTouchPos();
            if (touch.clicked)
            {
                Vec point{touch.x, touch.y};

                if (loginBtn.isIn(point))
                {
                    String user = readString("Username", "");
                    String pass = readString("Password", "");
                    tft.fillScreen(TFT_WHITE);

                    if (login(user, pass))
                    {
                        tft.setCursor(50, 100);
                        tft.setTextSize(3);
                        tft.print("Login successful!");
                    }
                    else
                    {
                        tft.setCursor(50, 100);
                        tft.setTextSize(3);
                        tft.print("Login failed! User/pass incorrect.");
                    }
                    delay(1500);
                    tft.fillScreen(TFT_WHITE);
                }
                else if (createBtn.isIn(point))
                {
                    String user = readString("New Username", "");
                    if (exists(user))
                    {
                        // Ask user to confirm overwriting/fallback
                        tft.fillScreen(TFT_WHITE);
                        tft.setCursor(20, 80);
                        tft.setTextSize(2);
                        tft.print("Username exists! Continue?");

                        // Yes/No buttons
                        Rect yesBtn{{50, 140}, {80, 40}};
                        Rect noBtn{{170, 140}, {80, 40}};
                        tft.fillRoundRect(yesBtn.pos.x, yesBtn.pos.y, yesBtn.dimensions.x, yesBtn.dimensions.y, 10, RGB(200, 255, 200));
                        tft.fillRoundRect(noBtn.pos.x, noBtn.pos.y, noBtn.dimensions.x, noBtn.dimensions.y, 10, RGB(255, 200, 200));
                        tft.setCursor(yesBtn.pos.x + 10, yesBtn.pos.y + 10);
                        tft.print("YES");
                        tft.setCursor(noBtn.pos.x + 10, noBtn.pos.y + 10);
                        tft.print("NO");

                        while (true)
                        {
                            TouchPos t = getTouchPos();
                            if (t.clicked)
                            {
                                Vec p{t.x, t.y};
                                if (yesBtn.isIn(p))
                                {
                                    String pass = readString("New Password", "");
                                    createAccount(user, pass);
                                    tft.fillScreen(TFT_WHITE);
                                    tft.setCursor(50, 100);
                                    tft.setTextSize(3);
                                    tft.print("Account updated!");
                                    delay(1500);
                                    tft.fillScreen(TFT_WHITE);
                                    break;
                                }
                                else if (noBtn.isIn(p))
                                {
                                    tft.fillScreen(TFT_WHITE);
                                    tft.setCursor(50, 100);
                                    tft.setTextSize(3);
                                    tft.print("Action cancelled.");
                                    delay(1500);
                                    tft.fillScreen(TFT_WHITE);
                                    break;
                                }
                            }
                            delay(50);
                        }
                    }
                    else
                    {
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
            }

            delay(50);
        }
    }
}
