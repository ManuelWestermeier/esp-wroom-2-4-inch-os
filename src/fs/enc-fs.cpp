#include "enc-fs.hpp"

#include <mbedtls/sha256.h>
#include <mbedtls/aes.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>
#include <esp_system.h>
#include <cstring>
#include <cctype>
#include <map>

namespace ENC_FS
{
    // ---------- secure zero helpers ----------
    static void secure_zero_ptr(void *ptr, size_t n)
    {
        volatile uint8_t *p = reinterpret_cast<volatile uint8_t *>(ptr);
        while (n--)
            *p++ = 0;
    }

    void secureZero(Buffer &b)
    {
        if (!b.empty())
        {
            secure_zero_ptr(b.data(), b.size());
            b.clear();
            b.shrink_to_fit();
        }
    }

    void secureZero(uint8_t *buf, size_t len)
    {
        if (buf && len)
            secure_zero_ptr(buf, len);
    }

    // ---------- Helpers ----------
    Buffer sha256(const String &s)
    {
        Buffer out(32);
        mbedtls_sha256_context ctx;
        mbedtls_sha256_init(&ctx);
        if (mbedtls_sha256_starts_ret(&ctx, 0) != 0)
        {
            mbedtls_sha256_free(&ctx);
            return Buffer();
        }
        mbedtls_sha256_update_ret(&ctx, (const unsigned char *)s.c_str(), s.length());
        mbedtls_sha256_finish_ret(&ctx, out.data());
        mbedtls_sha256_free(&ctx);
        return out;
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

    // ---------- hex encode/decode ----------
    static inline char nibble_to_hex(uint8_t n)
    {
        static const char hex[] = "0123456789abcdef";
        return hex[n & 0xF];
    }

    String base64url_encode(const uint8_t *data, size_t len)
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

    // ---------- Keys: master and per-folder ----------
    static std::map<String, Buffer> g_folderKeyCache;
    static Buffer g_masterKeyCache;

    Buffer deriveMasterKey()
    {
        if (!g_masterKeyCache.empty())
            return g_masterKeyCache;

        String secret = Auth::username + String(":") + Auth::password;

        uint8_t mac[6];
        if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != 0)
            memset(mac, 0, sizeof(mac));

        const char label[] = "encfs-master";
        uint8_t salt[sizeof(label) - 1 + sizeof(mac)];
        memcpy(salt, label, sizeof(label) - 1);
        memcpy(salt + (sizeof(label) - 1), mac, sizeof(mac));

        Buffer key(32);
        mbedtls_md_context_t ctx;
        const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
        mbedtls_md_init(&ctx);
        if (md_info == nullptr)
        {
            mbedtls_md_free(&ctx);
            return Buffer();
        }
        if (mbedtls_md_setup(&ctx, md_info, 1) != 0)
        {
            mbedtls_md_free(&ctx);
            return Buffer();
        }
        const unsigned int iterations = 10000;
        if (mbedtls_pkcs5_pbkdf2_hmac(&ctx,
                                      (const unsigned char *)secret.c_str(), secret.length(),
                                      salt, sizeof(salt),
                                      iterations,
                                      (uint32_t)key.size(), key.data()) != 0)
        {
            mbedtls_md_free(&ctx);
            secureZero(key);
            return Buffer();
        }
        mbedtls_md_free(&ctx);

        g_masterKeyCache = key;
        return g_masterKeyCache;
    }

    Buffer deriveFolderKey(const String &folder)
    {
        auto it = g_folderKeyCache.find(folder);
        if (it != g_folderKeyCache.end())
            return it->second;

        Buffer master = deriveMasterKey();
        if (master.empty())
            return Buffer();

        uint8_t mac[6];
        if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != 0)
            memset(mac, 0, sizeof(mac));

        const char label[] = "encfs-folder";
        size_t slen = sizeof(label) - 1 + sizeof(mac) + folder.length();
        Buffer salt;
        salt.resize(slen);
        size_t pos = 0;
        memcpy(salt.data() + pos, label, sizeof(label) - 1);
        pos += sizeof(label) - 1;
        memcpy(salt.data() + pos, mac, sizeof(mac));
        pos += sizeof(mac);
        memcpy(salt.data() + pos, folder.c_str(), folder.length());

        Buffer key(32);
        mbedtls_md_context_t ctx;
        const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
        mbedtls_md_init(&ctx);
        if (md_info == nullptr)
        {
            mbedtls_md_free(&ctx);
            secureZero(salt);
            return Buffer();
        }
        if (mbedtls_md_setup(&ctx, md_info, 1) != 0)
        {
            mbedtls_md_free(&ctx);
            secureZero(salt);
            return Buffer();
        }
        const unsigned int iterations = 8000;
        if (mbedtls_pkcs5_pbkdf2_hmac(&ctx,
                                      master.data(), master.size(),
                                      salt.data(), salt.size(),
                                      iterations,
                                      (uint32_t)key.size(), key.data()) != 0)
        {
            mbedtls_md_free(&ctx);
            secureZero(key);
            secureZero(salt);
            return Buffer();
        }
        mbedtls_md_free(&ctx);

        g_folderKeyCache[folder] = key;
        secureZero(salt);
        secureZero(master);
        return g_folderKeyCache[folder];
    }

    // ---------- Path helpers ----------
    bool isValidSegment(const String &seg)
    {
        if (seg.length() == 0)
            return false;
        if (seg == "." || seg == "..")
            return false;
        for (size_t i = 0; i < seg.length(); ++i)
        {
            char c = seg[i];
            if (c == '/' || c == '\\' || c == 0)
                return false;
        }
        return true;
    }

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
        for (const auto &seg : p)
            if (!isValidSegment(seg))
                return Path();
        return p;
    }

    String path2Str(const Path &p)
    {
        String out = p.size() ? "" : "/";
        for (const String &part : p)
            out += "/" + part;
        return out;
    }

    // ---------- HMAC helper ----------
    static bool hmac_sha256(const Buffer &key, const uint8_t *data, size_t len, Buffer &out)
    {
        const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
        if (!md_info)
            return false;
        out.resize(32);
        if (mbedtls_md_hmac(md_info, key.data(), key.size(), data, len, out.data()) != 0)
            return false;
        return true;
    }

    // ---------- Segment encryption ----------
    String encryptSegment(const String &seg)
    {
        if (!isValidSegment(seg))
            return String("");

        Buffer key = deriveMasterKey();
        if (key.empty())
            return String("");

        size_t L = seg.length();
        if (L > 0xFFFF)
            L = 0xFFFF;
        Buffer in;
        in.reserve(2 + L + 16);
        in.push_back((uint8_t)((L >> 8) & 0xFF));
        in.push_back((uint8_t)(L & 0xFF));
        for (size_t i = 0; i < L; ++i)
            in.push_back((uint8_t)seg[i]);
        size_t rem = in.size() % 16;
        if (rem != 0)
            in.insert(in.end(), 16 - rem, 0x00);

        if (in.empty())
        {
            secureZero(key);
            return String("");
        }

        mbedtls_aes_context aes;
        mbedtls_aes_init(&aes);
        if (mbedtls_aes_setkey_enc(&aes, key.data(), 256) != 0)
        {
            mbedtls_aes_free(&aes);
            secureZero(key);
            secureZero(in);
            return String("");
        }

        Buffer outbuf;
        outbuf.resize(in.size());
        uint8_t in_block[16];
        uint8_t out_block[16];

        for (size_t off = 0; off < in.size(); off += 16)
        {
            memcpy(in_block, in.data() + off, 16);
            if (mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, in_block, out_block) != 0)
            {
                mbedtls_aes_free(&aes);
                secureZero(key);
                secureZero(in);
                secureZero(outbuf);
                return String("");
            }
            memcpy(outbuf.data() + off, out_block, 16);
        }
        mbedtls_aes_free(&aes);

        Buffer tag;
        if (!hmac_sha256(key, outbuf.data(), outbuf.size(), tag))
        {
            secureZero(key);
            secureZero(in);
            secureZero(outbuf);
            return String("");
        }

        Buffer concat;
        concat.reserve(outbuf.size() + tag.size());
        concat.insert(concat.end(), outbuf.begin(), outbuf.end());
        concat.insert(concat.end(), tag.begin(), tag.end());
        String encoded = base64url_encode(concat.data(), concat.size());

        secureZero(key);
        secureZero(in);
        secureZero(outbuf);
        secureZero(concat);
        secureZero(tag);
        return encoded;
    }

    bool decryptSegment(const String &enc, String &outSeg)
    {
        outSeg.clear();
        if (enc.length() == 0)
            return true;

        Buffer key = deriveMasterKey();
        if (key.empty())
            return false;

        Buffer encbuf;
        if (!base64url_decode(enc, encbuf))
        {
            secureZero(key);
            return false;
        }
        if (encbuf.empty())
        {
            secureZero(key);
            return false;
        }
        if (encbuf.size() < 16 + 32)
        {
            secureZero(key);
            secureZero(encbuf);
            return false;
        }

        size_t tag_offset = encbuf.size() - 32;
        Buffer ciphertext(encbuf.begin(), encbuf.begin() + tag_offset);
        Buffer tag(encbuf.begin() + tag_offset, encbuf.end());

        Buffer computed;
        if (!hmac_sha256(key, ciphertext.data(), ciphertext.size(), computed) ||
            computed.size() != tag.size() || memcmp(computed.data(), tag.data(), tag.size()) != 0)
        {
            secureZero(key);
            secureZero(encbuf);
            secureZero(ciphertext);
            secureZero(tag);
            secureZero(computed);
            return false;
        }
        secureZero(computed);

        if (ciphertext.size() % 16 != 0)
        {
            secureZero(key);
            secureZero(encbuf);
            secureZero(ciphertext);
            secureZero(tag);
            return false;
        }

        mbedtls_aes_context aes;
        mbedtls_aes_init(&aes);
        if (mbedtls_aes_setkey_dec(&aes, key.data(), 256) != 0)
        {
            mbedtls_aes_free(&aes);
            secureZero(key);
            secureZero(encbuf);
            secureZero(ciphertext);
            secureZero(tag);
            return false;
        }

        Buffer ptxt(ciphertext.size());
        uint8_t in_block[16];
        uint8_t out_block[16];

        for (size_t off = 0; off < ciphertext.size(); off += 16)
        {
            memcpy(in_block, ciphertext.data() + off, 16);
            if (mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, in_block, out_block) != 0)
            {
                mbedtls_aes_free(&aes);
                secureZero(key);
                secureZero(encbuf);
                secureZero(ciphertext);
                secureZero(tag);
                secureZero(ptxt);
                return false;
            }
            memcpy(ptxt.data() + off, out_block, 16);
        }
        mbedtls_aes_free(&aes);

        if (ptxt.size() < 2)
        {
            secureZero(key);
            secureZero(encbuf);
            secureZero(ciphertext);
            secureZero(tag);
            secureZero(ptxt);
            return false;
        }

        uint16_t len = ((uint16_t)ptxt[0] << 8) | (uint16_t)ptxt[1];
        if ((size_t)len > ptxt.size() - 2)
        {
            secureZero(key);
            secureZero(encbuf);
            secureZero(ciphertext);
            secureZero(tag);
            secureZero(ptxt);
            return false;
        }

        outSeg.reserve(len);
        for (size_t i = 0; i < len; ++i)
            outSeg += (char)ptxt[2 + i];

        secureZero(key);
        secureZero(encbuf);
        secureZero(ciphertext);
        secureZero(tag);
        secureZero(ptxt);
        return true;
    }

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

    // ---------- derive per-file nonce ----------
    static void derive_file_nonce(const Path &p, uint8_t out_nonce[16])
    {
        String plainPath = path2Str(p); // deterministic
        Buffer h = sha256(plainPath);
        memset(out_nonce, 0, 8);
        memcpy(out_nonce + 8, h.data(), 8);
        secureZero(h);
    }

    // ---------- AES-CTR (file contents) ----------
    Buffer aes_ctr_crypt_offset(const Path &p, const Buffer &in, size_t offset)
    {
        String folder = "/";
        if (!p.empty())
        {
            Path parent = p;
            if (!parent.empty())
                parent.pop_back();
            folder = path2Str(parent);
        }
        Buffer key = deriveFolderKey(folder);
        if (key.empty())
            return Buffer();

        Buffer out;
        out.resize(in.size());

        mbedtls_aes_context aes;
        mbedtls_aes_init(&aes);
        if (mbedtls_aes_setkey_enc(&aes, key.data(), 256) != 0)
        {
            mbedtls_aes_free(&aes);
            secureZero(key);
            secureZero(out);
            return Buffer();
        }

        uint8_t nonce_counter[16];
        uint8_t stream_block[16];
        memset(stream_block, 0, sizeof(stream_block));
        derive_file_nonce(p, nonce_counter);

        uint64_t block_pos = offset / 16;
        for (int i = 0; i < 8; ++i)
            nonce_counter[15 - i] = (uint8_t)((block_pos >> (8 * i)) & 0xFF);
        size_t nc_off = offset % 16;

        if (mbedtls_aes_crypt_ctr(&aes, in.size(), &nc_off, nonce_counter, stream_block, in.data(), out.data()) != 0)
        {
            mbedtls_aes_free(&aes);
            secureZero(key);
            secureZero(out);
            return Buffer();
        }

        mbedtls_aes_free(&aes);
        secureZero(key);
        secureZero(nonce_counter, sizeof(nonce_counter));
        secureZero(stream_block, sizeof(stream_block));
        return out;
    }

    Buffer aes_ctr_crypt_full(const Path &p, const Buffer &in)
    {
        return aes_ctr_crypt_offset(p, in, 0);
    }

    // ---------- API functions ----------
    bool exists(const Path &p)
    {
        return SD.exists(joinEncPath(p).c_str());
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
                if (!SD.mkdir(accum.c_str()))
                    return false;
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
            return SD.remove(full.c_str());

        if (!f.isDirectory())
        {
            f.close();
            return SD.remove(full.c_str());
        }

        File entry = f.openNextFile();
        while (entry)
        {
            String en = String(entry.name());
            int lastSlash = en.lastIndexOf('/');
            String nameOnly = (lastSlash >= 0) ? en.substring(lastSlash + 1) : en;
            String dec;
            if (decryptSegment(nameOnly, dec))
            {
                Path child = p;
                child.push_back(dec);
                if (entry.isDirectory())
                    rmDir(child);
                else
                    deleteFile(child);
            }
            entry.close();
            entry = f.openNextFile();
        }
        f.close();
        return SD.remove(full.c_str());
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
        if (len == 0)
        {
            f.close();
            return Buffer();
        }
        Buffer cbuf(len);
        f.seek(start);
        int got = f.read(cbuf.data(), len);
        f.close();
        if (got <= 0)
            return Buffer();
        if (got != len)
            cbuf.resize(got);
        return aes_ctr_crypt_offset(p, cbuf, start);
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
        String s((const char *)b.data(), b.size());
        secureZero(b);
        return s;
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
            plaintext = data;
        else
        {
            Buffer existing = readFile(p, 0, -1);
            long existingLen = existing.size();
            if (start < 0)
                start = 0;
            if (end < 0)
                end = start + data.size();
            long writeLen = end - start;
            long needLen = std::max(existingLen, start + writeLen);
            plaintext = existing;
            plaintext.resize(needLen, 0);
            for (long i = 0; i < writeLen; ++i)
                plaintext[start + i] = data[i];
            secureZero(existing);
        }

        Buffer cipher = aes_ctr_crypt_full(p, plaintext);
        secureZero(plaintext);

        if (SD.exists(full.c_str()))
            SD.remove(full.c_str());
        File fw = SD.open(full.c_str(), FILE_WRITE);
        if (!fw)
        {
            secureZero(cipher);
            return false;
        }
        if (!cipher.empty())
            fw.write(cipher.data(), cipher.size());
        fw.close();
        secureZero(cipher);
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
        Buffer b(s.begin(), s.end());
        bool res = writeFile(p, 0, 0, b);
        secureZero(b);
        return res;
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
            m.decryptedName = "<enc>";
        f.close();
        return m;
    }

    std::vector<String> readDir(const Path &plainDir)
    {
        std::vector<String> out;
        File dir = SD.open(joinEncPath(plainDir).c_str());
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
        p.push_back("programms");
        p.push_back(appId);
        p.push_back("data");
        p.push_back(Crypto::HASH::sha256String(key) + ".data");
        return p;
    }

    namespace Storage
    {
        Buffer get(const String &appId, const String &key, long start, long end)
        {
            return ENC_FS::readFile(storagePath(appId, key), (start < 0 ? 0 : start), end);
        }

        bool del(const String &appId, const String &key)
        {
            return ENC_FS::deleteFile(storagePath(appId, key));
        }

        bool set(const String &appId, const String &key, const Buffer &data)
        {
            return ENC_FS::writeFile(storagePath(appId, key), 0, 0, data);
        }
    }

    void copyFileFromSPIFFS(const char *spiffsPath, const Path &sdPath)
    {
        File src = SPIFFS.open(spiffsPath, FILE_READ);
        if (!src || src.isDirectory())
            return;

        String accum = "/";
        accum += Auth::username;
        for (size_t i = 0; i + 1 < sdPath.size(); ++i)
        {
            accum += "/";
            accum += encryptSegment(sdPath[i]);
            if (!SD.exists(accum.c_str()))
                SD.mkdir(accum.c_str());
        }

        String destEncPath = joinEncPath(sdPath);
        File dest = SD.open(destEncPath.c_str(), FILE_WRITE);
        if (!dest)
        {
            src.close();
            return;
        }

        constexpr size_t bufSize = 1024;
        uint8_t buffer[bufSize];
        size_t offset = 0;
        while (true)
        {
            size_t bytesRead = src.read(buffer, bufSize);
            if (bytesRead == 0)
                break;

            Buffer chunk(buffer, buffer + bytesRead);
            Buffer cipher = aes_ctr_crypt_offset(sdPath, chunk, offset);
            if (dest.write(cipher.data(), cipher.size()) != (int)cipher.size())
            {
                dest.close();
                src.close();
                SD.remove(destEncPath.c_str());
                secureZero(chunk);
                secureZero(cipher);
                return;
            }
            offset += bytesRead;
            secureZero(chunk);
            secureZero(cipher);
        }
        dest.close();
        src.close();
    }

} // namespace ENC_FS
