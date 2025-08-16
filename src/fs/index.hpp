#pragma once

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <vector>

using std::vector;

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
        if (!file)
        {
            Serial.printf("❌ readFile: can't open %s\n", path.c_str());
            return "";
        }

        String result;
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

    vector<File> readDir(const String &path, uint8_t levels = 1)
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
        {
            Serial.printf("❌ Datei %s nicht gefunden.\n", path.c_str());
            return;
        }
        f.close();
    }

    uint64_t getCardSize() { return SD.cardSize(); }

    uint64_t getUsedBytes() { return SD.usedBytes(); }

    uint64_t getFreeBytes() { return getCardSize() - getUsedBytes(); }

    void getUsageSummary() {}
}
