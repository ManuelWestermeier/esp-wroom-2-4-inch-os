#include "auth.hpp"

namespace Auth
{
    String username = "";
    String password = "";

    bool exists(const String &user)
    {
        if (user.isEmpty())
            return false;
        String path = "/" + Crypto::HASH::sha256String(user);
        return SD_FS::exists(path);
    }

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
            return false;

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
        Rect messageArea{{20, 250}, {280, 30}};

        for (auto &f : SD_FS::readDir("/"))
        {
            if (f.isDirectory() && strcmp(f.name(), "System Volume Information") != 0)
                Serial.println("USER: " + String(f.name()));
        }

        int render = 50;
        String message = "";

        auto drawUI = [&](const String &msg = "")
        {
            auto time = UserTime::get();
            String hour = String(time.tm_hour);
            String minute = String(time.tm_min);
            if (hour.length() < 2)
                hour = "0" + hour;
            if (minute.length() < 2)
                minute = "0" + minute;

            tft.fillRect(55, 40, 210, 55, TFT_WHITE);
            tft.setTextSize(8);
            tft.setCursor(55, 40);
            tft.print(time.tm_year > 124 ? hour + ":" + minute : ".....");

            tft.fillRoundRect(loginBtn.pos.x, loginBtn.pos.y, loginBtn.dimensions.x, loginBtn.dimensions.y, 10, RGB(255, 240, 255));
            tft.fillRoundRect(createBtn.pos.x, createBtn.pos.y, createBtn.dimensions.x, createBtn.dimensions.y, 10, RGB(255, 240, 255));
            tft.setTextSize(2);
            tft.setCursor(loginBtn.pos.x + 10, loginBtn.pos.y + 10);
            tft.print("LOGIN");
            tft.setCursor(createBtn.pos.x + 10, createBtn.pos.y + 10);
            tft.print("CREATE ACCOUNT");

            tft.fillRect(messageArea.pos.x, messageArea.pos.y, messageArea.dimensions.x, messageArea.dimensions.y, TFT_WHITE);
            tft.setTextSize(2);
            tft.setCursor(messageArea.pos.x, messageArea.pos.y + 5);
            tft.print(msg);
        };

        drawUI();

        while (true)
        {
            if (--render < 0)
            {
                render = 50;
                drawUI(message);
            }

            TouchPos touch = getTouchPos();
            if (touch.clicked)
            {
                Vec point{touch.x, touch.y};

                if (loginBtn.isIn(point))
                {
                    String user = readString("Username", "");
                    if (user.isEmpty())
                    {
                        message = "Username required.";
                        continue;
                    }

                    String pass = readString("Password", "");
                    if (pass.isEmpty())
                    {
                        message = "Password required.";
                        continue;
                    }

                    tft.fillScreen(TFT_WHITE);
                    bool ok = login(user, pass);
                    tft.setCursor(50, 100);
                    tft.setTextSize(3);
                    message = ok ? "Login successful!" : "Login failed!";
                    tft.print(message);
                    Serial.println((ok ? "LOGIN SUCCESS: " : "LOGIN FAILED: ") + user);
                    delay(1500);
                    tft.fillScreen(TFT_WHITE);
                    if (ok)
                        return;
                    drawUI(message);
                }

                else if (createBtn.isIn(point))
                {
                    String user = readString("New Username", "");
                    if (user.isEmpty())
                    {
                        message = "Username required.";
                        continue;
                    }

                    if (exists(user))
                    {
                        message = "Username exists. Try another.";
                        drawUI(message);
                        delay(1500);
                        continue;
                    }

                    String pass = readString("New Password", "");
                    if (pass.isEmpty())
                    {
                        message = "Password required.";
                        continue;
                    }

                    bool ok = createAccount(user, pass);
                    tft.fillScreen(TFT_WHITE);
                    tft.setCursor(50, 100);
                    tft.setTextSize(3);
                    message = ok ? "Account created!" : "Creation failed!";
                    tft.print(message);
                    Serial.println((ok ? "ACCOUNT CREATED: " : "ACCOUNT CREATION FAILED: ") + user);
                    delay(1500);
                    tft.fillScreen(TFT_WHITE);
                    if (ok)
                        return;
                    drawUI(message);
                }
            }

            delay(50);
        }
    }

} // namespace Auth
