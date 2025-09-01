#include "auth.hpp"

namespace Auth
{
    String username = "";
    String name = "";
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
            username = Crypto::HASH::sha256String(user);
            password = Crypto::HASH::sha256String(pass);
            name = user;
            return true;
        }

        return false;
    }

    void copyPublicDir(String path)
    {
        auto files = SD_FS::readDir(path);

        for (auto &f : files) // besser const &
        {
            String fp = String(f.path()).substring(7); // remove "/public"

            if (f.isDirectory())
            {
                ENC_FS::mkDir(ENC_FS::str2Path(fp));
                copyPublicDir(f.path()); // <-- WICHTIG: ins Unterverzeichnis gehen
            }
            else
            {
                ENC_FS::Buffer buff;
                auto file = SD.open(f.path(), "r");

                if (!file)
                    continue;

                buff.resize(file.size());
                file.read(buff.data(), buff.size());
                file.close();

                ENC_FS::writeFile(
                    ENC_FS::str2Path(fp), 0, 0, buff);
            }
        }
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
        userDir += "/";

        String authFile = userDir + Crypto::HASH::sha256String(user + "\n" + pass) + ".auth";
        if (!SD_FS::writeFile(authFile, "AUTH"))
            return false;

        username = Crypto::HASH::sha256String(user);
        name = user;
        password = Crypto::HASH::sha256String(pass);

        copyPublicDir();

        return true;
    }

    void init()
    {
        using namespace Screen;

        tft.fillScreen(TFT_WHITE);
        tft.setTextColor(TFT_BLACK);

        Rect loginBtn{{60, 140 - 30}, {200, 40}};
        Rect createBtn{{60, 190 - 30}, {200, 40}};
        Rect messageArea{{40, 200}, {280, 30}};

        for (auto &f : SD_FS::readDir("/"))
        {
            if (f.isDirectory() && strcmp(f.name(), "System Volume Information") != 0)
                Serial.println("USER: " + String(f.name()));
        }

        int render = 50;
        String message = "";

        auto drawUI = [&](const String &msg = "")
        {
            tft.setTextColor(TFT_BLACK);

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

                while (getTouchPos().clicked)
                    delay(5);

                if (loginBtn.isIn(point))
                {
                    String user = readString("Username", "");
                    tft.fillScreen(TFT_WHITE);

                    if (user.isEmpty())
                    {
                        message = "Username required.";
                        drawUI(message);
                        continue;
                    }

                    if (!exists(user))
                    {
                        message = "Username not exits.";
                        drawUI(message);
                        continue;
                    }

                    String pass = readString("Password", "");
                    tft.fillScreen(TFT_WHITE);
                    if (pass.isEmpty())
                    {
                        message = "Password required.";
                        drawUI(message);
                        continue;
                    }

                    tft.fillScreen(TFT_WHITE);
                    bool ok = login(user, pass);
                    message = ok ? "Login successful!" : "Login failed!";
                    Serial.println((ok ? "LOGIN SUCCESS: " : "LOGIN FAILED: ") + user);

                    if (ok)
                        return;

                    drawUI(message);
                }

                else if (createBtn.isIn(point))
                {
                    String user = readString("New Username", "");
                    tft.fillScreen(TFT_WHITE);
                    if (user.isEmpty())
                    {
                        message = "Username required.";
                        drawUI(message);
                        continue;
                    }

                    if (exists(user))
                    {
                        message = "Username exists. Try another.";
                        drawUI(message);
                        continue;
                    }

                    String pass = readString("New Password", "");
                    tft.fillScreen(TFT_WHITE);
                    if (pass.isEmpty())
                    {
                        message = "Password required.";
                        drawUI(message);
                        continue;
                    }

                    bool ok = createAccount(user, pass);
                    message = ok ? "Account created!" : "Creation failed!";
                    Serial.println((ok ? "ACCOUNT CREATED: " : "ACCOUNT CREATION FAILED: ") + user);
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
