/*
  enc-fs.cpp
  Sicheres, virtuelles Dateisystem für ESP32.
  Nutzt AES-256-CTR für Datenblobs und AES-256-CBC für den verschlüsselten Index.
*/

#include "enc-fs.hpp"
#include <map>
#include <mbedtls/aes.h>
#include <mbedtls/sha256.h>
#include <mbedtls/pkcs5.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/md.h>

namespace ENC_FS
{

    // --- Interne Statics & Strukturen ---
    namespace
    {
        struct FileEntry
        {
            bool isDir;
            long size;           // Logische Größe (wichtig gegen Padding-Fehler)
            String physicalName; // Name des .dat Blobs auf SD
            uint8_t iv[16];      // IV für AES-CTR
        };

        std::map<String, FileEntry> fsIndex;
        uint8_t masterKey[32];
        bool isInitialized = false;
        String rootPathStr = "";

        uint32_t g_kdfIterations = 10000;
        mbedtls_entropy_context entropy;
        mbedtls_ctr_drbg_context ctr_drbg;

        const char *BLOBS_DIR = "/_blobs";
        const char *INDEX_FILE = "/enc_fs.idx";
    }

    // --- Hilfsfunktionen für Kryptographie ---
    namespace CryptoHelper
    {
        void log(String msg) { Serial.println("[ENC_FS] " + msg); }

        void getRandom(uint8_t *buffer, size_t length)
        {
            mbedtls_ctr_drbg_random(&ctr_drbg, buffer, length);
        }

        String sha256(const String &payload)
        {
            uint8_t result[32];
            mbedtls_sha256((const uint8_t *)payload.c_str(), payload.length(), result, 0);
            String hashStr = "";
            for (int i = 0; i < 32; i++)
            {
                if (result[i] < 16)
                    hashStr += "0";
                hashStr += String(result[i], HEX);
            }
            return hashStr;
        }

        // AES-256-CTR ermöglicht Random Access (Seek)
        void aesCtr(const uint8_t *key, const uint8_t *iv, long offset, uint8_t *data, size_t len)
        {
            mbedtls_aes_context aes;
            mbedtls_aes_init(&aes);
            mbedtls_aes_setkey_enc(&aes, key, 256);

            uint8_t nc[16];
            memcpy(nc, iv, 16);
            uint8_t sb[16];
            size_t no = 0;

            // Counter-Anpassung für Seek (Offset-Berechnung)
            if (offset > 0)
            {
                long blocks = offset / 16;
                no = offset % 16;
                for (int i = 15; i >= 0; i--)
                {
                    int sum = nc[i] + (blocks & 0xFF);
                    nc[i] = sum & 0xFF;
                    blocks >>= 8;
                    blocks += (sum >> 8);
                }
            }
            mbedtls_aes_crypt_ctr(&aes, len, &no, nc, sb, data, data);
            mbedtls_aes_free(&aes);
        }
    }

    // --- Index Management (Verschlüsselter Index auf SD) ---
    namespace IndexManager
    {

        Buffer serialize()
        {
            Buffer buf;
            uint32_t count = fsIndex.size();
            uint8_t *cPtr = (uint8_t *)&count;
            for (int i = 0; i < 4; i++)
                buf.push_back(cPtr[i]);

            for (auto const &[path, entry] : fsIndex)
            {
                uint16_t pLen = path.length();
                buf.push_back(pLen & 0xFF);
                buf.push_back((pLen >> 8) & 0xFF);
                for (size_t i = 0; i < pLen; i++)
                    buf.push_back((uint8_t)path[i]);
                buf.push_back(entry.isDir ? 1 : 0);
                uint8_t *sPtr = (uint8_t *)&entry.size;
                for (int i = 0; i < 4; i++)
                    buf.push_back(sPtr[i]);
                uint16_t phLen = entry.physicalName.length();
                buf.push_back(phLen & 0xFF);
                buf.push_back((phLen >> 8) & 0xFF);
                for (size_t i = 0; i < phLen; i++)
                    buf.push_back((uint8_t)entry.physicalName[i]);
                for (int i = 0; i < 16; i++)
                    buf.push_back(entry.iv[i]);
            }
            return buf;
        }

        bool save(const String &root)
        {
            Buffer plain = serialize();
            // CBC Padding
            size_t pLen = plain.size();
            size_t paddedLen = (pLen + 15) & ~15;
            if (paddedLen == pLen)
                paddedLen += 16;
            uint8_t padVal = paddedLen - pLen;
            for (size_t i = pLen; i < paddedLen; i++)
                plain.push_back(padVal);

            uint8_t iv[16];
            CryptoHelper::getRandom(iv, 16);
            Buffer enc(paddedLen);
            mbedtls_aes_context aes;
            mbedtls_aes_init(&aes);
            mbedtls_aes_setkey_enc(&aes, masterKey, 256);
            mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, paddedLen, iv, plain.data(), enc.data());
            mbedtls_aes_free(&aes);

            File f = SD.open(root + INDEX_FILE, FILE_WRITE);
            if (!f)
                return false;
            f.write(iv, 16);
            f.write(enc.data(), paddedLen);
            f.close();
            return true;
        }

        bool load(const String &root)
        {
            String path = root + INDEX_FILE;
            if (!SD.exists(path))
                return false;
            File f = SD.open(path, FILE_READ);
            if (!f || f.size() < 32)
                return false;

            uint8_t iv[16];
            f.read(iv, 16);
            size_t encSize = f.size() - 16;
            Buffer enc(encSize);
            f.read(enc.data(), encSize);
            f.close();

            Buffer plain(encSize);
            mbedtls_aes_context aes;
            mbedtls_aes_init(&aes);
            mbedtls_aes_setkey_dec(&aes, masterKey, 256);
            uint8_t ivCpy[16];
            memcpy(ivCpy, iv, 16);
            mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, encSize, ivCpy, enc.data(), plain.data());
            mbedtls_aes_free(&aes);

            uint8_t pad = plain[encSize - 1];
            if (pad > 16)
                return false;

            fsIndex.clear();
            size_t ptr = 4; // Skip count (handled in loop if needed)
            uint32_t count;
            memcpy(&count, plain.data(), 4);

            for (uint32_t i = 0; i < count; i++)
            {
                uint16_t pl = plain[ptr] | (plain[ptr + 1] << 8);
                ptr += 2;
                String pStr = "";
                for (int k = 0; k < pl; k++)
                    pStr += (char)plain[ptr++];
                FileEntry fe;
                fe.isDir = (plain[ptr++] == 1);
                memcpy(&fe.size, &plain[ptr], 4);
                ptr += 4;
                uint16_t phl = plain[ptr] | (plain[ptr + 1] << 8);
                ptr += 2;
                fe.physicalName = "";
                for (int k = 0; k < phl; k++)
                    fe.physicalName += (char)plain[ptr++];
                memcpy(fe.iv, &plain[ptr], 16);
                ptr += 16;
                fsIndex[pStr] = fe;
            }
            return true;
        }
    }

    // --- API Implementierung ---

    void init(String rootFolder, String password)
    {
        rootPathStr = rootFolder;
        if (rootPathStr.endsWith("/"))
            rootPathStr.remove(rootPathStr.length() - 1);

        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&ctr_drbg);
        mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, (const uint8_t *)"SEED", 4);

        // PBKDF2 Fix: Context Setup
        mbedtls_md_context_t m_ctx;
        mbedtls_md_init(&m_ctx);
        mbedtls_md_setup(&m_ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
        String salt = "ENC_FS_SALT_V1";
        mbedtls_pkcs5_pbkdf2_hmac(&m_ctx, (const uint8_t *)password.c_str(), password.length(),
                                  (const uint8_t *)salt.c_str(), salt.length(),
                                  g_kdfIterations, 32, masterKey);
        mbedtls_md_free(&m_ctx);

        if (!SD.exists(rootPathStr))
            SD.mkdir(rootPathStr);
        if (!SD.exists(rootPathStr + BLOBS_DIR))
            SD.mkdir(rootPathStr + BLOBS_DIR);

        IndexManager::load(rootPathStr);
        isInitialized = true;
    }

    Path str2Path(const String &s)
    {
        Path p;
        int start = (s.startsWith("/") ? 1 : 0), end = s.indexOf('/', start);
        while (end != -1)
        {
            p.push_back(s.substring(start, end));
            start = end + 1;
            end = s.indexOf('/', start);
        }
        if (start < s.length())
            p.push_back(s.substring(start));
        return p;
    }

    String path2Str(const Path &p)
    {
        String s = "/";
        for (size_t i = 0; i < p.size(); i++)
        {
            s += p[i];
            if (i < p.size() - 1)
                s += "/";
        }
        return s;
    }

    bool exists(const Path &p) { return fsIndex.count(path2Str(p)) > 0; }

    bool mkDir(const Path &p)
    {
        String path = path2Str(p);
        if (fsIndex.count(path))
            return true;
        fsIndex[path] = {true, 0, "", {0}};
        return IndexManager::save(rootPathStr);
    }

    bool writeFile(const Path &p, long start, long end, const Buffer &data)
    {
        String path = path2Str(p);
        FileEntry fe;
        fe.isDir = false;
        fe.size = data.size(); // Logische Größe fixieren
        CryptoHelper::getRandom(fe.iv, 16);

        uint8_t rnd[4];
        CryptoHelper::getRandom(rnd, 4);
        fe.physicalName = String(millis(), HEX) + String(rnd[0], HEX) + ".dat";

        Buffer enc = data;
        CryptoHelper::aesCtr(masterKey, fe.iv, 0, enc.data(), enc.size());

        File f = SD.open(rootPathStr + BLOBS_DIR + "/" + fe.physicalName, FILE_WRITE);
        if (!f)
            return false;
        f.write(enc.data(), enc.size());
        f.close();

        if (fsIndex.count(path))
            SD.remove(rootPathStr + BLOBS_DIR + "/" + fsIndex[path].physicalName);
        fsIndex[path] = fe;
        return IndexManager::save(rootPathStr);
    }

    Buffer readFile(const Path &p, long start, long end)
    {
        String path = path2Str(p);
        Buffer res;
        if (!fsIndex.count(path))
            return res;
        FileEntry fe = fsIndex[path];

        if (start < 0)
            start = 0;
        if (end < 0 || end >= fe.size)
            end = fe.size - 1;
        if (start > end)
            return res;

        size_t len = end - start + 1;
        res.resize(len);
        File f = SD.open(rootPathStr + BLOBS_DIR + "/" + fe.physicalName, FILE_READ);
        if (!f)
            return res;
        f.seek(start);
        f.read(res.data(), len);
        f.close();

        CryptoHelper::aesCtr(masterKey, fe.iv, start, res.data(), len);
        return res;
    }

    long getFileSize(const Path &p)
    {
        String path = path2Str(p);
        return fsIndex.count(path) ? fsIndex[path].size : -1;
    }

    bool deleteFile(const Path &p)
    {
        String path = path2Str(p);
        if (!fsIndex.count(path))
            return false;
        if (!fsIndex[path].isDir)
            SD.remove(rootPathStr + BLOBS_DIR + "/" + fsIndex[path].physicalName);
        fsIndex.erase(path);
        return IndexManager::save(rootPathStr);
    }

    std::vector<String> readDir(const Path &plainDir)
    {
        String d = path2Str(plainDir);
        if (!d.endsWith("/"))
            d += "/";
        std::vector<String> res;
        for (auto const &[path, entry] : fsIndex)
        {
            if (path.startsWith(d) && path != d)
            {
                String sub = path.substring(d.length());
                if (sub.indexOf('/') == -1)
                    res.push_back(sub);
            }
        }
        return res;
    }

    // --- Storage Integration ---
    Path storagePath(const String &appId, const String &key)
    {
        Path p;
        p.push_back("programms");
        p.push_back(appId);
        p.push_back("data");
        p.push_back(CryptoHelper::sha256(key) + ".data");
        return p;
    }

    namespace Storage
    {
        Buffer get(const String &app, const String &key, long s, long e) { return readFile(ENC_FS::storagePath(app, key), s, e); }
        bool del(const String &app, const String &key) { return deleteFile(ENC_FS::storagePath(app, key)); }
        bool set(const String &app, const String &key, const Buffer &data)
        {
            Path p = ENC_FS::storagePath(app, key);
            Path cur;
            for (size_t i = 0; i < p.size() - 1; i++)
            {
                cur.push_back(p[i]);
                if (!exists(cur))
                    mkDir(cur);
            }
            return writeFile(p, 0, -1, data);
        }
    }

    // --- Stubs & Helpers ---
    Buffer readFilePart(const Path &p, long s, long e) { return readFile(p, s, e); }
    String readFileString(const Path &p)
    {
        Buffer b = readFile(p);
        String s = "";
        for (uint8_t c : b)
            s += (char)c;
        return s;
    }
    bool writeFileString(const Path &p, const String &s)
    {
        Buffer b(s.length());
        memcpy(b.data(), s.c_str(), s.length());
        return writeFile(p, 0, -1, b);
    }
    bool appendFile(const Path &p, const Buffer &data) { return false; } // Simplified
    void lsDirSerial(const Path &p)
    {
        for (auto f : readDir(p))
            Serial.println(" - " + f);
    }
    void setKdfIterations(uint32_t it) { g_kdfIterations = it; }
    void setChunkSize(size_t s) {}
    void setParityGroupSize(size_t s) {}
    void copyFileFromSPIFFS(const char *sp, const Path &sd) {}
    Metadata getMetadata(const Path &p)
    {
        Metadata m = {getFileSize(p), "", p.empty() ? "" : p.back(), false};
        return m;
    }
    bool rmDir(const Path &p) { return deleteFile(p); }

} // namespace ENC_FS