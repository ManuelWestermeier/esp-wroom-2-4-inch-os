#pragma once

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <vector>

#include "../screen/index.hpp"

using std::vector;

namespace SD_FS
{
    using Buffer = std::vector<uint8_t>;

    bool init(uint8_t csPin = 5);
    void exit();

    bool writeFile(const String &path, const String &content);
    bool appendFile(const String &path, const String &content);
    String readFile(const String &path);
    bool readFileBuff(const String &path, long offset, size_t length, Buffer &buffer);

    bool deleteFile(const String &path);
    bool renameFile(const String &from, const String &to);

    vector<File> readDir(const String &path);
    vector<String> readDirStr(const String &path);
    void forEachFile(const String &path, std::function<void(const String &name, bool isDir)> cb);
    bool createDir(const String &path);
    bool deleteDir(const String &path);
    void lsDirSerial(const String &path);

    bool exists(const String &path);
    bool isDirectory(const String &path);
    size_t fileSize(const String &path);
    time_t getModifiedTime(const String &path);
    void getFileInfo(const String &path);
    long getFileSize(const String &path);

    uint64_t getCardSize();
    uint64_t getUsedBytes();
    uint64_t getFreeBytes();
    void getUsageSummary();

    void deleteFoldersXV(const String &path, const std::vector<String> &except);
    void copyFileFromSPIFFS(const char *spiffsPath, const char *sdPath);
}
