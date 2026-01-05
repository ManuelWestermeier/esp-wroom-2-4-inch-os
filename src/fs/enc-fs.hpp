#pragma once

#include <Arduino.h>
#include <SPIFFS.h>
#include <SD.h>
#include <vector>
#include <map>

#include "../auth/auth.hpp"
#include "../utils/crypto.hpp"

namespace ENC_FS
{
    using Path = std::vector<String>;
    using Buffer = std::vector<uint8_t>;

    // ---------- Helpers ----------
    Buffer sha256(const String &s);
    void pkcs7_pad(Buffer &b);
    bool pkcs7_unpad(Buffer &b);

    String base64url_encode(const uint8_t *data, size_t len); // hex encode
    bool base64url_decode(const String &s, Buffer &out);      // hex decode

    // master key derived from username:password + device salt
    Buffer deriveMasterKey();
    // per-folder key derived from master + folder name (caching)
    Buffer deriveFolderKey(const String &folder);

    void secureZero(Buffer &b);
    void secureZero(uint8_t *buf, size_t len);

    // ---------- Path helpers ----------
    Path str2Path(const String &s);
    String path2Str(const Path &s);
    bool isValidSegment(const String &seg);

    // ---------- Filename segment encryption ----------
    String encryptSegment(const String &seg); // AES-ECB + HMAC, hex encoded
    bool decryptSegment(const String &enc, String &outSeg);

    String joinEncPath(const Path &plain);

    // ---------- AES-CTR for file contents (per-folder keys, per-file nonce) ----------
    Buffer aes_ctr_crypt_full(const Path &p, const Buffer &in);
    Buffer aes_ctr_crypt_offset(const Path &p, const Buffer &in, size_t offset);

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

    Path storagePath(const String &appId, const String &key);

    namespace Storage
    {
        Buffer get(const String &appId, const String &key, long start = -1, long end = -1);
        bool del(const String &appId, const String &key);
        bool set(const String &appId, const String &key, const Buffer &data);
    }

    void copyFileFromSPIFFS(const char *spiffsPath, const Path &sdPath);
} // namespace ENC_FS
