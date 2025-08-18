#pragma once

#include "index.hpp" // SD_FS
#include "../auth/auth.hpp"
#include "../fs/index.hpp"
#include "../utils/crypto.hpp"

#include <Arduino.h>
#include <vector>

namespace EncFS
{
    using std::vector;

    // --- Path encryption/decryption ---
    String encryptPath(const String &plainPath);
    String decryptPath(const String &encPath);

    // --- Directory operations ---
    vector<String> readDir(const String &path);
    bool createDir(const String &path);
    bool deleteDir(const String &path);

    // --- File operations ---
    bool writeFile(const String &path, const vector<uint8_t> &data);
    vector<uint8_t> readFile(const String &path);
    bool appendFile(const String &path, const vector<uint8_t> &data);
    bool deleteFile(const String &path);

    bool exists(const String &path);
    bool isDirectory(const String &path);
    size_t fileSize(const String &path);
    void getFileInfo(const String &path);

    // --- Chunked file operations ---
    vector<uint8_t> readFileChunk(const String &path, size_t index, size_t len);
    bool writeFileChunk(const String &path, size_t index, const vector<uint8_t> &data);

    // --- Card info ---
    uint64_t getCardSize();
    uint64_t getUsedBytes();
    uint64_t getFreeBytes();
    void getUsageSummary();
}
