/*
  enc-fs.cpp

  Implementation of ENC_FS (simplified but functional) for ESP32 (Arduino)
  Uses: SD.h, SPIFFS.h, mbedTLS for crypto primitives available on ESP32
*/

#include "enc-fs.hpp"

// mbedTLS
#include "mbedtls/md.h"
#include "mbedtls/sha256.h"
#include "mbedtls/aes.h"

#include <SPI.h>
#include <SD.h>
#include <SPIFFS.h>
#include <esp_system.h>

#include <algorithm>
#include <cstring>
#include <iomanip>

namespace ENC_FS
{

    // -------- Configuration & internal state --------
    static String s_rootFolder = "/encfs";
    static Buffer s_master_key(32, 0); // 256-bit
    static size_t s_chunkSize = 4096;
    static size_t s_parityGroupSize = 4;
    static uint32_t s_kdfIterations = 100000; // default; tune to device

    // ---------- Utilities ----------
    static String toHex(const uint8_t *data, size_t len)
    {
        String out;
        out.reserve(len * 2);
        const char hex[] = "0123456789abcdef";
        for (size_t i = 0; i < len; ++i)
        {
            out += hex[(data[i] >> 4) & 0xF];
            out += hex[data[i] & 0xF];
        }
        return out;
    }

    static Buffer hexToBin(const String &hex)
    {
        Buffer out;
        size_t n = hex.length();
        out.reserve(n / 2);
        for (size_t i = 0; i + 1 < n; i += 2)
        {
            char a = hex[i];
            char b = hex[i + 1];
            auto val = [](char c) -> uint8_t
            {
                if (c >= '0' && c <= '9')
                    return c - '0';
                if (c >= 'a' && c <= 'f')
                    return 10 + (c - 'a');
                if (c >= 'A' && c <= 'F')
                    return 10 + (c - 'A');
                return 0;
            };
            out.push_back((uint8_t)((val(a) << 4) | val(b)));
        }
        return out;
    }

    // SHA256 -> output 32 bytes
    static void sha256(const uint8_t *in, size_t inlen, uint8_t out32[32])
    {
        mbedtls_sha256_ret(in, inlen, out32, 0);
    }

    // sha256 string convenience
    namespace Crypto
    {
        namespace HASH
        {
            String sha256String(const String &s)
            {
                uint8_t out[32];
                sha256((const uint8_t *)s.c_str(), s.length(), out);
                return toHex(out, 32);
            }
        }
    }

    // HMAC-SHA256
    static void hmac_sha256(const uint8_t *key, size_t keylen, const uint8_t *data, size_t datalen, uint8_t out32[32])
    {
        const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
        mbedtls_md_context_t ctx;
        mbedtls_md_init(&ctx);
        mbedtls_md_setup(&ctx, md_info, 1);
        mbedtls_md_hmac_starts(&ctx, key, keylen);
        mbedtls_md_hmac_update(&ctx, data, datalen);
        mbedtls_md_hmac_finish(&ctx, out32);
        mbedtls_md_free(&ctx);
    }

    // AES-256-CBC with PKCS7. IV is 16 bytes. Returns Buffer = IV || ciphertext
    static Buffer aes256_cbc_encrypt(const uint8_t key[32], const uint8_t *plaintext, size_t plen)
    {
        // PKCS7 padding
        size_t block = 16;
        size_t padlen = block - (plen % block);
        size_t total = plen + padlen;

        Buffer in;
        in.reserve(total);
        in.insert(in.end(), plaintext, plaintext + plen);
        for (size_t i = 0; i < padlen; ++i)
            in.push_back((uint8_t)padlen);

        // random IV
        uint8_t iv[16];
        for (int i = 0; i < 16; ++i)
        {
            uint32_t r = esp_random();
            iv[i] = (uint8_t)(r & 0xFF);
        }

        Buffer ciphertext(total);
        mbedtls_aes_context aes;
        mbedtls_aes_init(&aes);
        mbedtls_aes_setkey_enc(&aes, key, 256);
        uint8_t iv_copy[16];
        memcpy(iv_copy, iv, 16);
        mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, total, iv_copy, in.data(), ciphertext.data());
        mbedtls_aes_free(&aes);

        Buffer out;
        out.reserve(16 + total);
        out.insert(out.end(), iv, iv + 16);
        out.insert(out.end(), ciphertext.begin(), ciphertext.end());
        return out;
    }

    // aes decrypt expecting input = IV || ciphertext
    static bool aes256_cbc_decrypt(const uint8_t key[32], const uint8_t *in, size_t inlen, Buffer &plaintext_out)
    {
        if (inlen < 16)
            return false;
        const uint8_t *iv = in;
        const uint8_t *ciphertext = in + 16;
        size_t clen = inlen - 16;
        if (clen % 16 != 0)
            return false;

        Buffer plain(clen);
        mbedtls_aes_context aes;
        mbedtls_aes_init(&aes);
        mbedtls_aes_setkey_dec(&aes, key, 256);
        uint8_t iv_copy[16];
        memcpy(iv_copy, iv, 16);
        if (mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, clen, iv_copy, ciphertext, plain.data()) != 0)
        {
            mbedtls_aes_free(&aes);
            return false;
        }
        mbedtls_aes_free(&aes);

        // remove PKCS7 padding
        if (clen == 0)
            return false;
        uint8_t pad = plain[clen - 1];
        if (pad == 0 || pad > 16)
            return false;
        for (size_t i = 0; i < pad; ++i)
        {
            if (plain[clen - 1 - i] != pad)
                return false;
        }
        size_t plen = clen - pad;
        plaintext_out.assign(plain.begin(), plain.begin() + plen);
        return true;
    }

    // ---------- Path helpers ----------
    Path str2Path(const String &s)
    {
        Path p;
        if (s == "/" || s.length() == 0)
            return p;
        String tmp = s;
        if (tmp.startsWith("/"))
            tmp = tmp.substring(1);
        if (tmp.endsWith("/"))
            tmp = tmp.substring(0, tmp.length() - 1);
        int start = 0;
        while (start < (int)tmp.length())
        {
            int idx = tmp.indexOf('/', start);
            if (idx == -1)
                idx = tmp.length();
            String part = tmp.substring(start, idx);
            if (part.length() > 0)
                p.push_back(part);
            start = idx + 1;
        }
        return p;
    }

    String path2Str(const Path &s)
    {
        if (s.empty())
            return String("/");
        String out = "";
        for (size_t i = 0; i < s.size(); ++i)
        {
            out += "/";
            out += s[i];
        }
        return out;
    }

    // canonical path (used for HMAC) -> use path2Str
    static String canonicalPath(const Path &p)
    {
        // stable canonicalization
        return path2Str(p);
    }

    // path_key = HMAC_SHA256(master_key, canonical_path)
    static void derivePathKey(const Path &p, uint8_t out32[32])
    {
        String c = canonicalPath(p);
        if (s_master_key.size() != 32)
        {
            // default fallback: use sha256 of path
            sha256((const uint8_t *)c.c_str(), c.length(), out32);
            return;
        }
        hmac_sha256(s_master_key.data(), s_master_key.size(), (const uint8_t *)c.c_str(), c.length(), out32);
    }

    // compute hash filename (hex of hmac)
    static String pathHashHex(const Path &p)
    {
        uint8_t h[32];
        derivePathKey(p, h);
        return toHex(h, 32);
    }

    // file helpers
    static String nodeFilenameFor(const Path &p)
    {
        return s_rootFolder + "/" + pathHashHex(p) + ".node";
    }
    static String dataFilenameFor(const Path &p)
    {
        return s_rootFolder + "/" + pathHashHex(p) + ".data";
    }
    static String parityFilenameFor(const Path &p, size_t parityIndex)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), ".parity%u", (unsigned)parityIndex);
        return s_rootFolder + "/" + pathHashHex(p) + String(buf);
    }

    // ---------- Metadata serialization ----------
    /*
       Simple custom binary metadata format (unencrypted blob is small, then encrypted):
       [type:1][size:8 (uint64 LE)][chunkCount:4 (uint32 LE)][nameLen:2][name bytes][childrenLen:4][children bytes]
       type: 0=file, 1=dir
    */

    static void put_uint16_le(Buffer &b, uint16_t v)
    {
        b.push_back((uint8_t)(v & 0xFF));
        b.push_back((uint8_t)((v >> 8) & 0xFF));
    }
    static void put_uint32_le(Buffer &b, uint32_t v)
    {
        b.push_back((uint8_t)(v & 0xFF));
        b.push_back((uint8_t)((v >> 8) & 0xFF));
        b.push_back((uint8_t)((v >> 16) & 0xFF));
        b.push_back((uint8_t)((v >> 24) & 0xFF));
    }
    static void put_uint64_le(Buffer &b, uint64_t v)
    {
        for (int i = 0; i < 8; ++i)
            b.push_back((uint8_t)((v >> (8 * i)) & 0xFF));
    }
    static uint16_t get_uint16_le(const uint8_t *d) { return (uint16_t)d[0] | ((uint16_t)d[1] << 8); }
    static uint32_t get_uint32_le(const uint8_t *d) { return (uint32_t)d[0] | ((uint32_t)d[1] << 8) | ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24); }
    static uint64_t get_uint64_le(const uint8_t *d)
    {
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i)
            v |= ((uint64_t)d[i]) << (8 * i);
        return v;
    }

    // ---------- Node file format ----------
    /*
      Node file = [IV(16)][encrypted_meta(...)] [HMAC(32)]
      HMAC = HMAC_SHA256(path_key, IV || encrypted_meta)
    */

    // write metadata node
    static bool writeNode(const Path &p, bool isDir, uint64_t size, uint32_t chunkCount, const String &decryptedName, const Buffer &childrenPlain)
    {
        uint8_t path_key[32];
        derivePathKey(p, path_key);

        // build plain metadata blob
        Buffer plain;
        plain.push_back((uint8_t)(isDir ? 1 : 0));
        put_uint64_le(plain, size);
        put_uint32_le(plain, chunkCount);
        // name
        uint16_t nameLen = (uint16_t)min((size_t)UINT16_MAX, (size_t)decryptedName.length());
        put_uint16_le(plain, nameLen);
        for (size_t i = 0; i < nameLen; ++i)
            plain.push_back((uint8_t)decryptedName[i]);
        // children
        uint32_t cLen = (uint32_t)childrenPlain.size();
        put_uint32_le(plain, cLen);
        plain.insert(plain.end(), childrenPlain.begin(), childrenPlain.end());

        Buffer encrypted = aes256_cbc_encrypt(path_key, plain.data(), plain.size());
        // compute HMAC over IV||encrypted_meta
        uint8_t h[32];
        hmac_sha256(path_key, 32, encrypted.data(), encrypted.size(), h);

        // write to file
        String fname = nodeFilenameFor(p);
        // ensure file replaced/truncated by removing and recreating
        SD.remove(fname);
        File f = SD.open(fname, FILE_WRITE);
        if (!f)
            return false;
        size_t written = f.write(encrypted.data(), encrypted.size());
        if (written != encrypted.size())
        {
            f.close();
            return false;
        }
        written = f.write(h, 32);
        f.close();
        return written == 32;
    }

    // read node file, returns true if success and fills out args
    static bool readNode(const Path &p, bool &isDir, uint64_t &size, uint32_t &chunkCount, String &decryptedName, Buffer &childrenPlain)
    {
        String fname = nodeFilenameFor(p);
        File f = SD.open(fname, FILE_READ);
        if (!f)
            return false;
        size_t fsz = f.size();
        if (fsz < 32 + 16)
        {
            f.close();
            return false;
        } // too small
        // read entire file
        Buffer all;
        all.resize(fsz);
        f.read(all.data(), fsz);
        f.close();

        // last 32 bytes = hmac
        if (fsz < 32)
            return false;
        const uint8_t *hmac_in = all.data() + (fsz - 32);
        size_t encrypted_len = fsz - 32;

        uint8_t path_key[32];
        derivePathKey(p, path_key);

        // verify hmac
        uint8_t hcalc[32];
        hmac_sha256(path_key, 32, all.data(), encrypted_len, hcalc);
        if (memcmp(hcalc, hmac_in, 32) != 0)
            return false;

        // decrypt encrypted part
        Buffer plain;
        if (!aes256_cbc_decrypt(path_key, all.data(), encrypted_len, plain))
            return false;

        // parse
        const uint8_t *d = plain.data();
        size_t pos = 0;
        if (plain.size() < 1 + 8 + 4 + 2 + 4)
            return false;
        isDir = d[pos++] != 0;
        size = (uint64_t)get_uint64_le(d + pos);
        pos += 8;
        chunkCount = get_uint32_le(d + pos);
        pos += 4;
        uint16_t nameLen = get_uint16_le(d + pos);
        pos += 2;
        if (pos + nameLen > plain.size())
            return false;
        decryptedName = "";
        for (size_t i = 0; i < nameLen; ++i)
            decryptedName += (char)d[pos + i];
        pos += nameLen;
        uint32_t cLen = get_uint32_le(d + pos);
        pos += 4;
        if (pos + cLen > plain.size())
            return false;
        childrenPlain.assign(d + pos, d + pos + cLen);
        return true;
    }

    // ---------- Data chunk format ----------
    /*
       .data file is sequence of chunks:
         [IV(16)][LENGTH(4 LE)][CIPHERTEXT(LENGTH bytes)]
       Each chunk corresponds to original plaintext chunk of <= s_chunkSize.
    */

    // write a specific chunk index (overwrite location) -> we will implement append-only file for simplicity:
    // For simplicity we rewrite the entire .data file on writeFile overwrite.
    static bool writeDataAll(const Path &p, const std::vector<Buffer> &cipherChunks, const std::vector<Buffer> &ivChunks)
    {
        // cipherChunks[i] = ciphertext bytes (no IV)
        // ivChunks[i] = IV bytes (16)
        String fname = dataFilenameFor(p);
        // ensure file replaced/truncated by removing first
        SD.remove(fname);
        File f = SD.open(fname, FILE_WRITE);
        if (!f)
            return false;
        for (size_t i = 0; i < cipherChunks.size(); ++i)
        {
            if (ivChunks[i].size() != 16)
            {
                f.close();
                return false;
            }
            if (f.write(ivChunks[i].data(), 16) != 16)
            {
                f.close();
                return false;
            }
            uint32_t len = (uint32_t)cipherChunks[i].size();
            uint8_t lenb[4] = {(uint8_t)(len & 0xff), (uint8_t)((len >> 8) & 0xff), (uint8_t)((len >> 16) & 0xff), (uint8_t)((len >> 24) & 0xff)};
            if (f.write(lenb, 4) != 4)
            {
                f.close();
                return false;
            }
            if (f.write(cipherChunks[i].data(), cipherChunks[i].size()) != (int)cipherChunks[i].size())
            {
                f.close();
                return false;
            }
        }
        f.close();
        return true;
    }

    // read all chunks (returns vector of pair<iv, ciphertext>)
    static bool readDataAll(const Path &p, std::vector<Buffer> &ivChunks, std::vector<Buffer> &cipherChunks)
    {
        String fname = dataFilenameFor(p);
        File f = SD.open(fname, FILE_READ);
        if (!f)
            return false;
        size_t fsz = f.size();
        size_t pos = 0;
        ivChunks.clear();
        cipherChunks.clear();
        while (pos + 16 + 4 <= fsz)
        {
            uint8_t iv[16];
            if (f.read(iv, 16) != 16)
            {
                f.close();
                return false;
            }
            pos += 16;
            uint8_t lenb[4];
            if (f.read(lenb, 4) != 4)
            {
                f.close();
                return false;
            }
            pos += 4;
            uint32_t len = get_uint32_le(lenb);
            if (pos + len > fsz)
            {
                f.close();
                return false;
            }
            Buffer c(len);
            if (len > 0)
            {
                if (f.read(c.data(), len) != (int)len)
                {
                    f.close();
                    return false;
                }
            }
            pos += len;
            Buffer ivb(iv, iv + 16);
            ivChunks.push_back(ivb);
            cipherChunks.push_back(c);
        }
        f.close();
        return true;
    }

    // ---------- Parity handling (XOR) ----------
    static void buildParityFiles(const Path &p, const std::vector<Buffer> &cipherChunks, const std::vector<Buffer> &ivChunks)
    {
        if (s_parityGroupSize < 2)
            return;
        size_t totalChunks = cipherChunks.size();
        size_t groups = (totalChunks + s_parityGroupSize - 1) / s_parityGroupSize;
        for (size_t g = 0; g < groups; ++g)
        {
            size_t start = g * s_parityGroupSize;
            size_t end = std::min(start + s_parityGroupSize, totalChunks);
            // compute max ciphertext length in group
            size_t maxlen = 0;
            for (size_t i = start; i < end; ++i)
                if (cipherChunks[i].size() > maxlen)
                    maxlen = cipherChunks[i].size();
            // build parity buffers
            Buffer parityiv(16, 0);
            Buffer paritycipher(maxlen, 0);
            for (size_t i = start; i < end; ++i)
            {
                // XOR IVs (pad if necessary)
                for (size_t b = 0; b < 16; ++b)
                    parityiv[b] ^= ivChunks[i][b];
                // XOR ciphertexts (pad with zeros)
                for (size_t b = 0; b < maxlen; ++b)
                {
                    uint8_t v = (b < cipherChunks[i].size()) ? cipherChunks[i][b] : 0;
                    paritycipher[b] ^= v;
                }
            }
            // write parity file
            String fname = parityFilenameFor(p, g);
            // ensure replaced/truncated by removing first
            SD.remove(fname);
            File f = SD.open(fname, FILE_WRITE);
            if (!f)
                continue;
            // parity file: [IV(16)][LENGTH(4)][CIPHERTEXT]
            f.write(parityiv.data(), 16);
            uint32_t plen = (uint32_t)paritycipher.size();
            uint8_t plenb[4] = {(uint8_t)(plen & 0xff), (uint8_t)((plen >> 8) & 0xff), (uint8_t)((plen >> 16) & 0xff), (uint8_t)((plen >> 24) & 0xff)};
            f.write(plenb, 4);
            if (plen > 0)
                f.write(paritycipher.data(), paritycipher.size());
            f.close();
        }
    }

    // ---------- High-level file operations ----------
    bool exists(const Path &p)
    {
        String fname = nodeFilenameFor(p);
        return SD.exists(fname);
    }

    bool mkDir(const Path &p)
    {
        if (exists(p))
            return true;
        // create node file with empty children
        Buffer empty;
        return writeNode(p, true, 0, 0, p.empty() ? String("/") : p.back(), empty);
    }

    bool rmDir(const Path &p)
    {
        if (!exists(p))
            return false;
        // remove node, data, parity files
        String n = nodeFilenameFor(p);
        SD.remove(n);
        SD.remove(dataFilenameFor(p));
        // remove parity files possible
        for (size_t i = 0; i < 64; ++i)
        {
            String pn = parityFilenameFor(p, i);
            if (!SD.exists(pn))
                break;
            SD.remove(pn);
        }
        return true;
    }

    long getFileSize(const Path &p)
    {
        Metadata md = getMetadata(p);
        return md.size;
    }

    Metadata getMetadata(const Path &p)
    {
        Metadata m;
        m.size = -1;
        m.encryptedName = "";
        m.decryptedName = "";
        m.isDirectory = false;
        if (!exists(p))
            return m;
        bool isDir = false;
        uint64_t size = 0;
        uint32_t chunkCount = 0;
        String name;
        Buffer children;
        if (!readNode(p, isDir, size, chunkCount, name, children))
            return m;
        m.size = (long)size;
        m.decryptedName = name;
        m.encryptedName = pathHashHex(p);
        m.isDirectory = isDir;
        return m;
    }

    std::vector<String> readDir(const Path &plainDir)
    {
        std::vector<String> out;
        if (!exists(plainDir))
            return out;
        bool isDir = false;
        uint64_t size = 0;
        uint32_t chunkCount = 0;
        String name;
        Buffer children;
        if (!readNode(plainDir, isDir, size, chunkCount, name, children))
            return out;
        // children blob is a simple newline-separated list for this implementation:
        String s;
        s.reserve(children.size() + 1);
        for (auto c : children)
            s += (char)c;
        int start = 0;
        while (start < (int)s.length())
        {
            int idx = s.indexOf('\n', start);
            if (idx == -1)
                idx = s.length();
            String line = s.substring(start, idx);
            if (line.length() > 0)
                out.push_back(line);
            start = idx + 1;
        }
        return out;
    }

    void lsDirSerial(const Path &plainDir)
    {
        auto v = readDir(plainDir);
        for (auto &s : v)
            Serial.println(s);
    }

    // Storage helpers (namespace function requested)
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
            return ENC_FS::readFile(p, start, end);
        }
        bool del(const String &appId, const String &key)
        {
            Path p = storagePath(appId, key);
            return ENC_FS::deleteFile(p);
        }
        bool set(const String &appId, const String &key, const Buffer &data)
        {
            Path p = storagePath(appId, key);
            // write whole file
            return ENC_FS::writeFile(p, 0, -1, data);
        }
    }

    // copy from SPIFFS to ENC_FS path
    void copyFileFromSPIFFS(const char *spiffsPath, const Path &sdPath)
    {
        // assume SPIFFS has been initialized elsewhere (SPIFFS.begin())
        File sf = SPIFFS.open(spiffsPath, FILE_READ);
        if (!sf)
            return;
        size_t sz = sf.size();
        Buffer buf;
        buf.resize(sz);
        sf.read(buf.data(), sz);
        sf.close();
        writeFile(sdPath, 0, -1, buf);
    }

    // readFile implementation (reads specified byte range)
    Buffer readFile(const Path &p, long start, long end)
    {
        Buffer out;
        if (!exists(p))
            return out;
        Metadata md = getMetadata(p);
        if (md.size < 0)
            return out;
        uint64_t fsize = (uint64_t)md.size;
        if (end == -1 || end >= (long)fsize)
            end = (long)fsize - 1;
        if (start < 0)
            start = 0;
        if (start > end)
            return out;
        // compute needed chunks
        size_t chunkSize = s_chunkSize;
        size_t firstChunk = start / chunkSize;
        size_t lastChunk = end / chunkSize;
        std::vector<Buffer> ivChunks, cipherChunks;
        if (!readDataAll(p, ivChunks, cipherChunks))
            return out;
        // sanity: ensure chunks cover
        size_t available = cipherChunks.size();
        if (available == 0)
            return out;
        uint8_t path_key[32];
        derivePathKey(p, path_key);
        for (size_t ci = firstChunk; ci <= lastChunk && ci < available; ++ci)
        {
            // reconstruct chunk input = IV||ciphertext -> decrypt to plaintext
            // need to assemble: IV || ciphertext
            Buffer in;
            in.reserve(16 + cipherChunks[ci].size());
            in.insert(in.end(), ivChunks[ci].begin(), ivChunks[ci].end());
            in.insert(in.end(), cipherChunks[ci].begin(), cipherChunks[ci].end());
            Buffer plain;
            if (!aes256_cbc_decrypt(path_key, in.data(), in.size(), plain))
            {
                // Try parity recovery (simple)
                // locate parity group
                size_t group = ci / s_parityGroupSize;
                String pfn = parityFilenameFor(p, group);
                if (!SD.exists(pfn))
                    continue;
                // try to reconstruct using parity
                File pf = SD.open(pfn, FILE_READ);
                if (!pf)
                    continue;
                uint8_t piv[16];
                pf.read(piv, 16);
                uint8_t plenb[4];
                pf.read(plenb, 4);
                uint32_t plen = get_uint32_le(plenb);
                Buffer pbuf(plen);
                if (plen > 0)
                    pf.read(pbuf.data(), plen);
                pf.close();
                // reconstruct ciphertext and IV by XOR of parity with others
                Buffer recon_iv(16, 0), recon_cipher(plen, 0);
                // parity ^ XOR(other chunks) = missing chunk
                for (size_t b = 0; b < 16; ++b)
                    recon_iv[b] = piv[b];
                for (size_t b = 0; b < plen; ++b)
                    recon_cipher[b] = (b < pbuf.size()) ? pbuf[b] : 0;
                size_t startIdx = group * s_parityGroupSize;
                for (size_t j = startIdx; j < startIdx + s_parityGroupSize; ++j)
                {
                    if (j == ci)
                        continue;
                    if (j < ivChunks.size())
                    {
                        for (size_t b = 0; b < 16; ++b)
                            recon_iv[b] ^= ivChunks[j][b];
                        for (size_t b = 0; b < plen; ++b)
                        {
                            uint8_t v = (b < cipherChunks[j].size()) ? cipherChunks[j][b] : 0;
                            recon_cipher[b] ^= v;
                        }
                    }
                }
                // attempt decrypt reconstructed
                Buffer inr;
                inr.reserve(16 + recon_cipher.size());
                inr.insert(inr.end(), recon_iv.begin(), recon_iv.end());
                inr.insert(inr.end(), recon_cipher.begin(), recon_cipher.end());
                if (!aes256_cbc_decrypt(path_key, inr.data(), inr.size(), plain))
                    continue; // give up this chunk
            }

            // determine byte range to take from this chunk
            long chunkStart = (long)ci * (long)chunkSize;
            long takeFrom = max<long>(start, chunkStart);
            long takeTo = min<long>(end, chunkStart + (long)plain.size() - 1);
            if (takeFrom > takeTo)
                continue;
            size_t ofs = (size_t)(takeFrom - chunkStart);
            size_t len = (size_t)(takeTo - takeFrom + 1);
            size_t old = out.size();
            out.resize(old + len);
            memcpy(out.data() + old, plain.data() + ofs, len);
        }
        return out;
    }

    Buffer readFilePart(const Path &p, long start, long end)
    {
        return readFile(p, start, end);
    }

    String readFileString(const Path &p)
    {
        Buffer b = readFile(p, 0, -1);
        String s;
        s.reserve(b.size() + 1);
        for (auto c : b)
            s += (char)c;
        return s;
    }

    // writeFile: write data starting at byte offset 'start'. If end == -1 -> write entire provided data as replacement starting at start.
    // For simplicity: if start==0 and end==-1 -> overwrite file with data
    bool writeFile(const Path &p, long start, long end, const Buffer &data)
    {
        // ensure parent dir exists (no enforced hierarchy in this simplified version)
        // read existing content if partial write
        Buffer existing;
        Metadata md = getMetadata(p);
        if (md.size >= 0)
        {
            existing = readFile(p, 0, -1);
        }
        if (start < 0)
            start = 0;
        if (end != -1 && end < (long)start)
            end = -1;
        Buffer newdata;
        if (start == 0 && end == -1)
        {
            newdata = data; // replace
        }
        else
        {
            // merge into existing
            size_t needed = std::max(existing.size(), (size_t)start + data.size());
            newdata.resize(needed, 0);
            // copy existing into newdata
            if (!existing.empty())
                memcpy(newdata.data(), existing.data(), existing.size());
            // copy provided data
            memcpy(newdata.data() + start, data.data(), data.size());
        }

        // split into chunks, encrypt each chunk deterministically
        uint8_t path_key[32];
        derivePathKey(p, path_key);
        std::vector<Buffer> cipherChunks;
        std::vector<Buffer> ivChunks;

        size_t total = newdata.size();
        size_t nchunks = (total + s_chunkSize - 1) / s_chunkSize;
        for (size_t i = 0; i < nchunks; ++i)
        {
            size_t ofs = i * s_chunkSize;
            size_t len = std::min(s_chunkSize, total - ofs);
            Buffer plain(len);
            memcpy(plain.data(), newdata.data() + ofs, len);
            // encrypt using aes256_cbc_encrypt which prepends IV
            Buffer enc = aes256_cbc_encrypt(path_key, plain.data(), plain.size());
            // split into iv + ciphertext
            if (enc.size() < 16)
                return false;
            Buffer iv(enc.begin(), enc.begin() + 16);
            Buffer cipher(enc.begin() + 16, enc.end());
            ivChunks.push_back(iv);
            cipherChunks.push_back(cipher);
        }

        // write data file
        if (!writeDataAll(p, cipherChunks, ivChunks))
            return false;

        // build parity files
        buildParityFiles(p, cipherChunks, ivChunks);

        // build children list (for directory parent handling we'd need to update parent; for simplicity we don't maintain parent lists)
        Buffer childrenPlain; // empty

        // write node metadata
        bool isDir = false;
        uint64_t size = newdata.size();
        uint32_t chunkCount = (uint32_t)nchunks;
        String name = p.empty() ? String("/") : p.back();
        return writeNode(p, isDir, size, chunkCount, name, childrenPlain);
    }

    bool appendFile(const Path &p, const Buffer &data)
    {
        Metadata md = getMetadata(p);
        long start = 0;
        if (md.size > 0)
            start = md.size;
        return writeFile(p, start, -1, data);
    }

    bool writeFileString(const Path &p, const String &s)
    {
        Buffer b(s.length());
        memcpy(b.data(), s.c_str(), s.length());
        return writeFile(p, 0, -1, b);
    }

    bool deleteFile(const Path &p)
    {
        if (!exists(p))
            return false;
        String n = nodeFilenameFor(p);
        SD.remove(n);
        SD.remove(dataFilenameFor(p));
        // remove parity files possible
        for (size_t i = 0; i < 64; ++i)
        {
            String pn = parityFilenameFor(p, i);
            if (!SD.exists(pn))
                break;
            SD.remove(pn);
        }
        return true;
    }

    // init: setup rootFolder and derive master_key from password
    void init(String rootFolder, String password)
    {
        if (rootFolder.length() > 0)
            s_rootFolder = rootFolder;
        // ensure root folder exists on SD
        if (!SD.exists(s_rootFolder))
        {
            SD.mkdir(s_rootFolder);
        }
        // derive master key by iterated SHA256 (simple KDF)
        // Warning: the number of iterations should be tuned to require ~1s on the target device.
        uint8_t buf[32];
        // initial seed = sha256(password)
        sha256((const uint8_t *)password.c_str(), password.length(), buf);
        for (uint32_t i = 0; i < s_kdfIterations; ++i)
        {
            sha256(buf, 32, buf);
        }
        s_master_key.assign(buf, buf + 32);
    }

    // tuning setters
    void setChunkSize(size_t bytes) { s_chunkSize = bytes; }
    void setParityGroupSize(size_t g)
    {
        if (g >= 2)
            s_parityGroupSize = g;
    }
    void setKdfIterations(uint32_t it) { s_kdfIterations = it; }

} // namespace ENC_FS
