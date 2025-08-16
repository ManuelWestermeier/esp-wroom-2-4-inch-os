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

    // Check if a user exists by hashed directory
    bool exists(const String &user)
    {
        if (user.isEmpty())
            return false;
        String path = "/" + Crypto::HASH::sha256String(user);
        return SD_FS::exists(path);
    }

    // Attempt login with username and password
    bool login(const String &user, const String &pass)
    {
        if (user.isEmpty() || pass.isEmpty())
            return false;
        if (!exists(user))
            return false;

        String path = "/" + Crypto::HASH::sha256String(user) + "/" +
                      Crypto::HASH::sha256String(user + "\n" + pass) + ".auth";
        if (SD_FS::exists(path))
        {
            username = user;
            password = pass;
            return true;
        }
        return false;
    }

    // Create new account, returns false if user already exists
    bool createAccount(const String &user, const String &pass)
    {
        if (user.isEmpty() || pass.isEmpty())
            return false;
        if (exists(user))
            return false;

        String userDir = "/" + Crypto::HASH::sha256String(user);
        if (!SD_FS::createDir(userDir))
            return false;

        String authFile = userDir + "/" + Crypto::HASH::sha256String(user + "\n" + pass) + ".auth";
        if (!SD_FS::writeFile(authFile, "AUTH"))
            return false; // store a placeholder

        username = user;
        password = pass;
        return true;
    }

    // Main login/create account screen
    void init()
    {
        using namespace Screen;

        tft.fillScreen(TFT_WHITE);
        tft.setTextColor(TFT_BLACK);

        Rect loginBtn{{60, 140}, {200, 40}};
        Rect createBtn{{60, 190}, {200, 40}};

        // Debug: list existing users
        for (auto &f : SD_FS::readDir("/"))
        {
            if (f.isDirectory() && strcmp(f.name(), "System Volume Information") != 0)
                Serial.println("USER: " + String(f.name()));
        }

        int render = 50;

        while (true)
        {
            if (--render < 0)
            {
                render = 50;

                // Display clock
                auto time = UserTime::get();
                String hour = String(time.tm_hour);
                if (hour.length() < 2)
                    hour = "0" + hour;
                String minute = String(time.tm_min);
                if (minute.length() < 2)
                    minute = "0" + minute;

                tft.fillRect(55, 40, 210, 55, TFT_WHITE);
                tft.setTextSize(8);
                tft.setCursor(55, 40);
                tft.print(time.tm_year > 124 ? hour + ":" + minute : ".....");

                // Draw buttons
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

                // LOGIN FLOW
                if (loginBtn.isIn(point))
                {
                    String user = readString("Username", "");
                    String pass = readString("Password", "");
                    tft.fillScreen(TFT_WHITE);

                    bool ok = login(user, pass);
                    tft.setCursor(50, 100);
                    tft.setTextSize(3);
                    if (ok)
                    {
                        tft.print("Login successful!");
                        Serial.println("LOGIN SUCCESS: " + user);
                    }
                    else
                    {
                        tft.print("Login failed!");
                        Serial.println("LOGIN FAILED: " + user);
                    }

                    delay(1500);
                    tft.fillScreen(TFT_WHITE);

                    if (ok)
                        return;
                }
                // CREATE ACCOUNT FLOW
                else if (createBtn.isIn(point))
                {
                    String user = readString("New Username", "");
                    if (user.isEmpty())
                        continue;

                    tft.fillScreen(TFT_WHITE);

                    if (exists(user))
                    {
                        // Confirm overwrite
                        tft.setCursor(20, 80);
                        tft.setTextSize(2);
                        tft.println("Username exists!");
                        tft.println("Try an other name!");
                        delay(1500);
                    }
                    else
                    {
                        String pass = readString("New Password", "");
                        bool ok = !pass.isEmpty() && createAccount(user, pass);
                        tft.setCursor(50, 100);
                        tft.setTextSize(3);
                        if (ok)
                        {
                            tft.print("Account created!");
                            Serial.println("ACCOUNT CREATED: " + user);
                        }
                        else
                        {
                            tft.print("Creation failed!");
                            Serial.println("ACCOUNT CREATION FAILED: " + user);
                        }
                        delay(1500);
                        tft.fillScreen(TFT_WHITE);
                        if (ok)
                            return;
                    }
                }
            }
            delay(50);
        }
    }
}
