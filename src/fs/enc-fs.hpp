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

    // ---------- Helpers ----------

    Buffer sha256(const String &s);
    void pkcs7_pad(Buffer &b);
    bool pkcs7_unpad(Buffer &b);
    String base64url_encode(const uint8_t *data, size_t len);
    bool base64url_decode(const String &s, Buffer &out);
    static void deriveNonceForFullPathVersion(const String &full, uint64_t version, uint8_t nonce[16]);
    static bool writeVersionForFullPath(const String &full, uint64_t v);
    static uint64_t readVersionForFullPath(const String &full);

    Buffer deriveKey();

    // ---------- Path helpers ----------

    Path str2Path(const String &s);
    String path2Str(const Path &p);

    // ---------- Segment encryption/decryption ----------
    String joinEncPath(const Path &plain);
    // ---------- File AES-CTR ----------

    Buffer aes_ctr_crypt_full_with_nonce(const Buffer &in, const uint8_t nonce[16]);
    Buffer aes_ctr_crypt_offset_with_nonce(const Buffer &in, size_t offset, const uint8_t nonce[16]);

    // ---------- File API ----------

    bool exists(const Path &p);
    bool mkDir(const Path &p);
    bool rmDir(const Path &p);
    Buffer readFilePart(const Path &p, long start, long end);
    Buffer readFile(const Path &p, long start, long end);
    Buffer readFileFull(const Path &p);
    String readFileString(const Path &p);
    bool writeFile(const Path &p, long start, long end, const Buffer &data);
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

    namespace BrowserStorage
    {
        Buffer get(const String &domain);
        bool del(const String &domain);
        bool set(const String &domain, const Buffer &data);
        bool clearAll();
        std::vector<String> listSites();
    }

    void copyFileFromSPIFFS(const char *spiffsPath, const Path &sdPath);
} // namespace ENC_FS