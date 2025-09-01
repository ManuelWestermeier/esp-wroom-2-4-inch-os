#include "enc-fs.hpp"

#include <mbedtls/sha256.h>
#include <mbedtls/aes.h>
#include <esp_system.h>
#include "../auth/auth.hpp"

namespace ENC_FS
{
    // ---------- Helpers ----------

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
        if (p == 0 || p > 16)
            return false;
        size_t n = b.size();
        for (size_t i = 0; i < p; ++i)
            if (b[n - 1 - i] != p)
                return false;
        b.resize(n - p);
        return true;
    }

    String base64url_encode(const uint8_t *data, size_t len)
    {
        static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
        String out;
        uint32_t val = 0;
        int valb = -6;
        for (size_t i = 0; i < len; ++i)
        {
            val = (val << 8) | data[i];
            valb += 8;
            while (valb >= 0)
            {
                out += b64[(val >> valb) & 0x3F];
                valb -= 6;
            }
        }
        if (valb > -6)
        {
            out += b64[((val << (6 + valb)) & 0x3F)];
        }
        return out;
    }

    bool base64url_decode(const String &s, Buffer &out)
    {
        static int8_t rev[256];
        static bool init = false;
        if (!init)
        {
            init = true;
            for (int i = 0; i < 256; i++)
                rev[i] = -1;
            const char *b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
            for (int i = 0; i < 64; i++)
                rev[(uint8_t)b64[i]] = i;
        }
        out.clear();
        uint32_t val = 0;
        int valb = -8;
        for (size_t i = 0; i < (size_t)s.length(); ++i)
        {
            int8_t c = rev[(uint8_t)s[i]];
            if (c == -1)
                return false;
            val = (val << 6) | c;
            valb += 6;
            if (valb >= 0)
            {
                out.push_back((uint8_t)((val >> valb) & 0xFF));
                valb -= 8;
            }
        }
        return true;
    }

    Buffer deriveKey()
    {
        String in = Auth::username + String(":") + Auth::password;
        return sha256(in);
    }

    // ---------- Path helpers ----------

    Path str2Path(const String &s)
    {
        Path p;
        if (s.length() == 0)
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
        {
            p.erase(p.begin());
        }
        return p;
    }

    String encryptSegment(const String &seg)
    {
        Buffer key = deriveKey();
        Buffer in;
        in.reserve(seg.length());
        for (size_t i = 0; i < seg.length(); ++i)
            in.push_back((uint8_t)seg[i]);
        pkcs7_pad(in);

        mbedtls_aes_context aes;
        mbedtls_aes_init(&aes);
        mbedtls_aes_setkey_enc(&aes, key.data(), 256);
        uint8_t iv[16];
        memset(iv, 0, sizeof(iv));
        Buffer outbuf;
        outbuf.resize(in.size());
        mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, in.size(), iv, in.data(), outbuf.data());
        mbedtls_aes_free(&aes);

        return base64url_encode(outbuf.data(), outbuf.size());
    }

    bool decryptSegment(const String &enc, String &outSeg)
    {
        Buffer key = deriveKey();
        Buffer encbuf;
        if (!base64url_decode(enc, encbuf))
            return false;
        if (encbuf.empty())
            return false;

        mbedtls_aes_context aes;
        mbedtls_aes_init(&aes);
        mbedtls_aes_setkey_dec(&aes, key.data(), 256);
        uint8_t iv[16];
        memset(iv, 0, sizeof(iv));
        Buffer ptxt;
        ptxt.resize(encbuf.size());
        mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, encbuf.size(), iv, encbuf.data(), ptxt.data());
        mbedtls_aes_free(&aes);
        if (!pkcs7_unpad(ptxt))
            return false;
        outSeg.clear();
        outSeg.reserve(ptxt.size());
        for (auto &c : ptxt)
            outSeg += (char)c;
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

    // ---------- AES-CTR ----------

    Buffer aes_ctr_crypt_full(const Buffer &in)
    {
        Buffer key = deriveKey();
        Buffer out;
        out.resize(in.size());

        mbedtls_aes_context aes;
        mbedtls_aes_init(&aes);
        mbedtls_aes_setkey_enc(&aes, key.data(), 256);

        size_t nc_off = 0;
        uint8_t nonce_counter[16];
        memset(nonce_counter, 0, sizeof(nonce_counter));
        uint8_t stream_block[16];
        memset(stream_block, 0, sizeof(stream_block));

        mbedtls_aes_crypt_ctr(&aes, in.size(), &nc_off, nonce_counter, stream_block, in.data(), out.data());
        mbedtls_aes_free(&aes);
        return out;
    }

    Buffer aes_ctr_crypt_offset(const Buffer &in, size_t offset)
    {
        Buffer key = deriveKey();
        Buffer out;
        out.resize(in.size());
        mbedtls_aes_context aes;
        mbedtls_aes_init(&aes);
        mbedtls_aes_setkey_enc(&aes, key.data(), 256);

        size_t block_pos = offset / 16;
        size_t block_off = offset % 16;
        size_t remaining = in.size();
        size_t written = 0;

        uint8_t counter[16];
        uint8_t keystream[16];

        while (remaining)
        {
            memset(counter, 0, sizeof(counter));
            uint64_t bp = (uint64_t)block_pos;
            for (int i = 0; i < 8; i++)
            {
                counter[15 - i] = bp & 0xFF;
                bp >>= 8;
            }
            mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, counter, keystream);

            for (size_t i = block_off; i < 16 && remaining; ++i)
            {
                out[written] = in[written] ^ keystream[i];
                ++written;
                --remaining;
            }
            block_off = 0;
            ++block_pos;
        }

        mbedtls_aes_free(&aes);
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
            SD.remove(en.c_str());
            entry.close();
            entry = f.openNextFile();
        }
        f.close();
#if defined(SD_HAS_RMDIR) || defined(RMDIR_ENABLED)
        if (SD.rmdir)
            return SD.rmdir(full.c_str());
#endif
        return true;
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
        Buffer out = aes_ctr_crypt_offset(cbuf, (size_t)start);
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
            long needLen = max((long)existingLen, start + writeLen);
            plaintext = existing;
            plaintext.resize(needLen, 0);
            for (long i = 0; i < writeLen; ++i)
                plaintext[start + i] = data[i];
        }

        Buffer cipher = aes_ctr_crypt_full(plaintext);

        if (SD.exists(full.c_str()))
            SD.remove(full.c_str());
        File fw = SD.open(full.c_str(), "w+");
        if (!fw)
            return false;
        if (!cipher.empty())
            fw.write(cipher.data(), cipher.size());
        fw.close();
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
            else
                out.push_back(String("<unknown>"));
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
        p.push_back(String("user-data-storage"));
        p.push_back(key + ".data");
        return p;
    }

    namespace Storage
    {
        Buffer get(const String &appId, const String &key, long start, long end)
        {
            Path p = storagePath(appId, key);
            return ENC_FS::readFile(p, (start < 0 ? 0 : start), end);
        }

        bool set(const String &appId, const String &key, const Buffer &data)
        {
            Path p = storagePath(appId, key);
            return ENC_FS::writeFile(p, 0, 0, data);
        }
    }
}
