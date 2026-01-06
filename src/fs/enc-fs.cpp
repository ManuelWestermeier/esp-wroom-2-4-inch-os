#include "enc-fs.hpp"

#include <mbedtls/sha256.h>
#include <mbedtls/aes.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>

#include <esp_system.h>
#include "../auth/auth.hpp"
#include <cstring>
#include <cctype>
#include <algorithm>

namespace ENC_FS
{
    // ---------- Helpers ----------

    Buffer sha256(const Buffer &data)
    {
        Buffer out(32);
        mbedtls_sha256_context ctx;
        mbedtls_sha256_init(&ctx);
        mbedtls_sha256_starts_ret(&ctx, 0); // SHA-256
        mbedtls_sha256_update_ret(&ctx, data.data(), data.size());
        mbedtls_sha256_finish_ret(&ctx, out.data());
        mbedtls_sha256_free(&ctx);
        return out;
    }

    Buffer sha256(const String &s)
    {
        Buffer tmp;
        tmp.resize(s.length());
        if (!tmp.empty())
            memcpy(tmp.data(), s.c_str(), s.length());
        return sha256(tmp);
    }

    void secure_zero(void *p, size_t n)
    {
        if (p && n)
        {
            volatile uint8_t *vp = (volatile uint8_t *)p;
            while (n--)
                *vp++ = 0;
        }
    }

    void pkcs7_pad(Buffer &b)
    {
        const size_t B = 16;
        size_t pad = B - (b.size() % B);
        if (pad == 0)
            pad = B;
        b.insert(b.end(), pad, (uint8_t)pad);
    }

    bool pkcs7_unpad(Buffer &b)
    {
        if (b.empty())
            return false;
        uint8_t p = b.back();
        if (p == 0 || p > 16 || p > b.size())
            return false;
        size_t n = b.size();
        for (size_t i = 0; i < p; ++i)
            if (b[n - 1 - i] != p)
                return false;
        b.resize(n - p);
        return true;
    }

    // ---------- Hex encode/decode (used instead of base64url for robustness) ----------

    static inline char nibble_to_hex(uint8_t n)
    {
        static const char hex[] = "0123456789abcdef";
        return hex[n & 0xF];
    }

    String base64url_encode(const uint8_t *data, size_t len)
    {
        // hex encoding: 2 chars per byte
        String out;
        out.reserve(len * 2);
        for (size_t i = 0; i < len; ++i)
        {
            uint8_t b = data[i];
            out += nibble_to_hex(b >> 4);
            out += nibble_to_hex(b & 0x0F);
        }
        return out;
    }

    static inline int hexval(char c)
    {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    }

    bool base64url_decode(const String &s, Buffer &out)
    {
        size_t L = s.length();
        if (L % 2 != 0)
            return false;
        size_t bytes = L / 2;
        out.clear();
        out.resize(bytes);
        for (size_t i = 0; i < bytes; ++i)
        {
            int hi = hexval(s.charAt(i * 2));
            int lo = hexval(s.charAt(i * 2 + 1));
            if (hi < 0 || lo < 0)
                return false;
            out[i] = (uint8_t)((hi << 4) | lo);
        }
        return true;
    }

    // ---------- Key derivation (PBKDF2-HMAC-SHA256) ----------

    Buffer deriveKey()
    {
        static Buffer key(32);
        static bool initialized = false;

        if (initialized)
            return key;

        const unsigned char *pwd = (const unsigned char *)Auth::password.c_str();
        size_t pwdlen = Auth::password.length();
        const unsigned char *salt = (const unsigned char *)Auth::username.c_str();
        size_t saltlen = Auth::username.length();
        const unsigned int iterations = 10000; // adjustable for device capability

        mbedtls_md_context_t md_ctx;
        const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

        mbedtls_md_init(&md_ctx);
        int setup_ret = mbedtls_md_setup(&md_ctx, md_info, 1); // HMAC = 1
        if (setup_ret == 0)
        {
            int ret = mbedtls_pkcs5_pbkdf2_hmac(&md_ctx,
                                                pwd,
                                                pwdlen,
                                                salt,
                                                saltlen,
                                                iterations,
                                                (uint32_t)key.size(),
                                                key.data());
            mbedtls_md_free(&md_ctx);
            if (ret == 0)
                return key;
            // fall through to fallback
        }
        else
        {
            mbedtls_md_free(&md_ctx);
        }

        // fallback to simple SHA256(username:password) if pbkdf2 fails
        String in = Auth::username + String(":") + Auth::password;
        Buffer h = sha256(in);
        memcpy(key.data(), h.data(), std::min((size_t)32, h.size()));
        secure_zero(h.data(), h.size());

        initialized = true;

        return key;
    }

    // ---------- Path helpers ----------

    Path str2Path(const String &s)
    {
        Path p;
        if (s.length() == 0 || s == "/")
            return p;
        String t = s;
        while (t.startsWith("/"))
            t = t.substring(1);
        while (t.endsWith("/"))
            t = t.substring(0, t.length() - 1);
        if (t.length() == 0)
            return p;
        int idx = 0;
        while (idx < t.length())
        {
            int j = t.indexOf('/', idx);
            if (j == -1)
            {
                p.push_back(t.substring(idx));
                break;
            }
            p.push_back(t.substring(idx, j));
            idx = j + 1;
        }
        if (!p.empty() && p[0] == Auth::username)
            p.erase(p.begin());
        return p;
    }

    String path2Str(const Path &p)
    {
        if (p.empty())
            return String("/");
        String out = "";
        for (const String &part : p)
        {
            out += "/";
            out += part;
        }
        return out;
    }

    // ---------- Deterministic, authenticated segment encryption ----------
    // Format stored: [IV(12)] [ciphertext] [tag(16)], then hex-encoded.

    String encryptSegment_det(const String &seg)
    {
        if (seg.length() == 0)
            return String("");

        Buffer key = deriveKey();

        // Derive IV deterministically from key || seg: sha256(key || seg)[:12]
        Buffer hdata;
        hdata.resize(key.size() + seg.length());
        memcpy(hdata.data(), key.data(), key.size());
        if (seg.length())
            memcpy(hdata.data() + key.size(), seg.c_str(), seg.length());
        Buffer h = sha256(hdata);
        uint8_t iv[12];
        memcpy(iv, h.data(), 12);

        mbedtls_gcm_context gcm;
        mbedtls_gcm_init(&gcm);
        if (mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key.data(), 256) != 0)
        {
            mbedtls_gcm_free(&gcm);
            secure_zero(key.data(), key.size());
            secure_zero(hdata.data(), hdata.size());
            secure_zero(h.data(), h.size());
            return String("");
        }

        size_t plain_len = seg.length();
        Buffer outbuf;
        outbuf.resize(12 + plain_len + 16); // iv + ciphertext + tag
        // copy iv
        memcpy(outbuf.data(), iv, 12);
        uint8_t *cipher_ptr = outbuf.data() + 12;
        uint8_t *tag_ptr = cipher_ptr + plain_len;

        int ret = mbedtls_gcm_crypt_and_tag(&gcm,
                                            MBEDTLS_GCM_ENCRYPT,
                                            plain_len,
                                            iv, sizeof(iv),
                                            NULL, 0,
                                            (const unsigned char *)seg.c_str(),
                                            cipher_ptr,
                                            16, tag_ptr);
        mbedtls_gcm_free(&gcm);

        // clear sensitive temporaries
        secure_zero(key.data(), key.size());
        secure_zero(hdata.data(), hdata.size());
        secure_zero(h.data(), h.size());

        if (ret != 0)
            return String("");

        String out = base64url_encode(outbuf.data(), outbuf.size());
        secure_zero(outbuf.data(), outbuf.size());
        return out;
    }

    bool decryptSegment(const String &enc, String &outSeg)
    {
        if (enc.length() == 0)
        {
            outSeg = String("");
            return true;
        }

        Buffer key = deriveKey();
        Buffer encbuf;
        if (!base64url_decode(enc, encbuf))
        {
            secure_zero(key.data(), key.size());
            return false;
        }
        if (encbuf.size() < (12 + 16))
        {
            secure_zero(key.data(), key.size());
            return false;
        }

        const uint8_t *iv = encbuf.data();
        const size_t ct_and_tag_len = encbuf.size() - 12;
        const uint8_t *ct_and_tag = encbuf.data() + 12;
        size_t ct_len = ct_and_tag_len - 16;
        const uint8_t *ciphertext = ct_and_tag;
        const uint8_t *tag = ct_and_tag + ct_len;

        mbedtls_gcm_context gcm;
        mbedtls_gcm_init(&gcm);
        if (mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key.data(), 256) != 0)
        {
            mbedtls_gcm_free(&gcm);
            secure_zero(key.data(), key.size());
            return false;
        }

        Buffer ptxt;
        ptxt.resize(ct_len);
        int ret = mbedtls_gcm_auth_decrypt(&gcm,
                                           ct_len,
                                           iv, 12,
                                           NULL, 0,
                                           tag, 16,
                                           ciphertext,
                                           ptxt.data());
        mbedtls_gcm_free(&gcm);
        secure_zero(key.data(), key.size());

        if (ret != 0)
            return false;

        outSeg.clear();
        outSeg.reserve(ptxt.size());
        for (size_t i = 0; i < ptxt.size(); ++i)
            outSeg += (char)ptxt[i];

        secure_zero(ptxt.data(), ptxt.size());
        return true;
    }

    // Public encryptSegment forwards to deterministic implementation.
    String encryptSegment(const String &seg) { return encryptSegment_det(seg); }

    String joinEncPath(const Path &plain)
    {
        String r = "/";
        r += Auth::username;
        for (size_t i = 0; i < plain.size(); ++i)
        {
            r += "/";
            r += encryptSegment(plain[i]);
        }
        return r;
    }

    // ---------- AES-CTR for file contents (per-file IV derived from full encrypted path) ----------

    static void add_block_to_counter(uint8_t counter[16], uint64_t add)
    {
        // Treat last 8 bytes as big-endian counter
        uint64_t carry = add;
        for (int i = 15; i >= 8; --i)
        {
            uint64_t v = ((uint64_t)counter[i]) + (carry & 0xFF);
            counter[i] = (uint8_t)(v & 0xFF);
            carry = (v >> 8);
        }
        for (int i = 7; i >= 0 && carry; --i)
        {
            uint64_t v = ((uint64_t)counter[i]) + (carry & 0xFF);
            counter[i] = (uint8_t)(v & 0xFF);
            carry = (v >> 8);
        }
    }

    Buffer aes_ctr_crypt_full(const Buffer &in, const String &fullEncPath)
    {
        Buffer key = deriveKey();
        Buffer out;
        out.resize(in.size());

        // derive 16-byte IV from sha256(key || fullEncPath)
        Buffer tmp;
        tmp.resize(key.size() + fullEncPath.length());
        memcpy(tmp.data(), key.data(), key.size());
        if (fullEncPath.length())
            memcpy(tmp.data() + key.size(), fullEncPath.c_str(), fullEncPath.length());
        Buffer h = sha256(tmp);
        uint8_t nonce_counter[16];
        memcpy(nonce_counter, h.data(), 16);

        mbedtls_aes_context aes;
        mbedtls_aes_init(&aes);
        mbedtls_aes_setkey_enc(&aes, key.data(), 256);

        size_t nc_off = 0;
        uint8_t stream_block[16];
        memset(stream_block, 0, sizeof(stream_block));

        mbedtls_aes_crypt_ctr(&aes, in.size(), &nc_off, nonce_counter, stream_block, in.data(), out.data());

        // cleanup
        mbedtls_aes_free(&aes);
        secure_zero(key.data(), key.size());
        secure_zero(tmp.data(), tmp.size());
        secure_zero(h.data(), h.size());
        secure_zero(nonce_counter, sizeof(nonce_counter));
        secure_zero(stream_block, sizeof(stream_block));
        return out;
    }

    Buffer aes_ctr_crypt_offset(const Buffer &in, size_t offset, const String &fullEncPath)
    {
        Buffer key = deriveKey();
        Buffer out;
        out.resize(in.size());
        mbedtls_aes_context aes;
        mbedtls_aes_init(&aes);
        mbedtls_aes_setkey_enc(&aes, key.data(), 256);

        // derive nonce from key || fullEncPath
        Buffer tmp;
        tmp.resize(key.size() + fullEncPath.length());
        memcpy(tmp.data(), key.data(), key.size());
        if (fullEncPath.length())
            memcpy(tmp.data() + key.size(), fullEncPath.c_str(), fullEncPath.length());
        Buffer h = sha256(tmp);
        uint8_t counter[16];
        memcpy(counter, h.data(), 16);

        size_t block_pos = offset / 16;
        size_t block_off = offset % 16;

        // add block_pos to the counter (so we start from correct block)
        add_block_to_counter(counter, block_pos);

        size_t nc_off = block_off;
        uint8_t stream_block[16];
        memset(stream_block, 0, sizeof(stream_block));

        mbedtls_aes_crypt_ctr(&aes, in.size(), &nc_off, counter, stream_block, in.data(), out.data());

        // cleanup
        mbedtls_aes_free(&aes);
        secure_zero(key.data(), key.size());
        secure_zero(tmp.data(), tmp.size());
        secure_zero(h.data(), h.size());
        secure_zero(counter, sizeof(counter));
        secure_zero(stream_block, sizeof(stream_block));
        return out;
    }

    // ---------- API ----------

    bool exists(const Path &p)
    {
        String full = joinEncPath(p);
        return SD.exists(full.c_str());
    }

    bool mkDir(const Path &p)
    {
        String accum = "/";
        accum += Auth::username;
        for (size_t i = 0; i < p.size(); ++i)
        {
            accum += "/";
            accum += encryptSegment(p[i]);
            if (!SD.exists(accum.c_str()))
                SD.mkdir(accum.c_str());
        }
        return true;
    }

    bool rmDir(const Path &p)
    {
        String full = joinEncPath(p);
        Serial.printf("[rmDir] Called for path: %s\n", full.c_str());

        if (!SD.exists(full.c_str()))
        {
            Serial.printf("[rmDir] Path does not exist: %s\n", full.c_str());
            return false;
        }

        File f = SD.open(full.c_str());
        if (!f)
        {
            Serial.printf("[rmDir] Failed to open, attempting remove(): %s\n", full.c_str());
            bool res = SD.remove(full.c_str());
            Serial.printf("[rmDir] Remove result: %d\n", res);
            return res;
        }

        if (!f.isDirectory())
        {
            Serial.printf("[rmDir] Not a directory, removing file: %s\n", full.c_str());
            f.close();
            bool res = SD.remove(full.c_str());
            Serial.printf("[rmDir] File remove result: %d\n", res);
            return res;
        }

        Serial.printf("[rmDir] Directory found, cleaning contents...\n");
        File entry = f.openNextFile();
        while (entry)
        {
            String en = String(entry.name());
            Serial.printf("[rmDir] Found entry: %s\n", en.c_str());

            if (entry.isDirectory())
            {
                Serial.printf("[rmDir] Entry is directory, recursing via SD_FS::deleteDir()\n");
                bool res = SD_FS::deleteDir(en);
                Serial.printf("[rmDir] SD_FS::deleteDir('%s') => %d\n", en.c_str(), res);
            }
            else
            {
                bool res = SD.remove(en.c_str());
                Serial.printf("[rmDir] Removed file %s => %d\n", en.c_str(), res);
            }

            entry.close();
            entry = f.openNextFile();
        }

        f.close();
        Serial.printf("[rmDir] Directory empty, attempting to remove folder itself: %s\n", full.c_str());

        bool res = SD_FS::deleteDir(full);
        Serial.printf("[rmDir] Final SD_FS::deleteDir('%s') => %d\n", full.c_str(), res);

        return res;
    }

    Buffer readFilePart(const Path &p, long start, long end)
    {
        Buffer empty;
        String full = joinEncPath(p);
        File f = SD.open(full.c_str(), FILE_READ);
        if (!f)
            return empty;
        long fsize = f.size();
        if (end <= 0 || end > fsize)
            end = fsize;
        if (start < 0)
            start = 0;
        if (start > end)
            start = end;
        long len = end - start;
        Buffer cbuf;
        cbuf.resize(len);
        f.seek(start);
        int got = f.read(cbuf.data(), len);
        f.close();
        if (got <= 0)
            return Buffer();
        cbuf.resize(got);
        Buffer out = aes_ctr_crypt_offset(cbuf, (size_t)start, full);
        return out;
    }

    Buffer readFile(const Path &p, long start, long end)
    {
        return readFilePart(p, start, end);
    }

    String readFileString(const Path &p)
    {
        Buffer b = readFile(p, 0, -1);
        if (b.empty())
            return String("");
        return String((const char *)b.data(), b.size());
    }

    bool writeFile(const Path &p, long start, long end, const Buffer &data)
    {
        String full = joinEncPath(p);

        String accum = "/";
        accum += Auth::username;
        for (size_t i = 0; i + 1 < p.size(); ++i)
        {
            accum += "/";
            accum += encryptSegment(p[i]);
            if (!SD.exists(accum.c_str()))
                SD.mkdir(accum.c_str());
        }

        Buffer plaintext;
        if (start == 0 && end == 0)
        {
            plaintext = data;
        }
        else
        {
            Buffer existing = readFile(p, 0, -1);
            long existingLen = existing.size();
            if (start < 0)
                start = 0;
            if (end < 0)
                end = start + (long)data.size();
            long writeLen = end - start;
            long needLen = std::max((long)existingLen, start + writeLen);
            plaintext = existing;
            plaintext.resize(needLen, 0);
            for (long i = 0; i < writeLen; ++i)
                plaintext[start + i] = data[i];
        }

        Buffer cipher = aes_ctr_crypt_full(plaintext, full);

        if (SD.exists(full.c_str()))
            SD.remove(full.c_str());
        File fw = SD.open(full.c_str(), "w+");
        if (!fw)
            return false;
        if (!cipher.empty())
            fw.write(cipher.data(), cipher.size());
        fw.close();
        secure_zero(plaintext.data(), plaintext.size());
        secure_zero(cipher.data(), cipher.size());
        return true;
    }

    bool appendFile(const Path &p, const Buffer &data)
    {
        String full = joinEncPath(p);
        File f = SD.open(full.c_str(), FILE_WRITE);
        if (!f)
            return false;
        long pos = f.size();
        f.close();
        return writeFile(p, pos, pos + (long)data.size(), data);
    }

    bool writeFileString(const Path &p, const String &s)
    {
        Buffer b;
        b.reserve(s.length());
        for (size_t i = 0; i < s.length(); ++i)
            b.push_back((uint8_t)s[i]);
        return writeFile(p, 0, 0, b);
    }

    bool deleteFile(const Path &p)
    {
        String full = joinEncPath(p);
        if (!SD.exists(full.c_str()))
            return false;
        return SD.remove(full.c_str());
    }

    long getFileSize(const Path &p)
    {
        String full = joinEncPath(p);
        File f = SD.open(full.c_str(), FILE_READ);
        if (!f)
            return -1;
        long s = f.size();
        f.close();
        return s;
    }

    Metadata getMetadata(const Path &p)
    {
        Metadata m;
        String full = joinEncPath(p);
        File f = SD.open(full.c_str());
        if (!f)
        {
            m.size = -1;
            m.encryptedName = "";
            m.decryptedName = "";
            m.isDirectory = false;
            return m;
        }
        m.size = f.size();
        m.encryptedName = String(f.name());
        m.isDirectory = f.isDirectory();
        int lastSlash = m.encryptedName.lastIndexOf('/');
        String last = (lastSlash >= 0) ? m.encryptedName.substring(lastSlash + 1) : m.encryptedName;
        String dec;
        if (decryptSegment(last, dec))
            m.decryptedName = dec;
        else
            m.decryptedName = String("<enc>");
        f.close();
        return m;
    }

    std::vector<String> readDir(const Path &plainDir)
    {
        std::vector<String> out;
        String encPath = joinEncPath(plainDir);
        File dir = SD.open(encPath.c_str());
        if (!dir || !dir.isDirectory())
            return out;
        File e = dir.openNextFile();
        while (e)
        {
            String en = String(e.name());
            int lastSlash = en.lastIndexOf('/');
            String nameOnly = (lastSlash >= 0) ? en.substring(lastSlash + 1) : en;
            String dec;
            if (decryptSegment(nameOnly, dec))
                out.push_back(dec);
            e.close();
            e = dir.openNextFile();
        }
        dir.close();
        return out;
    }

    void lsDirSerial(const Path &plainDir)
    {
        auto v = readDir(plainDir);
        for (auto &s : v)
            Serial.println(s);
    }

    Path storagePath(const String &appId, const String &key)
    {
        Path p;
        p.push_back(String("programms"));
        p.push_back(appId);
        p.push_back(String("data"));
        p.push_back(Crypto::HASH::sha256String(key) + ".data");
        return p;
    }

    namespace Storage
    {
        Buffer get(const String &appId, const String &key, long start, long end)
        {
            Path p = storagePath(appId, key);
            return ENC_FS::readFile(p, (start < 0 ? 0 : start), end);
        }

        bool del(const String &appId, const String &key)
        {
            Path p = storagePath(appId, key);
            return ENC_FS::deleteFile(p);
        }

        bool set(const String &appId, const String &key, const Buffer &data)
        {
            Path p = storagePath(appId, key);
            return ENC_FS::writeFile(p, 0, 0, data);
        }
    }

    void copyFileFromSPIFFS(const char *spiffsPath, const Path &sdPath)
    {
        File src = SPIFFS.open(spiffsPath, FILE_READ);
        if (!src || src.isDirectory())
            return;

        String encDestPath = joinEncPath(sdPath);
        File dest = SD.open(encDestPath.c_str(), FILE_WRITE);
        if (!dest)
        {
            src.close();
            return;
        }

        constexpr size_t bufSize = 1024;
        uint8_t buffer[bufSize];

        // Read raw from spiffs, encrypt with AES-CTR (per-file IV derived from encDestPath) and write.
        Buffer accum;
        while (true)
        {
            size_t bytesRead = src.read(buffer, bufSize);
            if (bytesRead == 0)
                break;
            accum.insert(accum.end(), buffer, buffer + bytesRead);
        }

        Buffer cipher = aes_ctr_crypt_full(accum, encDestPath);
        if (!cipher.empty())
        {
            dest.write(cipher.data(), cipher.size());
        }

        dest.close();
        src.close();

        secure_zero(accum.data(), accum.size());
        secure_zero(cipher.data(), cipher.size());
    }

} // namespace ENC_FS
