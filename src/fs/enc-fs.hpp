#pragma once

#include <Arduino.h>
#include <vector>
#include <FS.h>
#include <SPIFFS.h>
#include <SD.h>

namespace ENC_FS
{
    using Buffer = std::vector<uint8_t>;
    using Path = std::vector<String>;

    struct Metadata
    {
        long size;
        String encryptedName;
        String decryptedName;
        bool isDirectory;
    };

    // High-level API (compatible with original interface)
    Buffer sha256(const String &s);

    // Path helpers
    Path str2Path(const String &s);
    String path2Str(const Path &p);

    // Name token / metadata
    String joinEncPath(const Path &plain);

    // File AEAD (AES-GCM) helpers
    Buffer aes_gcm_encrypt(const Buffer &plaintext);
    bool aes_gcm_decrypt(const Buffer &in, Buffer &out);

    // File API (note: write operations encrypt the whole file atomically)
    bool exists(const Path &p);
    bool mkDir(const Path &p);
    bool rmDir(const Path &p);
    Buffer readFile(const Path &p);
    Buffer readFilePart(const Path &p, long start, long end);
    String readFileString(const Path &p);
    bool writeFile(const Path &p, const Buffer &data);
    bool writeFile(const Path &p, long start, long end, const Buffer &data); // will re-encrypt whole file
    bool appendFile(const Path &p, const Buffer &data);
    bool writeFileString(const Path &p, const String &s);
    bool deleteFile(const Path &p);
    long getFileSize(const Path &p);
    Metadata getMetadata(const Path &p);
    std::vector<String> readDir(const Path &plainDir);
    void lsDirSerial(const Path &plainDir);

    Path storagePath(const String &appId, const String &key);

    namespace Storage
    {
        Buffer get(const String &appId, const String &key, long start = -1, long end = -1);
        bool del(const String &appId, const String &key);
        bool set(const String &appId, const String &key, const Buffer &data);
    }

    void copyFileFromSPIFFS(const char *spiffsPath, const Path &sdPath);
} // namespace ENC_FS
