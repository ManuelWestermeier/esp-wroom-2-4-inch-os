#include "enc-fs.hpp"

#include <Arduino.h>
#include <SD.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>
#include <mbedtls/sha256.h>
#include <esp_system.h>

namespace ENC_FS
{

    static String g_rootFolder;
    static uint8_t g_masterKey[32];
    static uint8_t g_salt[16];
    static uint32_t g_kdfIterations = 20000; // reasonable default for ESP32

    // ---------- Helpers ----------

    static void random_bytes(uint8_t *buf, size_t len)
    {
        size_t i = 0;
        while (i < len)
        {
            uint32_t r = esp_random();
            for (int b = 0; b < 4 && i < len; ++b)
            {
                buf[i++] = (r >> (8 * b)) & 0xFF;
            }
        }
    }

    static String hexEncode(const uint8_t *data, size_t len)
    {
        static const char hex[] = "0123456789abcdef";
        String s;
        s.reserve(len * 2);
        for (size_t i = 0; i < len; ++i)
        {
            s += hex[(data[i] >> 4) & 0xF];
            s += hex[data[i] & 0xF];
        }
        return s;
    }

    static void hexDecode(const String &s, uint8_t *out, size_t outLen)
    {
        size_t len = s.length();
        size_t pos = 0;
        for (size_t i = 0; i + 1 < len && pos < outLen; i += 2)
        {
            char a = s[i];
            char b = s[i + 1];
            auto val = [](char c) -> uint8_t
            {
                if (c >= '0' && c <= '9')
                    return c - '0';
                if (c >= 'a' && c <= 'f')
                    return c - 'a' + 10;
                if (c >= 'A' && c <= 'F')
                    return c - 'A' + 10;
                return 0;
            };
            out[pos++] = (val(a) << 4) | val(b);
        }
    }

    // HMAC-SHA256 (deterministic encrypted names)
    static void hmac_sha256(const uint8_t *key, size_t keylen, const uint8_t *msg, size_t msglen, uint8_t out[32])
    {
        const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
        mbedtls_md_context_t ctx;
        mbedtls_md_init(&ctx);
        if (mbedtls_md_setup(&ctx, md_info, 1) == 0)
        {
            mbedtls_md_hmac_starts(&ctx, key, keylen);
            mbedtls_md_hmac_update(&ctx, msg, msglen);
            mbedtls_md_hmac_finish(&ctx, out);
        }
        else
        {
            memset(out, 0, 32);
        }
        mbedtls_md_free(&ctx);
    }

    // PBKDF2-SHA256: derive master key from password and salt
    static bool derive_master_key(const String &password, const uint8_t salt[16], uint32_t iterations, uint8_t outKey[32])
    {
        mbedtls_md_context_t md_ctx;
        const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
        mbedtls_md_init(&md_ctx);
        if (mbedtls_md_setup(&md_ctx, md_info, 1) != 0)
        {
            mbedtls_md_free(&md_ctx);
            return false;
        }
        int rc = mbedtls_pkcs5_pbkdf2_hmac(&md_ctx,
                                           (const unsigned char *)password.c_str(), password.length(),
                                           salt, 16,
                                           (unsigned int)iterations,
                                           (uint32_t)32, outKey);
        mbedtls_md_free(&md_ctx);
        return rc == 0;
    }

    // AES-GCM encrypt/decrypt
    static bool aes_gcm_encrypt(const uint8_t *key, const uint8_t *plaintext, size_t plen,
                                uint8_t **out_ciphertext, size_t *out_len,
                                uint8_t nonce[12], uint8_t tag[16])
    {
        mbedtls_gcm_context gcm;
        mbedtls_gcm_init(&gcm);
        if (mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256) != 0)
        {
            mbedtls_gcm_free(&gcm);
            return false;
        }
        random_bytes(nonce, 12);
        uint8_t *cipher = (uint8_t *)malloc(plen);
        if (!cipher)
        {
            mbedtls_gcm_free(&gcm);
            return false;
        }
        if (mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT,
                                      plen, nonce, 12,
                                      nullptr, 0,
                                      plaintext, cipher, 16, tag) != 0)
        {
            free(cipher);
            mbedtls_gcm_free(&gcm);
            return false;
        }
        *out_ciphertext = cipher;
        *out_len = plen;
        mbedtls_gcm_free(&gcm);
        return true;
    }

    static bool aes_gcm_decrypt(const uint8_t *key, const uint8_t *ciphertext, size_t clen,
                                const uint8_t nonce[12], const uint8_t tag[16],
                                uint8_t **out_plaintext)
    {
        mbedtls_gcm_context gcm;
        mbedtls_gcm_init(&gcm);
        if (mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256) != 0)
        {
            mbedtls_gcm_free(&gcm);
            return false;
        }
        uint8_t *plain = (uint8_t *)malloc(clen + 1);
        if (!plain)
        {
            mbedtls_gcm_free(&gcm);
            return false;
        }
        if (mbedtls_gcm_auth_decrypt(&gcm, clen, nonce, 12, nullptr, 0, tag, 16, ciphertext, plain) != 0)
        {
            free(plain);
            mbedtls_gcm_free(&gcm);
            return false;
        }
        *out_plaintext = plain;
        mbedtls_gcm_free(&gcm);
        return true;
    }

    // ---------- Path helpers ----------

    Path str2Path(const String &s)
    {
        Path p;
        if (s.length() == 0)
            return p;
        String tmp = s;
        if (tmp[0] == '/')
            tmp = tmp.substring(1);
        int start = 0;
        for (int i = 0; i <= tmp.length(); ++i)
        {
            if (i == tmp.length() || tmp[i] == '/')
            {
                String part = tmp.substring(start, i);
                if (part.length() > 0)
                    p.push_back(part);
                start = i + 1;
            }
        }
        return p;
    }

    String path2Str(const Path &s)
    {
        String out = "/";
        for (size_t i = 0; i < s.size(); ++i)
        {
            out += s[i];
            if (i + 1 < s.size())
                out += "/";
        }
        return out;
    }

    // Convert a plain directory path to physical encrypted SD path
    static String encryptedDirPath(const Path &plainDir)
    {
        if (plainDir.empty()) // root folder
            return g_rootFolder;

        String cur = g_rootFolder;
        for (const auto &part : plainDir)
        {
            uint8_t mac[32];
            hmac_sha256(g_masterKey, sizeof(g_masterKey), (const uint8_t *)part.c_str(), part.length(), mac);
            String enc = hexEncode(mac, 16);
            cur += "/" + enc;
        }
        return cur;
    }

    // metadata file path inside a directory
    static String metaFilePathForDir(const String &encDir)
    {
        return encDir + "/.meta";
    }

    // metadata format (plaintext before encryption): line-based entries:
    // encName|nameHex|isDir|size\n

    static String encodeNameHex(const String &name)
    {
        // convert UTF8 String to bytes and hex encode
        const char *c = name.c_str();
        size_t l = strlen(c);
        return hexEncode((const uint8_t *)c, l);
    }

    static String decodeNameHex(const String &hex)
    {
        size_t len = hex.length() / 2;
        uint8_t *buf = (uint8_t *)malloc(len + 1);
        hexDecode(hex, buf, len);
        buf[len] = 0;
        String s((const char *)buf);
        free(buf);
        return s;
    }

    struct DirEntryPlain
    {
        String encName;
        String name;
        bool isDir;
        long size;
    };

    static std::vector<DirEntryPlain> readDirMeta(const String &encDir)
    {
        std::vector<DirEntryPlain> ret;
        String metaPath = metaFilePathForDir(encDir);
        if (!SD.exists(metaPath))
            return ret; // empty
        File f = SD.open(metaPath, FILE_READ);
        if (!f)
            return ret;
        // read encrypted blob
        size_t total = f.size();
        if (total < 12 + 16)
        {
            f.close();
            return ret;
        }
        uint8_t nonce[12];
        if (f.read(nonce, 12) != 12)
        {
            f.close();
            return ret;
        }
        size_t cipherlen = total - 12 - 16;
        uint8_t *cipher = (uint8_t *)malloc(cipherlen);
        if (f.read(cipher, cipherlen) != (int)cipherlen)
        {
            free(cipher);
            f.close();
            return ret;
        }
        uint8_t tag[16];
        if (f.read(tag, 16) != 16)
        {
            free(cipher);
            f.close();
            return ret;
        }
        f.close();
        uint8_t *plain;
        if (!aes_gcm_decrypt(g_masterKey, cipher, cipherlen, nonce, tag, &plain))
        {
            free(cipher);
            return ret; // decryption failed -> treat directory as empty
        }
        free(cipher);
        String pdata((char *)plain, cipherlen); // decrypted content
        free(plain);
        // parse lines
        int start = 0;
        for (int i = 0; i <= pdata.length(); ++i)
        {
            if (i == pdata.length() || pdata[i] == '\n')
            {
                String line = pdata.substring(start, i);
                start = i + 1;
                if (line.length() == 0)
                    continue;
                int p1 = line.indexOf('|');
                int p2 = line.indexOf('|', p1 + 1);
                int p3 = line.indexOf('|', p2 + 1);
                if (p1 < 0 || p2 < 0 || p3 < 0)
                    continue;
                DirEntryPlain e;
                e.encName = line.substring(0, p1);
                String nameHex = line.substring(p1 + 1, p2);
                e.name = decodeNameHex(nameHex);
                e.isDir = line.substring(p2 + 1, p3) == "1";
                e.size = line.substring(p3 + 1).toInt();
                ret.push_back(e);
            }
        }
        return ret;
    }

    static bool writeDirMeta(const String &encDir, const std::vector<DirEntryPlain> &entries)
    {
        // build plaintext meta
        String pdata;
        for (const auto &e : entries)
        {
            pdata += e.encName + "|" + encodeNameHex(e.name) + "|" + (e.isDir ? "1" : "0") + "|" + String(e.size) + "\n";
        }
        // encrypt
        uint8_t *cipher = nullptr;
        size_t cipherlen = 0;
        uint8_t nonce[12];
        uint8_t tag[16];
        if (!aes_gcm_encrypt(g_masterKey, (const uint8_t *)pdata.c_str(), pdata.length(), &cipher, &cipherlen, nonce, tag))
        {
            return false;
        }
        String metaPath = metaFilePathForDir(encDir);
        File f = SD.open(metaPath, FILE_WRITE);
        if (!f)
        {
            free(cipher);
            return false;
        }
        f.write(nonce, 12);
        f.write(cipher, cipherlen);
        f.write(tag, 16);
        f.close();
        free(cipher);
        return true;
    }

    // Ensure directory exists on SD (encrypted path). Also create .meta if not exists.
    static bool ensureEncryptedDirExists(const String &encDir)
    {
        if (!SD.exists(encDir))
        {
            // create the directory itself
            if (!SD.mkdir(encDir))
                return false;
        }

        // check .meta
        String metaPath = metaFilePathForDir(encDir);
        if (!SD.exists(metaPath))
        {
            std::vector<DirEntryPlain> emptyEntries;
            if (!writeDirMeta(encDir, emptyEntries))
                return false;
        }

        return true;
    }

    // Find entry in directory metadata by plain name
    static bool findEntryInDir(const String &encDir, const String &plainName, DirEntryPlain &out)
    {
        auto entries = readDirMeta(encDir);
        for (auto &e : entries)
        {
            if (e.name == plainName)
            {
                out = e;
                return true;
            }
        }
        return false;
    }

    // Add or update entry
    static bool upsertEntryInDir(const String &encDir, const DirEntryPlain &entry)
    {
        auto entries = readDirMeta(encDir);
        bool found = false;
        for (auto &e : entries)
        {
            if (e.encName == entry.encName)
            {
                e = entry;
                found = true;
                break;
            }
        }
        if (!found)
            entries.push_back(entry);
        return writeDirMeta(encDir, entries);
    }

    static bool removeEntryInDir(const String &encDir, const String &encName)
    {
        auto entries = readDirMeta(encDir);
        bool changed = false;
        std::vector<DirEntryPlain> newe;
        for (auto &e : entries)
        {
            if (e.encName == encName)
            {
                changed = true;
                continue;
            }
            newe.push_back(e);
        }
        if (!changed)
            return false;
        return writeDirMeta(encDir, newe);
    }

    // ---------- API ----------

    bool exists(const Path &p)
    {
        if (p.empty())
            return SD.exists(g_rootFolder);
        Path dir = p;
        String filename = dir.back();
        dir.pop_back();
        String encDir = encryptedDirPath(dir);
        DirEntryPlain e;
        return findEntryInDir(encDir, filename, e);
    }

    bool mkDir(const Path &p)
    {
        if (p.empty())
            return false;
        Path dir = p;
        String newName = dir.back();
        dir.pop_back();
        String parentEnc = encryptedDirPath(dir);
        if (!ensureEncryptedDirExists(parentEnc))
            return false;
        // compute encName
        uint8_t mac[32];
        hmac_sha256(g_masterKey, sizeof(g_masterKey), (const uint8_t *)newName.c_str(), newName.length(), mac);
        String enc = hexEncode(mac, 16);
        String newDirPath = parentEnc + "/" + enc;
        if (!SD.exists(newDirPath))
        {
            if (!SD.mkdir(newDirPath))
                return false;
        }
        DirEntryPlain e;
        e.encName = enc;
        e.name = newName;
        e.isDir = true;
        e.size = 0;
        if (!upsertEntryInDir(parentEnc, e))
            return false;
        // create meta in the new dir as well
        if (!ensureEncryptedDirExists(newDirPath))
            return false;
        return true;
    }

    bool rmDir(const Path &p)
    {
        if (p.empty())
            return false;
        Path dir = p;
        String name = dir.back();
        dir.pop_back();
        String encParent = encryptedDirPath(dir);
        DirEntryPlain e;
        if (!findEntryInDir(encParent, name, e))
            return false;
        if (!e.isDir)
            return false;
        String encFull = encParent + "/" + e.encName;
        // simple remove: only if empty (no .meta entries)
        auto childEntries = readDirMeta(encFull);
        if (!childEntries.empty())
            return false;
        // remove directory
        if (!SD.rmdir(encFull))
            return false;
        if (!removeEntryInDir(encParent, e.encName))
            return false;
        return true;
    }

    Buffer readFilePart(const Path &p, long start, long end)
    {
        Buffer empty;
        if (p.empty())
            return empty;
        Path dir = p;
        String name = dir.back();
        dir.pop_back();
        String encDir = encryptedDirPath(dir);
        DirEntryPlain e;
        if (!findEntryInDir(encDir, name, e))
            return empty;
        if (e.isDir)
            return empty;
        String filePath = encDir + "/" + e.encName + ".data";
        if (!SD.exists(filePath))
            return empty;
        File f = SD.open(filePath, FILE_READ);
        if (!f)
            return empty;
        size_t total = f.size();
        if (total < 12 + 16)
        {
            f.close();
            return empty;
        }
        uint8_t nonce[12];
        f.read(nonce, 12);
        size_t cipherlen = total - 12 - 16;
        uint8_t *cipher = (uint8_t *)malloc(cipherlen);
        f.read(cipher, cipherlen);
        uint8_t tag[16];
        f.read(tag, 16);
        f.close();
        uint8_t *plain;
        if (!aes_gcm_decrypt(g_masterKey, cipher, cipherlen, nonce, tag, &plain))
        {
            free(cipher);
            return empty;
        }
        free(cipher);
        long plen = cipherlen; // plaintext length equals cipherlen
        if (start < 0)
            start = 0;
        if (end < 0 || end > plen)
            end = plen;
        if (start >= end)
        {
            free(plain);
            return empty;
        }
        Buffer out;
        out.resize(end - start);
        memcpy(out.data(), plain + start, end - start);
        free(plain);
        return out;
    }

    Buffer readFile(const Path &p, long start, long end)
    {
        return readFilePart(p, start, end);
    }

    String readFileString(const Path &p)
    {
        auto buf = readFile(p);
        if (buf.empty())
            return String();
        String s;
        s.reserve(buf.size());
        for (size_t i = 0; i < buf.size(); ++i)
            s += (char)buf[i];
        return s;
    }

    bool writeFile(const Path &p, long start, long end, const Buffer &data)
    {
        if (p.empty())
            return false;

        Path dir = p;
        String name = dir.back();
        dir.pop_back();

        String encDir = encryptedDirPath(dir);
        if (!ensureEncryptedDirExists(encDir))
            return false;

        uint8_t mac[32];
        hmac_sha256(g_masterKey, sizeof(g_masterKey), (const uint8_t *)name.c_str(), name.length(), mac);
        String enc = hexEncode(mac, 16);
        String filePath = encDir + "/" + enc + ".data";

        // read existing file if present
        DirEntryPlain e;
        Buffer oldPlain;
        if (findEntryInDir(encDir, name, e) && !e.isDir)
        {
            String existingPath = encDir + "/" + e.encName + ".data";
            File ef = SD.open(existingPath, FILE_READ);
            if (ef)
            {
                size_t total = ef.size();
                if (total >= 12 + 16)
                {
                    uint8_t nonce[12];
                    ef.read(nonce, 12);
                    size_t cipherlen = total - 12 - 16;
                    uint8_t *cipher = (uint8_t *)malloc(cipherlen);
                    ef.read(cipher, cipherlen);
                    uint8_t tag[16];
                    ef.read(tag, 16);
                    ef.close();

                    uint8_t *plain = nullptr;
                    if (aes_gcm_decrypt(g_masterKey, cipher, cipherlen, nonce, tag, &plain))
                    {
                        oldPlain.resize(cipherlen);
                        memcpy(oldPlain.data(), plain, cipherlen);
                        free(plain);
                    }
                    free(cipher);
                }
                else
                    ef.close();
            }
        }

        // compute new plaintext buffer
        size_t oldSize = oldPlain.size();
        size_t s = start < 0 ? 0 : (size_t)start;
        size_t epos = end < 0 ? oldSize : (size_t)end;
        if (s > oldSize)
            s = oldSize;
        if (epos > oldSize)
            epos = oldSize;

        size_t newSize = s + data.size() + ((oldSize > epos) ? (oldSize - epos) : 0);
        Buffer newPlain;
        newPlain.resize(newSize);

        if (s > 0 && oldSize > 0)
            memcpy(newPlain.data(), oldPlain.data(), s);
        if (!data.empty())
            memcpy(newPlain.data() + s, data.data(), data.size());
        if (oldSize > epos)
            memcpy(newPlain.data() + s + data.size(), oldPlain.data() + epos, oldSize - epos);

        // encrypt and write
        uint8_t *cipher = nullptr;
        size_t cipherlen = 0;
        uint8_t nonce[12];
        uint8_t tag[16];
        if (!aes_gcm_encrypt(g_masterKey, newPlain.data(), newPlain.size(), &cipher, &cipherlen, nonce, tag))
            return false;

        File f = SD.open(filePath, FILE_WRITE);
        if (!f)
        {
            free(cipher);
            return false;
        }
        f.write(nonce, 12);
        f.write(cipher, cipherlen);
        f.write(tag, 16);
        f.close();
        free(cipher);

        // update metadata
        DirEntryPlain ne;
        ne.encName = enc;
        ne.name = name;
        ne.isDir = false;
        ne.size = newPlain.size();
        if (!upsertEntryInDir(encDir, ne))
            return false;

        return true;
    }

    bool appendFile(const Path &p, const Buffer &data)
    {
        // append == writeFile with start = current size
        if (p.empty())
            return false;
        Path dir = p;
        String name = dir.back();
        dir.pop_back();
        String encDir = encryptedDirPath(dir);
        DirEntryPlain e;
        long cursize = 0;
        if (findEntryInDir(encDir, name, e) && !e.isDir)
            cursize = e.size;
        return writeFile(p, cursize, cursize, data);
    }

    bool writeFileString(const Path &p, const String &s)
    {
        Buffer b(s.length());
        for (size_t i = 0; i < s.length(); ++i)
            b[i] = (uint8_t)s[i];
        // write replacing whole file
        return writeFile(p, 0, -1, b);
    }

    bool deleteFile(const Path &p)
    {
        if (p.empty())
            return false;
        Path dir = p;
        String name = dir.back();
        dir.pop_back();
        String encDir = encryptedDirPath(dir);
        DirEntryPlain e;
        if (!findEntryInDir(encDir, name, e))
            return false;
        if (e.isDir)
            return false;
        String filePath = encDir + "/" + e.encName + ".data";
        if (SD.exists(filePath))
            SD.remove(filePath);
        if (!removeEntryInDir(encDir, e.encName))
            return false;
        return true;
    }

    long getFileSize(const Path &p)
    {
        if (p.empty())
            return -1;
        Path dir = p;
        String name = dir.back();
        dir.pop_back();
        String encDir = encryptedDirPath(dir);
        DirEntryPlain e;
        if (!findEntryInDir(encDir, name, e))
            return -1;
        return e.size;
    }

    Metadata getMetadata(const Path &p)
    {
        Metadata m;
        m.size = -1;
        m.encryptedName = "";
        m.decryptedName = "";
        m.isDirectory = false;
        if (p.empty())
        {
            m.isDirectory = true;
            m.decryptedName = "/";
            m.encryptedName = g_rootFolder;
            return m;
        }
        Path dir = p;
        String name = dir.back();
        dir.pop_back();
        String encDir = encryptedDirPath(dir);
        DirEntryPlain e;
        if (!findEntryInDir(encDir, name, e))
            return m;
        m.size = e.size;
        m.encryptedName = e.encName;
        m.decryptedName = e.name;
        m.isDirectory = e.isDir;
        return m;
    }

    std::vector<String> readDir(const Path &plainDir)
    {
        String encDir = encryptedDirPath(plainDir);
        std::vector<String> ret;
        auto entries = readDirMeta(encDir);
        for (auto &e : entries)
            ret.push_back(e.name);
        return ret;
    }

    void lsDirSerial(const Path &plainDir)
    {
        auto items = readDir(plainDir);
        for (auto &s : items)
        {
            Serial.println(s);
        }
    }

    Path storagePath(const String &appId, const String &key)
    {
        // map to /programms/<appId>/data/<sha256(key)>.data
        // we implement path as: /programms/<appId>/data/<hex(sha256(key))>
        uint8_t sha[32];
        mbedtls_sha256_context ctx;
        mbedtls_sha256_init(&ctx);
        mbedtls_sha256_starts_ret(&ctx, 0);
        mbedtls_sha256_update_ret(&ctx, (const unsigned char *)key.c_str(), key.length());
        mbedtls_sha256_finish_ret(&ctx, sha);
        mbedtls_sha256_free(&ctx);
        String keyHex = hexEncode(sha, 32);
        Path p;
        p.push_back("programms");
        p.push_back(appId);
        p.push_back("data");
        p.push_back(keyHex);
        return p;
    }

    namespace Storage
    {
        Buffer get(const String &appId, const String &key, long start, long end)
        {
            Path p = storagePath(appId, key);
            return readFile(p, start, end);
        }
        bool del(const String &appId, const String &key)
        {
            Path p = storagePath(appId, key);
            return deleteFile(p);
        }
        bool set(const String &appId, const String &key, const Buffer &data)
        {
            Path p = storagePath(appId, key);
            return writeFile(p, 0, -1, data);
        }
    }

    void copyFileFromSPIFFS(const char *spiffsPath, const Path &sdPath)
    {
        File s = SPIFFS.open(spiffsPath, FILE_READ);
        if (!s)
            return;
        Buffer b;
        b.resize(s.size());
        s.read(b.data(), b.size());
        s.close();
        writeFile(sdPath, 0, -1, b);
    }

    void init(String rootFolder, String password)
    {
        g_rootFolder = rootFolder;
        if (g_rootFolder.endsWith("/"))
            g_rootFolder.remove(g_rootFolder.length() - 1);

        // initialize SD
        if (!SD.begin())
        {
            Serial.println("SD.begin() failed");
            return;
        }

        // create root folder if missing
        if (!SD.exists(g_rootFolder))
            SD.mkdir(g_rootFolder);

        // read or create salt
        String saltPath = g_rootFolder + "/.salt";
        if (SD.exists(saltPath))
        {
            File f = SD.open(saltPath, FILE_READ);
            String hex;
            hex.reserve(f.size());
            while (f.available())
                hex += (char)f.read();
            f.close();
            hexDecode(hex, g_salt, 16);
        }
        else
        {
            random_bytes(g_salt, 16);
            File f = SD.open(saltPath, FILE_WRITE);
            if (f)
            {
                String hex = hexEncode(g_salt, 16);
                f.print(hex);
                f.close();
            }
        }

        // derive master key
        if (!derive_master_key(password, g_salt, g_kdfIterations, g_masterKey))
        {
            Serial.println("KDF failed");
            return;
        }

        // ensure root metadata exists
        String rootMeta = g_rootFolder + "/.meta";
        if (!SD.exists(rootMeta))
        {
            writeDirMeta(g_rootFolder, {}); // empty root directory
        }
    }

    void setKdfIterations(uint32_t it)
    {
        g_kdfIterations = it;
    }

} // namespace ENC_FS
