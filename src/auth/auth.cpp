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

        for (const auto f : SD_FS::readDir("/" + Crypto::HASH::sha256String(user)))
            Serial.println(String("FILE: ") + user + ": " + f.name());

        if (SD_FS::exists(path))
        {
            username = Crypto::HASH::sha256String(user);
            password = Crypto::HASH::sha256String(pass);
            return true;
        }
        return false;
    }

    // Helper: convert String to vector<uint8_t>
    std::vector<uint8_t> stringToBytes(const String &s)
    {
        return std::vector<uint8_t>(s.begin(), s.end());
    }

    // Helper: convert vector<uint8_t> to hex string
    String bytesToHexString(const std::vector<uint8_t> &data)
    {
        String hex;
        for (auto b : data)
        {
            if (b < 16)
                hex += "0";
            hex += String(b, HEX);
        }
        return hex;
    }

    // Helper: read file as bytes
    std::vector<uint8_t> readFileBytes(const String &path)
    {
        String content = SD_FS::readFile(path);
        return stringToBytes(content);
    }

    // Helper: separate files and directories
    void listDir(const String &path, std::vector<String> &files, std::vector<String> &dirs)
    {
        auto entries = SD_FS::readDir(path);
        for (auto &f : entries)
        {
            if (f.isDirectory())
                dirs.push_back(f.name());
            else
                files.push_back(f.name());
        }
    }

    bool copyAndEncryptDir(const String &srcDir, const String &dstDir, const std::vector<uint8_t> &key)
    {
        std::vector<String> files, dirs;
        listDir(srcDir, files, dirs);

        // Copy files
        for (auto &file : files)
        {
            String srcFile = srcDir + "/" + file;
            std::vector<uint8_t> fileBytes = readFileBytes(srcFile);

            std::vector<uint8_t> encNameBytes = Crypto::AES::encrypt(stringToBytes(file), key);
            String encFileName = bytesToHexString(encNameBytes);
            String dstFile = dstDir + "/" + encFileName;

            if (!SD_FS::writeFile(dstFile, String((const char *)fileBytes.data(), fileBytes.size())))
                return false;
        }

        // Recurse into directories
        for (auto &subdir : dirs)
        {
            std::vector<uint8_t> encNameBytes = Crypto::AES::encrypt(stringToBytes(subdir), key);
            String encSubdir = bytesToHexString(encNameBytes);
            String dstSubdir = dstDir + "/" + encSubdir;

            if (!SD_FS::createDir(dstSubdir))
                return false;

            if (!copyAndEncryptDir(srcDir + "/" + subdir, dstSubdir, key))
                return false;
        }

        return true;
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

        // AES key for file/dir name encryption
        std::vector<uint8_t> key = stringToBytes(Crypto::HASH::sha256String(user + pass));

        // Copy and encrypt /public/
        if (!copyAndEncryptDir("/public", userDir, key))
            return false;

        username = Crypto::HASH::sha256String(user);
        password = Crypto::HASH::sha256String(pass);
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
