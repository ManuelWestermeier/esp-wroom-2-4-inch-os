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
    // ---------- Utilities ----------

    static inline void secure_zero(void *p, size_t n)
    {
        volatile uint8_t *q = (volatile uint8_t *)p;
        while (n--)
            *q++ = 0;
    }

    Buffer sha256(const String &s)
    {
        Buffer out(32);
        mbedtls_sha256_context ctx;
        mbedtls_sha256_init(&ctx);
        mbedtls_sha256_starts_ret(&ctx, 0);
        mbedtls_sha256_update_ret(&ctx, (const unsigned char *)s.c_str(), s.length());
        mbedtls_sha256_finish_ret(&ctx, out.data());
        mbedtls_sha256_free(&ctx);
        return out;
    }

    // Hex encode / decode (small, robust)
    static inline char nibble_to_hex(uint8_t n)
    {
        static const char hex[] = "0123456789abcdef";
        return hex[n & 0xF];
    }
    String hex_encode(const uint8_t *data, size_t len)
    {
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

    bool hex_decode(const String &s, Buffer &out)
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

    // constant-time compare
    static inline bool ct_compare(const uint8_t *a, const uint8_t *b, size_t n)
    {
        uint8_t r = 0;
        for (size_t i = 0; i < n; ++i)
            r |= a[i] ^ b[i];
        return r == 0;
    }

    // ---------- Key derivation (PBKDF2 with device-unique salt) ----------
    Buffer deriveKey()
    {
        static Buffer key(32);
        static bool created = false;
        if (created)
            return key;

        // derive a salt from device MAC (stable per device) + username
        uint8_t mac[6] = {0};
        esp_efuse_mac_get_default(mac);

        // salt = mac || username
        Buffer salt;
        salt.resize(6 + Auth::username.length());
        memcpy(salt.data(), mac, 6);
        for (size_t i = 0; i < Auth::username.length(); ++i)
            salt[6 + i] = (uint8_t)Auth::username[i];

        // PBKDF2-HMAC-SHA256
        mbedtls_md_context_t md_ctx;
        mbedtls_md_init(&md_ctx);

        const mbedtls_md_info_t *md_info =
            mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

        mbedtls_md_setup(&md_ctx, md_info, 1);

        const unsigned int iterations = 10000;

        String pw = Auth::username + String(":") + Auth::password;

        mbedtls_pkcs5_pbkdf2_hmac(
            &md_ctx,
            (const unsigned char *)pw.c_str(),
            pw.length(),
            salt.data(),
            salt.size(),
            iterations,
            key.size(),
            key.data());

        mbedtls_md_free(&md_ctx);

        // wipe sensitive temporaries
        secure_zero(salt.data(), salt.size());
        secure_zero((void *)pw.c_str(), pw.length());

        created = true;
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
        String out = p.size() ? "" : "/";
        for (const String &part : p)
            out += "/" + part;
        return out;
    }

    // ---------- Deterministic name token using HMAC-SHA256 ----------
    static String makeNameToken(const String &parentPlain, const String &name)
    {
        Buffer key = deriveKey();
        String input = Auth::username + String(":") + parentPlain + String(":") + name;

        unsigned char h[32];
        mbedtls_md_context_t ctx;
        mbedtls_md_init(&ctx);
        const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
        mbedtls_md_setup(&ctx, info, 1);
        mbedtls_md_hmac_starts(&ctx, key.data(), key.size());
        mbedtls_md_hmac_update(&ctx, (const unsigned char *)input.c_str(), input.length());
        mbedtls_md_hmac_finish(&ctx, h);
        mbedtls_md_free(&ctx);

        String r = hex_encode(h, 32);
        secure_zero(h, sizeof(h));
        return r; // 64 hex chars
    }

    // ---------- AEAD helpers (AES-256-GCM) ----------
    // File format: | 12 bytes IV | ciphertext... | 16 bytes tag |

    static uint32_t get_random_u32()
    {
        return (uint32_t)esp_random();
    }

    Buffer aes_gcm_encrypt(const Buffer &plaintext)
    {
        Buffer out;
        // allocate: iv(12) + ciphertext + tag(16)
        const size_t iv_len = 12;
        const size_t tag_len = 16;

        Buffer key = deriveKey();

        // generate IV: 12 bytes from esp_random
        uint8_t iv[iv_len];
        uint32_t r1 = get_random_u32();
        uint32_t r2 = get_random_u32();
        uint32_t r3 = get_random_u32();
        memcpy(iv, &r1, 4);
        memcpy(iv + 4, &r2, 4);
        memcpy(iv + 8, &r3, 4);

        size_t ct_len = plaintext.size();
        out.resize(iv_len + ct_len + tag_len);
        memcpy(out.data(), iv, iv_len);

        mbedtls_gcm_context gcm;
        mbedtls_gcm_init(&gcm);
        mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key.data(), 256);

        int ret = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT,
                                            ct_len,
                                            iv, iv_len,
                                            nullptr, 0, // additional data (none)
                                            plaintext.data(), out.data() + iv_len,
                                            tag_len, out.data() + iv_len + ct_len);
        mbedtls_gcm_free(&gcm);

        // wipe key copy
        secure_zero(key.data(), key.size());
        (void)ret; // ignore ret for now; caller should ensure success
        return out;
    }

    bool aes_gcm_decrypt(const Buffer &in, Buffer &out)
    {
        const size_t iv_len = 12;
        const size_t tag_len = 16;
        if (in.size() < iv_len + tag_len)
            return false;
        size_t ct_len = in.size() - iv_len - tag_len;
        const uint8_t *iv = in.data();
        const uint8_t *ct = in.data() + iv_len;
        const uint8_t *tag = in.data() + iv_len + ct_len;

        Buffer key = deriveKey();

        mbedtls_gcm_context gcm;
        mbedtls_gcm_init(&gcm);
        mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key.data(), 256);

        out.clear();
        out.resize(ct_len);
        int ret = mbedtls_gcm_auth_decrypt(&gcm, ct_len, iv, iv_len, nullptr, 0, tag, tag_len, ct, out.data());

        mbedtls_gcm_free(&gcm);
        secure_zero(key.data(), key.size());
        return ret == 0;
    }

    // ---------- Name metadata encryption helpers (small payloads) ----------
    static bool writeNameMetaRaw(const String &parentEncPath, const String &encName, const String &plainName)
    {
        String metaFull = parentEncPath + String("/") + encName + String(".namemeta");

        size_t L = plainName.length();
        if (L > 0xFFFF)
            L = 0xFFFF;
        Buffer ptxt;
        ptxt.reserve(2 + L);
        ptxt.push_back((uint8_t)((L >> 8) & 0xFF));
        ptxt.push_back((uint8_t)(L & 0xFF));
        for (size_t i = 0; i < L; ++i)
            ptxt.push_back((uint8_t)plainName[i]);

        Buffer cipher = aes_gcm_encrypt(ptxt);
        if (SD.exists(metaFull.c_str()))
            SD.remove(metaFull.c_str());
        File f = SD.open(metaFull.c_str(), "w+");
        if (!f)
            return false;
        if (!cipher.empty())
            f.write(cipher.data(), cipher.size());
        f.close();
        return true;
    }

    static bool readNameMetaRaw(const String &parentEncPath, const String &encName, String &outName)
    {
        String metaFull = parentEncPath + String("/") + encName + String(".namemeta");
        File f = SD.open(metaFull.c_str(), FILE_READ);
        if (!f)
            return false;
        long fsize = f.size();
        if (fsize <= 0)
        {
            f.close();
            return false;
        }
        Buffer cbuf;
        cbuf.resize(fsize);
        int got = f.read(cbuf.data(), fsize);
        f.close();
        if (got <= 0)
            return false;
        cbuf.resize(got);

        Buffer ptxt;
        if (!aes_gcm_decrypt(cbuf, ptxt))
            return false;
        if (ptxt.size() < 2)
            return false;
        uint16_t len = ((uint16_t)ptxt[0] << 8) | (uint16_t)ptxt[1];
        if ((size_t)len > ptxt.size() - 2)
            return false;
        outName.clear();
        outName.reserve(len);
        for (size_t i = 0; i < len; ++i)
            outName += (char)ptxt[2 + i];
        return true;
    }

    // ---------- Segment encryption (creates deterministic encrypted filename token) ----------
    String encryptSegment(const String &seg, const String &parentPlain, const String &parentEnc)
    {
        if (seg.length() == 0)
            return String("");
        String token = makeNameToken(parentPlain, seg);
        // best-effort: ensure metadata exists
        String metaFull = parentEnc + String("/") + token + String(".namemeta");
        if (!SD.exists(metaFull.c_str()))
        {
            writeNameMetaRaw(parentEnc, token, seg);
        }
        return token;
    }

    bool decryptSegment(const String &enc, const String &parentEnc, String &outSeg)
    {
        if (enc.length() == 0)
        {
            outSeg = String("");
            return true;
        }
        if (readNameMetaRaw(parentEnc, enc, outSeg))
            return true;
        return false;
    }

    String joinEncPath(const Path &plain)
    {
        String r = "/";
        r += Auth::username;
        String accumPlain = String("");
        String accumEnc = r;
        for (size_t i = 0; i < plain.size(); ++i)
        {
            String segment = plain[i];
            String enc = encryptSegment(segment, accumPlain, accumEnc);
            accumEnc += "/";
            accumEnc += enc;
            if (accumPlain.length() == 0)
                accumPlain = String("/") + segment;
            else
                accumPlain += String("/") + segment;
        }
        return accumEnc;
    }

    // ---------- API implementations ----------
    bool exists(const Path &p)
    {
        String full = joinEncPath(p);
        return SD.exists(full.c_str());
    }

    bool mkDir(const Path &p)
    {
        String accumPlain = String("");
        String accumEnc = String("/") + Auth::username;
        for (size_t i = 0; i < p.size(); ++i)
        {
            String seg = p[i];
            String enc = encryptSegment(seg, accumPlain, accumEnc);
            accumEnc += String("/") + enc;
            if (!SD.exists(accumEnc.c_str()))
                SD.mkdir(accumEnc.c_str());
            if (accumPlain.length() == 0)
                accumPlain = String("/") + seg;
            else
                accumPlain += String("/") + seg;
        }
        return true;
    }

    bool rmDir(const Path &p)
    {
        String full = joinEncPath(p);
        if (!SD.exists(full.c_str()))
            return false;
        File f = SD.open(full.c_str());
        if (!f)
        {
            return SD.remove(full.c_str());
        }
        if (!f.isDirectory())
        {
            f.close();
            SD.remove(full.c_str());
            return true;
        }
        File entry = f.openNextFile();
        while (entry)
        {
            String en = String(entry.name());
            if (entry.isDirectory())
            {
                rmDir(str2Path(en));
            }
            else
            {
                SD.remove(en.c_str());
                String nameMeta = en + String(".namemeta");
                if (SD.exists(nameMeta.c_str()))
                    SD.remove(nameMeta.c_str());
            }
            entry.close();
            entry = f.openNextFile();
        }
        f.close();
        return SD.rmdir(full.c_str());
    }

    Buffer readFile(const Path &p)
    {
        Buffer empty;
        String full = joinEncPath(p);
        File f = SD.open(full.c_str(), FILE_READ);
        if (!f)
            return empty;
        long fsize = f.size();
        if (fsize <= 0)
        {
            f.close();
            return empty;
        }
        Buffer cbuf;
        cbuf.resize(fsize);
        int got = f.read(cbuf.data(), fsize);
        f.close();
        if (got <= 0)
            return empty;
        cbuf.resize(got);
        Buffer out;
        if (!aes_gcm_decrypt(cbuf, out))
            return Buffer();
        return out;
    }

    Buffer readFilePart(const Path &p, long start, long end)
    {
        Buffer full = readFile(p);
        if (full.empty())
            return Buffer();
        long fsize = (long)full.size();
        if (end <= 0 || end > fsize)
            end = fsize;
        if (start < 0)
            start = 0;
        if (start > end)
            start = end;
        Buffer out;
        out.resize(end - start);
        for (long i = start; i < end; ++i)
            out[i - start] = full[i];
        return out;
    }

    String readFileString(const Path &p)
    {
        Buffer b = readFile(p);
        if (b.empty())
            return String("");
        return String((const char *)b.data(), b.size());
    }

    bool writeFile(const Path &p, const Buffer &data)
    {
        String full = joinEncPath(p);
        // ensure parent dirs exist
        String accumPlain = String("");
        String accumEnc = String("/") + Auth::username;
        for (size_t i = 0; i + 1 < p.size(); ++i)
        {
            accumPlain = (accumPlain.length() == 0) ? String("/") + p[i] : accumPlain + String("/") + p[i];
            String enc = encryptSegment(p[i], accumPlain, accumEnc);
            accumEnc += String("/") + enc;
            if (!SD.exists(accumEnc.c_str()))
                SD.mkdir(accumEnc.c_str());
        }

        Buffer cipher = aes_gcm_encrypt(data);
        if (SD.exists(full.c_str()))
            SD.remove(full.c_str());
        File fw = SD.open(full.c_str(), "w+");
        if (!fw)
            return false;
        if (!cipher.empty())
            fw.write(cipher.data(), cipher.size());
        fw.close();

        // ensure name meta exists
        int lastSlash = full.lastIndexOf('/');
        String parentEnc = (lastSlash >= 0) ? full.substring(0, lastSlash) : String("");
        String encName = (lastSlash >= 0) ? full.substring(lastSlash + 1) : full;
        String metaFull = parentEnc + String("/") + encName + String(".namemeta");
        if (!SD.exists(metaFull.c_str()))
        {
            String parentPlain = String("");
            for (size_t i = 0; i + 1 < p.size(); ++i)
            {
                if (parentPlain.length() == 0)
                    parentPlain = String("/") + p[i];
                else
                    parentPlain += String("/") + p[i];
            }
            writeNameMetaRaw(parentEnc, encName, p.back());
        }
        return true;
    }

    bool writeFile(const Path &p, long start, long end, const Buffer &data)
    {
        // perform full-file read-modify-write (AES-GCM requires whole-file AEAD here)
        Buffer existing = readFile(p);
        long existingLen = existing.size();
        if (start < 0)
            start = 0;
        if (end < 0)
            end = start + (long)data.size();
        long writeLen = end - start;
        long needLen = max((long)existingLen, start + writeLen);
        Buffer plaintext = existing;
        plaintext.resize(needLen, 0);
        for (long i = 0; i < writeLen; ++i)
            plaintext[start + i] = data[i];
        return writeFile(p, plaintext);
    }

    bool appendFile(const Path &p, const Buffer &data)
    {
        Buffer existing = readFile(p);
        long pos = existing.size();
        existing.insert(existing.end(), data.begin(), data.end());
        return writeFile(p, existing);
    }

    bool writeFileString(const Path &p, const String &s)
    {
        Buffer b;
        b.reserve(s.length());
        for (size_t i = 0; i < s.length(); ++i)
            b.push_back((uint8_t)s[i]);
        return writeFile(p, b);
    }

    bool deleteFile(const Path &p)
    {
        String full = joinEncPath(p);
        if (!SD.exists(full.c_str()))
            return false; // remove name meta sibling
        String nameMeta = full + String(".namemeta");
        if (SD.exists(nameMeta.c_str()))
            SD.remove(nameMeta.c_str());
        return SD.remove(full.c_str());
    }

    long getFileSize(const Path &p)
    {
        Buffer b = readFile(p);
        if (b.empty())
            return -1;
        return (long)b.size();
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
        String parentEnc = (lastSlash >= 0) ? m.encryptedName.substring(0, lastSlash) : String("");
        String dec;
        if (decryptSegment(last, parentEnc, dec))
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
            if (decryptSegment(nameOnly, encPath, dec))
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
        p.push_back(hex_encode(sha256(key).data(), 32) + String(".data"));
        return p;
    }

    namespace Storage
    {
        Buffer get(const String &appId, const String &key, long start, long end)
        {
            Path p = storagePath(appId, key);
            if (start < 0 && end < 0)
                return ENC_FS::readFile(p);
            return ENC_FS::readFilePart(p, (start < 0 ? 0 : start), end);
        }
        bool del(const String &appId, const String &key)
        {
            Path p = storagePath(appId, key);
            return ENC_FS::deleteFile(p);
        }
        bool set(const String &appId, const String &key, const Buffer &data)
        {
            Path p = storagePath(appId, key);
            return ENC_FS::writeFile(p, data);
        }
    }

    void copyFileFromSPIFFS(const char *spiffsPath, const Path &sdPath)
    {
        File src = SPIFFS.open(spiffsPath, FILE_READ);
        if (!src || src.isDirectory())
            return;
        Buffer b;
        b.reserve(src.size());
        const size_t bufSize = 1024;
        uint8_t buffer[bufSize];
        while (true)
        {
            size_t bytesRead = src.read(buffer, bufSize);
            if (bytesRead == 0)
                break;
            for (size_t i = 0; i < bytesRead; ++i)
                b.push_back(buffer[i]);
        }
        src.close();
        writeFile(sdPath, b);
    }

} // namespace ENC_FS
