#pragma once

#include <Arduino.h>
#include <SD.h>
#include <vector>

namespace ENC_FS
{
    using Path = std::vector<String>;
    using Buffer = std::vector<uint8_t>;

    // ---------- Helpers ----------
    Buffer sha256(const String &s);
    void pkcs7_pad(Buffer &b);
    bool pkcs7_unpad(Buffer &b);

    String base64url_encode(const uint8_t *data, size_t len);
    bool base64url_decode(const String &s, Buffer &out);

    Buffer deriveKey();

    // ---------- Path helpers ----------
    Path str2Path(const String &s);
    String path2Str(const Path &s);
    String encryptSegment(const String &seg);
    bool decryptSegment(const String &enc, String &outSeg);
    String joinEncPath(const Path &plain);

    // ---------- AES-CTR ----------
    Buffer aes_ctr_crypt_full(const Buffer &in);
    Buffer aes_ctr_crypt_offset(const Buffer &in, size_t offset);

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
        bool set(const String &appId, const String &key, const Buffer &data);
    }
}
