#include "auth.hpp"

#include "../sys-apps/designer.hpp"
#include "../styles/global.hpp"
#include "../fs/enc-fs.hpp"

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
            ENC_FS::init(user, path);
            applyColorPalette();
            return true;
        }

        return false;
    }

    void copyPublicDir(const String &path)
    {
        esp_log_level_set("*", ESP_LOG_NONE);
        auto files = SD_FS::readDirStr(path);

        for (const auto &full : files)
        {
            String fp = full.startsWith("/public/") ? full.substring(7) : full;

            if (SD_FS::isDirectory(full))
            {
                ENC_FS::mkDir(ENC_FS::str2Path(fp));
                copyPublicDir(full); // recurse
            }
            else
            {
                const size_t CHUNK_SIZE = 4096;
                long fileSize = SD_FS::getFileSize(full);
                if (fileSize <= 0)
                    continue;

                ENC_FS::Buffer chunk(CHUNK_SIZE);
                long offset = 0;

                int lastSlash = fp.lastIndexOf('/');
                if (lastSlash >= 0)
                {
                    String parentDir = fp.substring(0, lastSlash);
                    ENC_FS::mkDir(ENC_FS::str2Path(parentDir));
                }

                while (offset < fileSize)
                {
                    size_t bytesToRead = min((long)CHUNK_SIZE, fileSize - offset);
                    if (!SD_FS::readFileBuff(full, offset, bytesToRead, chunk))
                        break;
                    if (!ENC_FS::writeFile(ENC_FS::str2Path(fp), offset, offset + bytesToRead, chunk))
                        break;
                    offset += bytesToRead;
                }
            }
        }
        esp_log_level_set("*", ESP_LOG_INFO); // or ESP_LOG_WARN / ESP_LOG_ERROR
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
        ENC_FS::init(user, authFile);

        // Bildschirm mit Hintergrundfarbe füllen
        Screen::tft.fillScreen(BG);

        // Text-Datum auf Mittelpunkt setzen (horizontal & vertikal)
        Screen::tft.setTextDatum(MC_DATUM);

        // Schriftgröße und Farbe setzen
        Screen::tft.setTextSize(3);
        Screen::tft.setTextColor(TEXT);

        // Cursor auf Bildschirmmitte setzen
        Screen::tft.setCursor(20, 100);

        // Text ausgeben
        Screen::tft.println("Copying Data...");

        // Kurze Pause, damit Text sichtbar bleibt
        delay(1500);

        copyPublicDir();

        // Bildschirm mit Hintergrundfarbe füllen
        Screen::tft.fillScreen(BG);

        // Text-Datum auf Mittelpunkt setzen (horizontal & vertikal)
        Screen::tft.setTextDatum(MC_DATUM);

        // Schriftgröße und Farbe setzen
        Screen::tft.setTextSize(3);

        Screen::tft.setTextColor(TEXT);

        // Cursor auf Bildschirmmitte setzen
        Screen::tft.setCursor(50, 100);

        // Text ausgeben
        Screen::tft.println("Finished...");

        // Kurze Pause, damit Text sichtbar bleibt
        delay(1500);

        applyColorPalette();

        tft.fillScreen(BG);
        tft.setTextColor(TEXT);
        tft.setTextSize(2);
        tft.setCursor(0, 0);

        return true;
    }

    void init()
    {
        using namespace Screen;

        tft.fillScreen(BG);
        tft.setTextColor(TEXT);

        Rect loginBtn{{50, 140 - 30}, {220, 40}};
        Rect createBtn{{50, 190 - 30}, {220, 40}};
        Rect messageArea{{40, 200}, {280, 30}};

#if PRINT_ALL_USERS
        for (auto &f : SD_FS::readDir("/"))
        {
            if (f.isDirectory() && strcmp(f.name(), "System Volume Information") != 0)
                Serial.println("USER: " + String(f.name()));
        }
#endif

        int render = 50;
        String message = "";

        auto drawUI = [&](const String &msg = "")
        {
            tft.setTextColor(TEXT);

            auto time = UserTime::get();
            String hour = String(time.tm_hour);
            String minute = String(time.tm_min);
            if (hour.length() < 2)
                hour = "0" + hour;
            if (minute.length() < 2)
                minute = "0" + minute;

            tft.fillRect(55, 40, 210, 55, BG);
            tft.setTextSize(8);
            tft.setCursor(55, 40);
            tft.print(time.tm_year > 124 ? hour + ":" + minute : "00:00");

            tft.fillRoundRect(loginBtn.pos.x, loginBtn.pos.y, loginBtn.dimensions.x, loginBtn.dimensions.y, 10, PRIMARY);
            tft.fillRoundRect(createBtn.pos.x, createBtn.pos.y, createBtn.dimensions.x, createBtn.dimensions.y, 10, PRIMARY);
            tft.setTextSize(2);

            int d = loginBtn.dimensions.y - 5;
            drawSVGString(SVG::login, loginBtn.pos.x, loginBtn.pos.y + 3, d, d, TEXT);

            tft.setCursor(loginBtn.pos.x + 5 + d, loginBtn.pos.y + 13);
            tft.print("LOGIN");

            d = createBtn.dimensions.y;
            drawSVGString(SVG::signin, createBtn.pos.x, createBtn.pos.y, d, d, TEXT);

            tft.setCursor(createBtn.pos.x + 5 + d, createBtn.pos.y + 13);
            tft.print("CREATE ACCOUNT");

            tft.fillRect(messageArea.pos.x, messageArea.pos.y, messageArea.dimensions.x, messageArea.dimensions.y, BG);
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
                    tft.fillScreen(BG);

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
                    tft.fillScreen(BG);
                    if (pass.isEmpty())
                    {
                        message = "Password required.";
                        drawUI(message);
                        continue;
                    }

                    tft.fillScreen(BG);
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
                    tft.fillScreen(BG);
                    if (user.isEmpty())
                    {
                        message = "Username required.";
                        drawUI(message);
                        continue;
                    }

                    if (exists(user))
                    {
                        message = "Username exists.\n    Try another.";
                        drawUI(message);
                        continue;
                    }

                    String pass = readString("New Password", "");
                    tft.fillScreen(BG);
                    if (pass.isEmpty())
                    {
                        message = "Password required.";
                        drawUI(message);
                        continue;
                    }

                    bool ok = createAccount(user, pass);
                    message = ok ? "Account created!" : "Creation failed!";
                    Serial.println((ok ? "ACCOUNT CREATED: " : "ACCOUNT CREATION FAILED: ") + user);
                    tft.fillScreen(BG);

                    if (ok)
                        return;

                    drawUI(message);
                }
            }

            delay(50);
        }
    }

} // namespace Auth
