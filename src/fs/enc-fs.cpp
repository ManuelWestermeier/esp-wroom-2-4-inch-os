#include "enc-fs.hpp"

#include <mbedtls/sha256.h>
#include <mbedtls/aes.h>
#include <mbedtls/md.h>
#include <esp_system.h>
#include "../auth/auth.hpp"
#include <cstring>
#include <cctype>
#include <algorithm>

#define DERIVE_ITERATIONS 1

namespace ENC_FS
{
    // ---------- Helpers ----------

    Buffer sha256(const String &s)
    {
        Buffer out(32);
        mbedtls_sha256_context ctx;
        mbedtls_sha256_init(&ctx);
        mbedtls_sha256_starts_ret(&ctx, 0); // SHA-256
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

    // ---------- Hex encode/decode (used instead of base64url for robustness) ----------
    // We keep the function names base64url_encode/decode to match the header.

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

    Buffer deriveKey()
    {
        static Buffer key(32);
        static bool created = false;
        if (created)
            return key;

        String in = Crypto::HASH::sha256StringMul(Auth::username + String(":") + Auth::password, DERIVE_ITERATIONS);
        Buffer h = sha256(in); // 32 bytes
        memcpy(key.data(), h.data(), 32);

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

    // ---------- Name token (deterministic, HMAC-SHA256) ----------
    // We derive a deterministic token for a filename from: HMAC(key, username + ":" + parentPlain + ":" + name)
    // This token is used as the on-disk filename (hex). The original plaintext name is stored in a small
    // metadata sibling file encrypted with the file-content scheme so it remains confidential.

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

        return base64url_encode(h, 32); // 64 hex chars
    }

    // ---------- Metadata (per-name) raw writer/reader ----------
    // We store a small metadata file alongside each encrypted name: <encName>.namemeta
    // The metadata file is written directly to SD as ciphertext (AES-CTR using the same per-file nonce scheme)
    // so it can be recovered by readers who know the key.

    static bool writeNameMetaRaw(const String &parentEncPath, const String &encName, const String &plainName)
    {
        // meta file path
        String metaFull = parentEncPath + String("/") + encName + String(".namemeta");

        // prepare plaintext payload: 2-byte BE length + name bytes
        size_t L = plainName.length();
        if (L > 0xFFFF)
            L = 0xFFFF;
        Buffer ptxt;
        ptxt.reserve(2 + L);
        ptxt.push_back((uint8_t)((L >> 8) & 0xFF));
        ptxt.push_back((uint8_t)(L & 0xFF));
        for (size_t i = 0; i < L; ++i)
            ptxt.push_back((uint8_t)plainName[i]);

        // derive deterministic nonce for this new meta file version = 1
        uint64_t version = 1;
        uint8_t nonce[16];
        deriveNonceForFullPathVersion(metaFull, version, nonce);

        Buffer cipher = aes_ctr_crypt_full_with_nonce(ptxt, nonce);

        // write raw ciphertext to SD and persist version metadata
        if (SD.exists(metaFull.c_str()))
            SD.remove(metaFull.c_str());
        File f = SD.open(metaFull.c_str(), "w+");
        if (!f)
            return false;
        if (!cipher.empty())
            f.write(cipher.data(), cipher.size());
        f.close();

        // persist the version file for this meta
        writeVersionForFullPath(metaFull, version);

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

        uint64_t version = readVersionForFullPath(metaFull);
        if (version == 0)
            version = 1; // legacy

        uint8_t nonce[16];
        deriveNonceForFullPathVersion(metaFull, version, nonce);

        Buffer ptxt = aes_ctr_crypt_full_with_nonce(cbuf, nonce);
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

    // ---------- Robust segment encryption/decryption (deterministic token + per-name metadata) ----------
    // Deterministic token (HMAC) used as on-disk filename. Original plaintext is stored in an encrypted
    // sibling metadata file so it can be recovered when required.

    // New signature: encryptSegment(segment, parentPlainPath, parentEncPath)
    String encryptSegment(const String &seg, const String &parentPlain, const String &parentEnc)
    {
        if (seg.length() == 0)
            return String("");

        String token = makeNameToken(parentPlain, seg);

        // ensure metadata exists (if absent, create it)
        String metaFull = parentEnc + String("/") + token + String(".namemeta");
        if (!SD.exists(metaFull.c_str()))
        {
            // attempt to write metadata; ignore failure (best-effort)
            writeNameMetaRaw(parentEnc, token, seg);
        }

        return token;
    }

    // decryptSegment now requires parentEncPath to find the metadata sibling
    bool decryptSegment(const String &enc, const String &parentEnc, String &outSeg)
    {
        if (enc.length() == 0)
        {
            outSeg = String("");
            return true;
        }

        // read metadata file and decrypt it
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

            // update plain accum for next iteration
            if (accumPlain.length() == 0)
                accumPlain = String("/") + segment;
            else
                accumPlain += String("/") + segment;
        }
        return accumEnc;
    }

    // ---------- Deterministic per-file nonce (IV) support ----------
    static String ivMetaSuffix = String(".ivmeta");

    static uint64_t readVersionForFullPath(const String &full)
    {
        String metaPath = full + ivMetaSuffix;
        File f = SD.open(metaPath.c_str(), FILE_READ);
        if (!f)
            return 0; // 0 indicates missing -> treat as version 0 (will be incremented to 1 on write)
        uint8_t buf[8];
        int got = f.read(buf, 8);
        f.close();
        if (got != 8)
            return 0;
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i)
            v = (v << 8) | (uint64_t)buf[i];
        return v;
    }

    static bool writeVersionForFullPath(const String &full, uint64_t v)
    {
        String metaPath = full + ivMetaSuffix;
        // overwrite or create
        if (SD.exists(metaPath.c_str()))
            SD.remove(metaPath.c_str());
        File f = SD.open(metaPath.c_str(), "w+");
        if (!f)
            return false;
        uint8_t buf[8];
        for (int i = 7; i >= 0; --i)
        {
            buf[i] = (uint8_t)(v & 0xFF);
            v >>= 8;
        }
        size_t wrote = f.write(buf, 8);
        f.close();
        return wrote == 8;
    }

    static void deriveNonceForFullPathVersion(const String &full, uint64_t version, uint8_t nonce[16])
    {
        // input string: username + ":" + full + ":" + decimal(version)
        String input = Auth::username + String(":") + full + String(":") + String((unsigned long long)version);
        Buffer h = sha256(input);
        // copy first 16 bytes
        memcpy(nonce, h.data(), 16);
    }

    // ---------- AES-CTR for file contents (updated to use per-file deterministic nonces) ----------

    Buffer aes_ctr_crypt_full_with_nonce(const Buffer &in, const uint8_t nonce[16])
    {
        Buffer key = deriveKey();
        Buffer out;
        out.resize(in.size());

        mbedtls_aes_context aes;
        mbedtls_aes_init(&aes);
        mbedtls_aes_setkey_enc(&aes, key.data(), 256);

        size_t nc_off = 0;
        uint8_t nonce_counter[16];
        memcpy(nonce_counter, nonce, 16);
        uint8_t stream_block[16];
        memset(stream_block, 0, sizeof(stream_block));

        mbedtls_aes_crypt_ctr(&aes, in.size(), &nc_off, nonce_counter, stream_block, in.data(), out.data());
        mbedtls_aes_free(&aes);
        return out;
    }

    Buffer aes_ctr_crypt_offset_with_nonce(const Buffer &in, size_t offset, const uint8_t nonce[16])
    {
        Buffer key = deriveKey();
        Buffer out;
        out.resize(in.size());
        mbedtls_aes_context aes;
        mbedtls_aes_init(&aes);
        mbedtls_aes_setkey_enc(&aes, key.data(), 256);

        // CTR counter is nonce || counter. We emulate mbedtls_crypt_ctr behavior by constructing
        // a counter block where the last 8 bytes are the block counter. We'll copy nonce into
        // the first 16 bytes (treating it as the initial counter) and then increment the 128-bit
        // counter as blocks advance. For compatibility with deriveNonceForFullPathVersion
        // we treat the provided nonce as the initial 16-byte counter value.
        size_t block_pos = offset / 16;
        size_t block_off = offset % 16;
        size_t remaining = in.size();
        size_t written = 0;

        // working counter (16 bytes)
        uint8_t counter[16];
        uint8_t keystream[16];

        while (remaining)
        {
            // set counter = nonce + block_pos (add as little-endian to the last 8 bytes)
            memcpy(counter, nonce, 16);
            uint64_t bp = (uint64_t)block_pos;
            // add bp into last 8 bytes little-endian style as CTR often uses LSB side
            // We'll XOR/ADD in little-endian manner into last 8 bytes.
            for (int i = 0; i < 8; ++i)
            {
                uint16_t sum = (uint16_t)counter[15 - i] + (uint16_t)(bp & 0xFF);
                counter[15 - i] = (uint8_t)(sum & 0xFF);
                bp >>= 8;
                if (sum >> 8)
                {
                    // carry propagation
                    uint64_t carry_pos = 1;
                    while (carry_pos < 16)
                    {
                        int idx = 15 - i - carry_pos;
                        if (idx < 0)
                            break;
                        uint16_t s2 = (uint16_t)counter[idx] + 1;
                        counter[idx] = (uint8_t)(s2 & 0xFF);
                        if ((s2 >> 8) == 0)
                            break;
                        ++carry_pos;
                    }
                }
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

    // ---------- API (modified to maintain per-file deterministic nonce version) ----------

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

            // update plain accum
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
        // Serial.printf("[rmDir] Called for path: %s\n", full.c_str());

        if (!SD.exists(full.c_str()))
        {
            // Serial.printf("[rmDir] Path does not exist: %s\n", full.c_str());
            return false;
        }

        File f = SD.open(full.c_str());
        if (!f)
        {
            // Serial.printf("[rmDir] Failed to open, attempting remove(): %s\n", full.c_str());
            bool res = SD.remove(full.c_str());
            // Serial.printf("[rmDir] Remove result: %d\n", res);
            return res;
        }

        if (!f.isDirectory())
        {
            // Serial.printf("[rmDir] Not a directory, removing file: %s\n", full.c_str());
            f.close();
            // remove associated iv meta as well
            String metaPath = String(full) + ivMetaSuffix;
            if (SD.exists(metaPath.c_str()))
                SD.remove(metaPath.c_str());
            // remove namemeta sibling
            String nameMeta = String(full) + String(".namemeta");
            if (SD.exists(nameMeta.c_str()))
                SD.remove(nameMeta.c_str());
            bool res = SD.remove(full.c_str());
            // Serial.printf("[rmDir] File remove result: %d\n", res);
            return res;
        }

        // Serial.printf("[rmDir] Directory found, cleaning contents...\n");
        File entry = f.openNextFile();
        while (entry)
        {
            String en = String(entry.name());
            // Serial.printf("[rmDir] Found entry: %s\n", en.c_str());

            if (entry.isDirectory())
            {
                // Serial.printf("[rmDir] Entry is directory, recursing via SD_FS::deleteDir()\n");
                bool res = SD_FS::deleteDir(en);
                // Serial.printf("[rmDir] SD_FS::deleteDir('%s') => %d\n", en.c_str(), res);
            }
            else
            {
                // remove associated iv meta if present
                String metaPath = en + ivMetaSuffix;
                if (SD.exists(metaPath.c_str()))
                    SD.remove(metaPath.c_str());
                // remove namemeta
                String nameMeta = en + String(".namemeta");
                if (SD.exists(nameMeta.c_str()))
                    SD.remove(nameMeta.c_str());
                bool res = SD.remove(en.c_str());
                // Serial.printf("[rmDir] Removed file %s => %d\n", en.c_str(), res);
            }

            entry.close();
            entry = f.openNextFile();
        }

        f.close();
        // Serial.printf("[rmDir] Directory empty, attempting to remove folder itself: %s\n", full.c_str());

        bool res = SD_FS::deleteDir(full);
        // Serial.printf("[rmDir] Final SD_FS::deleteDir('%s') => %d\n", full.c_str(), res);

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
        if (start == end && end <= 0)
        {
            start = 0;
            end = f.size();
        }
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

        // obtain version (if missing, treat as 0)
        uint64_t version = readVersionForFullPath(full);
        if (version == 0)
        {
            // legacy/no-meta: treat as version 1 to be compatible with future writes
            version = 1;
        }

        uint8_t nonce[16];
        deriveNonceForFullPathVersion(full, version, nonce);

        Buffer out = aes_ctr_crypt_offset_with_nonce(cbuf, (size_t)start, nonce);
        return out;
    }

    Buffer readFile(const Path &p, long start, long end)
    {
        return readFilePart(p, start, end);
    }

    Buffer readFileFull(const Path &p)
    {
        return readFilePart(p, -1, -1);
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

        // increment version for this file to avoid keystream reuse
        uint64_t curVersion = readVersionForFullPath(full);
        uint64_t newVersion = (curVersion == 0) ? 1 : (curVersion + 1);
        // derive nonce from newVersion (deterministic)
        uint8_t nonce[16];
        deriveNonceForFullPathVersion(full, newVersion, nonce);

        Buffer cipher = aes_ctr_crypt_full_with_nonce(plaintext, nonce);

        if (SD.exists(full.c_str()))
            SD.remove(full.c_str());
        File fw = SD.open(full.c_str(), "w+");
        if (!fw)
            return false;
        if (!cipher.empty())
            fw.write(cipher.data(), cipher.size());
        fw.close();

        // persist new version
        writeVersionForFullPath(full, newVersion);

        // ensure name meta exists for the file (create sibling .namemeta if missing)
        // determine parentEnc path
        int lastSlash = full.lastIndexOf('/');
        String parentEnc = (lastSlash >= 0) ? full.substring(0, lastSlash) : String("");
        String encName = (lastSlash >= 0) ? full.substring(lastSlash + 1) : full;
        // try to write name meta if missing
        String metaFull = parentEnc + String("/") + encName + String(".namemeta");
        if (!SD.exists(metaFull.c_str()))
        {
            // derive plain parent path from p (reconstruct)
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
        // remove associated iv meta too
        String metaPath = full + ivMetaSuffix;
        if (SD.exists(metaPath.c_str()))
            SD.remove(metaPath.c_str());
        // remove name meta sibling
        String nameMeta = full + String(".namemeta");
        if (SD.exists(nameMeta.c_str()))
            SD.remove(nameMeta.c_str());
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

        // parentEnc path
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

    namespace BrowserStorage
    {
        inline void makeSureBrowserStorageExist()
        {
            if (!exists({"browser"}))
            {
                mkDir({"browser"});
            }
        }
        // get set delete from browser/domain.data
        Buffer get(const String &domain)
        {
            makeSureBrowserStorageExist();
            // Returns the encrypted file content for the specific domain
            return readFileFull({"browser", domain + ".data"});
        }

        bool del(const String &domain)
        {
            makeSureBrowserStorageExist();
            // Deletes the specific domain data file and its associated metadata
            return deleteFile({"browser", domain + ".data"});
        }

        std::vector<String> listSites()
        {
            makeSureBrowserStorageExist();

            vector<String> out;
            auto files = readDir({"browser"});

            for (const String &file : files)
            {
                if (file.endsWith(".data"))
                {
                    out.push_back(file.substring(0, file.length() - 5));
                }
            }

            return out;
        }

        bool set(const String &domain, const Buffer &data)
        {
            makeSureBrowserStorageExist();
            // Writes the buffer to /browser/domain.data (encrypted via ENC_FS)
            return writeFile({"browser", domain + ".data"}, 0, 0, data);
        }

        bool clearAll()
        {
            // Removes the entire browser directory and all domain files within it
            return rmDir({"browser"});
        }
    }

    void copyFileFromSPIFFS(const char *spiffsPath, const Path &sdPath)
    {
        File src = SPIFFS.open(spiffsPath, FILE_READ);
        if (!src || src.isDirectory())
            return;

        File dest = SD.open(path2Str(sdPath), FILE_WRITE);
        if (!dest)
        {
            src.close();
            return;
        }

        constexpr size_t bufSize = 1024;
        uint8_t buffer[bufSize];

        while (true)
        {
            size_t bytesRead = src.read(buffer, bufSize);
            if (bytesRead == 0)
                break;
            if (dest.write(buffer, bytesRead) != bytesRead)
            {
                dest.close();
                src.close();
                SD.remove(path2Str(sdPath));
                return;
            }
        }

        dest.close();
        src.close();
    }
} // namespace ENC_FS
