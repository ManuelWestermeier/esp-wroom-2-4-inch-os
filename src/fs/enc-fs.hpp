#pragma once
/*
  enc-fs.hpp

  ENC_FS - Virtuelles, verschlüsseltes Dateisystem für ESP32
  Vereinfachte, funktionierende Implementierung (Header).
  Implementierung in enc-fs.cpp
*/

#include <Arduino.h>
#include <vector>
#include <SD.h>
#include <FS.h>
#include <SPIFFS.h>

namespace ENC_FS
{
    using Path = std::vector<String>;
    using Buffer = std::vector<uint8_t>;

    // ---------- Path helpers ----------
    Path str2Path(const String &s);
    String path2Str(const Path &s);

    // ---------- API ----------
    bool exists(const Path &p);
    bool mkDir(const Path &p);
    bool rmDir(const Path &p);

    Buffer readFilePart(const Path &p, long start, long end);
    Buffer readFile(const Path &p, long start = 0, long end = -1);
    String readFileString(const Path &p);

    bool writeFile(const Path &p, long start, long end, const Buffer &data);
    bool appendFile(const Path &p, const Buffer &data);
    bool writeFileString(const Path &p, const String &s);

    bool deleteFile(const Path &p);
    long getFileSize(const Path &p);

    struct Metadata
    {
        long size;
        String encryptedName;
        String decryptedName;
        bool isDirectory;
    };

    Metadata getMetadata(const Path &p);

    std::vector<String> readDir(const Path &plainDir);
    void lsDirSerial(const Path &plainDir);

    // storagePath as specified by the user
    // to path { programms, appId, data, CryptoHelper::sha256(key) + ".data" }
    Path storagePath(const String &appId, const String &key);

    namespace Storage
    {
        Buffer get(const String &appId, const String &key, long start = -1, long end = -1);
        bool del(const String &appId, const String &key);
        bool set(const String &appId, const String &key, const Buffer &data);
    }

    void copyFileFromSPIFFS(const char *spiffsPath, const Path &sdPath);

    void init(String rootFolder, String password);

    void setKdfIterations(uint32_t it); // iterations for password -> master_key (dangerous to change at runtime)

} // namespace ENC_FS
