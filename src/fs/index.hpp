#pragma once

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>

namespace SD_FS
{
    bool init(uint8_t csPin = 5)
    {
        if (!SD.begin(csPin))
        {
            Serial.println("❌ SD card initialization failed!");
            return false;
        }

        if (!SD.exists("/"))
        {
            Serial.println("⚠️ SD mounted, but root not accessible");
            return false;
        }

        Serial.println("✅ SD card initialized.");
        uint8_t cardType = SD.cardType();
        Serial.print("📇 Card Type: ");
        if (cardType == CARD_MMC)
            Serial.println("MMC");
        else if (cardType == CARD_SD)
            Serial.println("SDSC");
        else if (cardType == CARD_SDHC)
            Serial.println("SDHC");
        else
            Serial.println("Unknown");

        uint64_t sizeMB = SD.cardSize() / (1024 * 1024);
        Serial.printf("💾 Card Size: %llu MB\n", sizeMB);

        return true;
    }

    void exit()
    {
        Serial.println("ℹ️ exit() called (noop).");
    }

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
        Serial.printf("✅ Wrote to %s\n", path.c_str());
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
        Serial.printf("✅ Appended to %s\n", path.c_str());
        return true;
    }

    String readFile(const String &path)
    {
        File file = SD.open(path);
        if (!file)
        {
            Serial.printf("❌ readFile: can't open %s\n", path.c_str());
            return "";
        }

        String result;
        while (file.available())
            result += (char)file.read();
        file.close();
        Serial.printf("✅ Read from %s\n", path.c_str());
        return result;
    }

    bool deleteFile(const String &path)
    {
        if (SD.remove(path))
        {
            Serial.printf("🗑️ Deleted: %s\n", path.c_str());
            return true;
        }
        Serial.printf("❌ deleteFile failed: %s\n", path.c_str());
        return false;
    }

    bool renameFile(const String &from, const String &to)
    {
        if (SD.rename(from, to))
        {
            Serial.printf("✏️ Renamed: %s → %s\n", from.c_str(), to.c_str());
            return true;
        }
        Serial.printf("❌ renameFile failed\n");
        return false;
    }

    void readDir(const String &path, uint8_t levels = 1)
    {
        File root = SD.open(path);
        if (!root || !root.isDirectory())
        {
            Serial.printf("❌ Not a dir: %s\n", path.c_str());
            return;
        }

        File file = root.openNextFile();
        while (file)
        {
            Serial.print(file.isDirectory() ? "DIR : " : "FILE: ");
            Serial.print(file.name());
            if (!file.isDirectory())
            {
                Serial.print("\tSIZE: ");
                Serial.println(file.size());
            }
            else
            {
                Serial.println();
                if (levels > 0)
                    readDir(file.name(), levels - 1);
            }
            file = root.openNextFile();
        }
    }

    bool createDir(const String &path)
    {
        if (SD.mkdir(path))
        {
            Serial.printf("📁 Created dir: %s\n", path.c_str());
            return true;
        }
        Serial.printf("❌ mkdir failed: %s\n", path.c_str());
        return false;
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
        return SD.rmdir(path);
    }

    bool exists(const String &path)
    {
        return SD.exists(path);
    }

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
        {
            Serial.printf("❌ Datei %s nicht gefunden.\n", path.c_str());
            return;
        }

        Serial.printf("📄 Datei: %s\n", f.name());
        Serial.printf("📦 Größe: %zu Bytes\n", f.size());
        Serial.printf("🕒 Letzte Änderung: %lu\n", f.getLastWrite());
        f.close();
    }

    uint64_t getCardSize()
    {
        return SD.cardSize();
    }

    uint64_t getUsedBytes()
    {
        return SD.usedBytes();
    }

    uint64_t getFreeBytes()
    {
        return getCardSize() - getUsedBytes();
    }

    void getUsageSummary()
    {
        uint64_t total = getCardSize();
        uint64_t used = getUsedBytes();
        uint64_t free = total - used;
        float usedPercent = (total > 0) ? (used * 100.0 / total) : 0;

        Serial.printf("Gesamtgröße: %.2f MB\n", total / 1024.0 / 1024.0);
        Serial.printf("Belegt: %.2f MB\n", used / 1024.0 / 1024.0);
        Serial.printf("Frei: %.2f MB\n", free / 1024.0 / 1024.0);
        Serial.printf("Belegt (%%): %.2f%%\n", usedPercent);
    }
}
