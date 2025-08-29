#pragma once

#include <Arduino.h>
#include <SD.h>
#include <vector>
#include <mbedtls/sha256.h>
#include <mbedtls/aes.h>
#include <esp_system.h>

#include "../auth/auth.hpp"

namespace ENC_FS
{
    typedef std::vector<String> Path;
    typedef std::vector<uint8_t> Buffer;

    // ---- interne State ----
    static uint8_t master_key[32];
    static String g_username = "";
    static String g_password = "";
    static bool initialized = false;

    // ---- Hilfsfunktionen ----
    static void deriveMasterKey(const String &username, const String &password)
    {
        String in = username + ":" + password;
        mbedtls_sha256_context ctx;
        mbedtls_sha256_init(&ctx);
        mbedtls_sha256_starts_ret(&ctx, 0);
        mbedtls_sha256_update_ret(&ctx, (const unsigned char *)in.c_str(), in.length());
        mbedtls_sha256_finish_ret(&ctx, master_key);
        mbedtls_sha256_free(&ctx);
        initialized = true;
        g_username = username;
        g_password = password;
    }

    static String sha256_hex(const String &s)
    {
        mbedtls_sha256_context ctx;
        uint8_t out[32];
        mbedtls_sha256_init(&ctx);
        mbedtls_sha256_starts_ret(&ctx, 0);
        mbedtls_sha256_update_ret(&ctx, (const unsigned char *)s.c_str(), s.length());
        mbedtls_sha256_finish_ret(&ctx, out);
        mbedtls_sha256_free(&ctx);

        static const char hexd[] = "0123456789abcdef";
        String hex;
        hex.reserve(64);
        for (size_t i = 0; i < 32; ++i)
        {
            hex += hexd[(out[i] >> 4) & 0xF];
            hex += hexd[out[i] & 0xF];
        }
        return hex;
    }

    static String joinPath(const Path &p)
    {
        if (p.empty())
            return "/";
        String s = "";
        for (size_t i = 0; i < p.size(); ++i)
        {
            s += "/";
            s += p[i];
        }
        return s;
    }

    static Buffer stringToBuffer(const String &s)
    {
        Buffer b;
        size_t n = s.length();
        b.reserve(n);
        for (size_t i = 0; i < n; ++i)
            b.push_back((uint8_t)s[i]);
        return b;
    }

    static String bufferToString(const Buffer &b)
    {
        if (b.empty())
            return String("");
        String s;
        s.reserve(b.size());
        for (auto c : b)
            s += (char)c;
        return s;
    }

    // AES-CTR auf ganzen Puffer (encrypt/decrypt gleicher Aufruf)
    static Buffer aes_ctr_crypt(const Buffer &in, const uint8_t iv[16])
    {
        Buffer out;
        out.resize(in.size());

        mbedtls_aes_context aes;
        mbedtls_aes_init(&aes);
        mbedtls_aes_setkey_enc(&aes, master_key, 256);

        size_t nc_off = 0;
        uint8_t nonce_counter[16];
        uint8_t stream_block[16];
        memcpy(nonce_counter, iv, 16);
        memset(stream_block, 0, 16);

        mbedtls_aes_crypt_ctr(&aes, in.size(), &nc_off, nonce_counter, stream_block, in.data(), out.data());
        mbedtls_aes_free(&aes);
        return out;
    }

    // ---- API ----

    // Muss einmal aufgerufen werden (z.B. beim Start), SD.begin() sollte bereits gelaufen sein
    void init(String username, String password)
    {
        deriveMasterKey(username, password);
    }

    // convert string path like "/a/b/c.txt" -> Path vector ["a","b","c.txt"]
    Path str2Path(String s)
    {
        Path p;
        if (s.length() == 0)
            return p;
        while (s.length() && s.startsWith("/"))
            s = s.substring(1);
        while (s.length() && s.endsWith("/"))
            s = s.substring(0, s.length() - 1);
        int idx = 0;
        while (idx < s.length())
        {
            int j = s.indexOf('/', idx);
            if (j == -1)
            {
                p.push_back(s.substring(idx));
                break;
            }
            p.push_back(s.substring(idx, j));
            idx = j + 1;
        }
        return p;
    }

    // pathEnc: mappt einen Benutzer-Pfad auf die SD-Struktur:
    // /programms/test.txt -> /<username>/<hash("programms"+username+password)>/<hash("test.txt"+username+password)>
    Path pathEnc(Path plain)
    {
        Path out;
        if (!initialized)
            return out;
        // erstes Segment: username (ungehasht)
        out.push_back(g_username);
        for (size_t i = 0; i < plain.size(); ++i)
        {
            String seg = plain[i];
            String key = seg + g_username + g_password;
            String h = sha256_hex(key);
            out.push_back(h);
        }
        return out;
    }

    // pathDec: Hashes sind nicht reversibel; wir geben username zurück und für die restlichen Segmente
    // die Hash-Strings (keine Rekonstruktion möglich).
    Path pathDec(Path enc)
    {
        Path out;
        if (!initialized)
            return out;
        if (enc.empty())
            return out;
        out.push_back(enc[0]); // username
        for (size_t i = 1; i < enc.size(); ++i)
            out.push_back(enc[i]); // hashes
        return out;
    }

    bool exists(Path p)
    {
        Path ep = pathEnc(p);
        String full = joinPath(ep);
        return SD.exists(full.c_str());
    }

    bool mkDir(Path p)
    {
        Path ep = pathEnc(p);
        String full = joinPath(ep);
        return SD.mkdir(full.c_str());
    }

    // recursive remove directory/file helper (best-effort, abhängig von SD lib capabilities)
    static bool removeRecursive(const String &fullPath)
    {
        if (!SD.exists(fullPath.c_str()))
            return false;
        File f = SD.open(fullPath.c_str());
        if (!f)
            return false;
        if (!f.isDirectory())
        {
            f.close();
            return SD.remove(fullPath.c_str());
        }
        // directory: iterate entries
        File entry = f.openNextFile();
        while (entry)
        {
            String name = String(entry.name());
            if (entry.isDirectory())
            {
                removeRecursive(name);
            }
            else
            {
                SD.remove(name.c_str());
            }
            entry.close();
            entry = f.openNextFile();
        }
        f.close();
        return true;
    }

    bool rmDir(Path p)
    {
        Path ep = pathEnc(p);
        String full = joinPath(ep);
        return removeRecursive(full);
    }

    bool lsDir(Path p)
    {
        Path ep = pathEnc(p);
        String full = joinPath(ep);
        File dir = SD.open(full.c_str());
        if (!dir)
            return false;
        if (!dir.isDirectory())
        {
            dir.close();
            return false;
        }
        File f = dir.openNextFile();
        while (f)
        {
            String ent = String(f.name());
            int lastSlash = ent.lastIndexOf('/');
            String nameOnly = (lastSlash >= 0) ? ent.substring(lastSlash + 1) : ent;
            Serial.print(nameOnly);
            if (f.isDirectory())
                Serial.println("/");
            else
                Serial.println("");
            f.close();
            f = dir.openNextFile();
        }
        dir.close();
        return true;
    }

    // read entire file into buffer and decrypt. start/end sind Plaintext-Offsets; end==-1 -> to end
    Buffer readFile(Path p, long start, long end)
    {
        Buffer empty;
        if (!initialized)
            return empty;
        Path ep = pathEnc(p);
        String full = joinPath(ep);
        File f = SD.open(full.c_str(), FILE_READ);
        if (!f)
            return empty;
        size_t fileSize = f.size();
        if (fileSize < 16)
        {
            f.close();
            return empty;
        } // No IV + data
        uint8_t iv[16];
        if (f.read(iv, 16) != 16)
        {
            f.close();
            return empty;
        }
        size_t csize = fileSize - 16;
        Buffer cbuf;
        cbuf.resize(csize);
        if (f.read(cbuf.data(), csize) != (int)csize)
        {
            f.close();
            return empty;
        }
        f.close();

        Buffer ptxt = aes_ctr_crypt(cbuf, iv);
        long plen = (long)ptxt.size();
        if (end < 0 || end > plen)
            end = plen;
        if (start < 0)
            start = 0;
        if (start > plen)
            start = plen;
        if (end < start)
            end = start;
        Buffer out;
        out.reserve(end - start);
        out.insert(out.end(), ptxt.begin() + start, ptxt.begin() + end);
        return out;
    }

    String readFileString(Path p)
    {
        Buffer b = readFile(p, 0, -1);
        return bufferToString(b);
    }

    bool writeFile(Path p, long start, long end, Buffer data)
    {
        if (!initialized)
            return false;
        Path ep = pathEnc(p);
        String full = joinPath(ep);

        // Falls Verzeichnisse fehlen, versuchen wir sie iterativ zu erstellen
        String accum = "";
        for (size_t i = 0; i + 1 < ep.size(); ++i)
        {
            accum += "/";
            accum += ep[i];
            if (!SD.exists(accum.c_str()))
            {
                SD.mkdir(accum.c_str());
            }
        }

        // Erzeuge plaintext abhängig von start/end
        Buffer plaintext;
        if (start == 0 && end == 0)
        {
            plaintext = data; // overwrite whole file
        }
        else
        {
            Buffer existing = readFile(p, 0, -1);
            long existingLen = existing.size();
            if (start < 0)
                start = 0;
            if (end < 0)
                end = start + data.size();
            long writeLen = end - start;
            long needLen = max((long)existingLen, start + writeLen);
            plaintext = existing;
            plaintext.resize(needLen, 0);
            for (long i = 0; i < writeLen; ++i)
                plaintext[start + i] = data[i];
        }

        // generate random IV
        uint8_t iv[16];
        esp_fill_random(iv, sizeof(iv));

        Buffer cipher = aes_ctr_crypt(plaintext, iv);

        // write IV + cipher (overwrite)
        if (SD.exists(full.c_str()))
            SD.remove(full.c_str());
        File fw = SD.open(full.c_str(), FILE_WRITE);
        if (!fw)
            return false;
        fw.write(iv, 16);
        if (!cipher.empty())
            fw.write(cipher.data(), cipher.size());
        fw.close();
        return true;
    }

    bool appendFile(Path p, Buffer data)
    {
        Buffer existing = readFile(p, 0, -1);
        Buffer combined = existing;
        combined.insert(combined.end(), data.begin(), data.end());
        return writeFile(p, 0, 0, combined);
    }

    bool writeFileString(Path p, String data)
    {
        Buffer b = stringToBuffer(data);
        return writeFile(p, 0, 0, b);
    }

    bool deleteFile(Path p)
    {
        Path ep = pathEnc(p);
        String full = joinPath(ep);
        if (!SD.exists(full.c_str()))
            return false;
        return SD.remove(full.c_str());
    }

    // ---- Storage namespace (angepasst an gewünschte Struktur) ----
    namespace Storage
    {
        // Erzeugt Pfad wie:
        // /<username>/hash("programms"+username+password)/hash("<app-id>"+username+password)/hash("user-data-storage"+username+password)/<hash(key+username+password)>.data
        static Path storagePath(const String &appId, const String &key)
        {
            Path out;
            out.push_back(g_username);

            // hash(programms + username + password)
            String h_programms = sha256_hex(String("programms") + g_username + g_password);
            out.push_back(h_programms);

            // hash(app-id + username + password)
            String h_app = sha256_hex(appId + g_username + g_password);
            out.push_back(h_app);

            // hash(user-data-storage + username + password)
            String h_storage = sha256_hex(String("user-data-storage") + g_username + g_password);
            out.push_back(h_storage);

            // final filename: hash(key + username + password) + ".data"
            String keyHash = sha256_hex(key + g_username + g_password) + ".data";
            out.push_back(keyHash);
            return out;
        }

        // get/set Signaturen
        Buffer get(String appId, long start, long end)
        {
            // fallback key "default"
            Path p = storagePath(appId, String("default"));
            return ENC_FS::readFile(p, start, end);
        }

        Buffer get(String appId, const String &key, long start, long end)
        {
            Path p = storagePath(appId, key);
            return ENC_FS::readFile(p, start, end);
        }

        bool set(String appId, long start, long end, const Buffer &data)
        {
            Path p = storagePath(appId, String("default"));

            // ensure hashed directory chain exists
            Path ep;
            ep.push_back(g_username);
            ep.push_back(sha256_hex(String("programms") + g_username + g_password));
            ep.push_back(sha256_hex(appId + g_username + g_password));
            ep.push_back(sha256_hex(String("user-data-storage") + g_username + g_password));

            String accum = "";
            for (size_t i = 0; i + 1 < ep.size(); ++i)
            {
                accum += "/";
                accum += ep[i];
                if (!SD.exists(accum.c_str()))
                    SD.mkdir(accum.c_str());
            }

            return ENC_FS::writeFile(p, start, end, data);
        }

        bool set(const String &appId, const String &key, long start, long end, const Buffer &data)
        {
            Path p = storagePath(appId, key);

            // ensure hashed directory chain exists
            Path ep;
            ep.push_back(g_username);
            ep.push_back(sha256_hex(String("programms") + g_username + g_password));
            ep.push_back(sha256_hex(appId + g_username + g_password));
            ep.push_back(sha256_hex(String("user-data-storage") + g_username + g_password));

            String accum = "";
            for (size_t i = 0; i + 1 < ep.size(); ++i)
            {
                accum += "/";
                accum += ep[i];
                if (!SD.exists(accum.c_str()))
                    SD.mkdir(accum.c_str());
            }

            return ENC_FS::writeFile(p, start, end, data);
        }

        // convenience overload: set entire buffer
        bool set(const String &appId, const String &key, const Buffer &data)
        {
            return set(appId, key, 0, 0, data);
        }
    } // namespace Storage

} // namespace ENC_FS
