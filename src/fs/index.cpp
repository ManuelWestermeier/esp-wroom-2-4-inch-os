#include "fs/index.hpp"

#include "../styles/global.hpp"

namespace SD_FS
{

    bool init(uint8_t csPin)
    {
        // --- Color scheme ---
        const uint16_t BG_ERROR = 0x7800;  // dark red
        const uint16_t BG_WARN = 0xFBE0;   // soft yellow
        const uint16_t BG_INFO = 0x001F;   // dark blue
        const uint16_t TEXT_MAIN = 0xFFFF; // white

        if (!SPIFFS.begin(true))
        {
            Serial.println("⚠️ SPIFFS mount failed");
        }

        // --- SD init failed ---
        if (!SD.begin(csPin))
        {
            Serial.println("❌ SD card initialization failed");

            Screen::tft.fillScreen(BG_ERROR);
            Screen::tft.setCursor(20, 20);
            Screen::tft.setTextColor(TEXT_MAIN);
            Screen::tft.setTextSize(3);

            Screen::tft.println("SD ERROR");
            Screen::tft.println();

            Screen::tft.setTextSize(2);
            Screen::tft.println("No SD card detected.");
            Screen::tft.println("Insert a SD card");
            Screen::tft.println("formatted as FAT32.");

            delay(1500);
            return init(csPin);
        }

        // --- Root not accessible ---
        if (!SD.exists("/"))
        {
            Serial.println("⚠️ SD mounted, but root not accessible");

            Screen::tft.fillScreen(BG_WARN);
            Screen::tft.setCursor(20, 20);
            Screen::tft.setTextColor(TEXT_MAIN);
            Screen::tft.setTextSize(3);

            Screen::tft.println("SD WARNING");
            Screen::tft.println();

            Screen::tft.setTextSize(2);
            Screen::tft.println("SD detected but unusable.");
            Screen::tft.println("Please FORMAT the");
            Screen::tft.println("SD card as FAT32.");

            delay(1500);
            return init(csPin);
        }

        // --- Success (optional but good UX) ---
        Screen::tft.fillScreen(BG_INFO);
        Screen::tft.setCursor(20, 20);
        Screen::tft.setTextColor(TEXT_MAIN);
        Screen::tft.setTextSize(3);

        Screen::tft.println("SD OK");
        Screen::tft.println();

        Screen::tft.setTextSize(2);
        Screen::tft.println("SD card mounted");
        Screen::tft.println("successfully.");

        delay(500);
        return true;
    }

    void exit() {}

    bool writeFile(const String &path, const String &content)
    {
        File file = SD.open(path, FILE_WRITE);
        if (!file)
        {
            Serial.printf("❌ writeFile: can't open %s\n", path.c_str());
            return false;
        }
        file.print(content);
        file.close();
        return true;
    }

    bool appendFile(const String &path, const String &content)
    {
        File file = SD.open(path, FILE_APPEND);
        if (!file)
        {
            Serial.printf("❌ appendFile: can't open %s\n", path.c_str());
            return false;
        }
        file.print(content);
        file.close();
        return true;
    }

    String readFile(const String &path)
    {
        File file = SD.open(path);
        String result = "";

        if (!file)
        {
            Serial.printf("❌ readFile: can't open %s\n", path.c_str());
            return result;
        }

        while (file.available())
            result += (char)file.read();

        file.close();

        return result;
    }

    bool deleteFile(const String &path)
    {
        if (!SD.remove(path))
        {
            Serial.printf("❌ deleteFile failed: %s\n", path.c_str());
            return false;
        }
        return true;
    }

    bool renameFile(const String &from, const String &to)
    {
        if (!SD.rename(from, to))
        {
            Serial.printf("❌ renameFile failed: %s → %s\n", from.c_str(), to.c_str());
            return false;
        }
        return true;
    }

    vector<File> readDir(const String &path)
    {
        vector<File> out;
        File root = SD.open(path);
        if (!root || !root.isDirectory())
        {
            Serial.printf("❌ Not a dir: %s\n", path.c_str());
            return {};
        }

        File file = root.openNextFile();
        while (file)
        {
            out.push_back(file);
            file = root.openNextFile();
        }

        return out;
    }

    bool createDir(const String &path)
    {
        if (!SD.mkdir(path))
        {
            Serial.printf("❌ mkdir failed: %s\n", path.c_str());
            return false;
        }
        return true;
    }

    bool deleteDir(const String &path)
    {
        File dir = SD.open(path);
        if (!dir || !dir.isDirectory())
        {
            Serial.printf("❌ Not a dir: %s\n", path.c_str());
            return false;
        }

        File file = dir.openNextFile();
        while (file)
        {
            String filePath = String(path) + "/" + file.name();
            if (file.isDirectory())
                deleteDir(filePath);
            else
                SD.remove(filePath);
            file = dir.openNextFile();
        }

        if (!SD.rmdir(path))
            Serial.printf("❌ deleteDir failed: %s\n", path.c_str());

        return true;
    }

    bool exists(const String &path) { return SD.exists(path); }

    bool isDirectory(const String &path)
    {
        File f = SD.open(path);
        bool r = f && f.isDirectory();
        if (f)
            f.close();
        return r;
    }

    size_t fileSize(const String &path)
    {
        File f = SD.open(path);
        if (!f)
            return 0;
        size_t s = f.size();
        f.close();
        return s;
    }

    time_t getModifiedTime(const String &path)
    {
        File f = SD.open(path);
        if (!f)
            return 0;
        time_t t = f.getLastWrite();
        f.close();
        return t;
    }

    void getFileInfo(const String &path)
    {
        File f = SD.open(path);
        if (!f)
            Serial.printf("❌ Datei %s nicht gefunden.\n", path.c_str());
        if (f)
            f.close();
    }

    void copyFileFromSPIFFS(const char *spiffsPath, const char *sdPath)
    {
        File f = SPIFFS.open(spiffsPath, "r");
        if (!f)
        {
            Serial.printf("Fehler beim Öffnen von %s in SPIFFS\n", spiffsPath);
            return;
        }

        String content = f.readString(); // ganzen Inhalt als String einlesen
        f.close();

        writeFile(sdPath, content); // auf SD schreiben
    }

    void lsDirSerial(const String &path)
    {
        Serial.println("--- READ DIR ---");
        auto files = readDir(path);
        for (const auto &f : files)
        {
            Serial.println(f.path());
        }
        Serial.println("--- READ DIR END ---");
    }

    void deleteFoldersXV(const String &path, const std::vector<String> &except)
    {
        auto files = readDir(path);

        for (auto f : files)
        {
            if (f.isDirectory())
            {
                String dirName = f.name();

                // check ob dieser Ordner in except ist
                bool skip = false;
                for (const auto &ex : except)
                {
                    if (dirName.equalsIgnoreCase(ex))
                    {
                        skip = true;
                        break;
                    }
                }

                if (!skip)
                {
                    deleteDir(f.path());
                }
            }
        }
    }

    uint64_t getCardSize() { return SD.cardSize(); }
    uint64_t getUsedBytes() { return SD.usedBytes(); }
    uint64_t getFreeBytes() { return getCardSize() - getUsedBytes(); }
    void getUsageSummary() {}
}
