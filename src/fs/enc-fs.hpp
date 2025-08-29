#pragma once

#include <Arduino.h>
#include <SD.h>
#include <vector>
#include <functional>
#include "../auth/auth.hpp"

namespace ENC_FS
{
    typedef std::vector<String> Path;
    typedef std::vector<uint8_t> Buffer;

    // ---------- intern ----------

    // Simple hash helper (kannst später SHA256/MD5 ersetzen)
    String hashString(const String &input)
    {
        uint32_t h = 5381;
        for (size_t i = 0; i < input.length(); i++)
        {
            h = ((h << 5) + h) + input[i]; // djb2 hash
        }
        return String(h, HEX);
    }

    // Convert plain path string into path vector
    Path str2Path(const String &str)
    {
        Path p;
        int start = 0;
        int idx = str.indexOf('/', start);
        while (idx >= 0)
        {
            if (idx > start)
                p.push_back(str.substring(start, idx));
            start = idx + 1;
            idx = str.indexOf('/', start);
        }
        if (start < str.length())
            p.push_back(str.substring(start));
        return p;
    }

    // ---------- Path Encryption ----------

    Path pathEnc(const Path &plain)
    {
        Path enc;
        if (plain.empty())
            return enc;

        String user = Auth::username;
        String pass = Auth::password;

        // Root: always user dir
        enc.push_back(user);

        // Example: /programms/test.txt
        // -> /<username>/<hash("programms"+user+pass)>/<hash("test.txt"+user+pass)>
        for (auto &part : plain)
        {
            String h = hashString(part + user + pass);
            enc.push_back(h);
        }

        return enc;
    }

    Path pathDec(const Path &enc)
    {
        // ⚠️ Reverse decoding ist ohne Mapping nicht möglich,
        // weil Hashing One-Way ist.
        // Lösung: Metadaten speichern (Mapping Hash -> Originalname)
        // → z.B. `.map` Datei pro Ordner
        Path plain;
        plain.push_back("DECODING_NOT_POSSIBLE");
        return plain;
    }

    // ---------- File System Utils ----------

    String joinPath(const Path &p)
    {
        String r = "/";
        for (size_t i = 0; i < p.size(); i++)
        {
            r += p[i];
            if (i < p.size() - 1)
                r += "/";
        }
        return r;
    }

    bool exists(const Path &p)
    {
        return SD.exists(joinPath(pathEnc(p)));
    }

    bool mkDir(const Path &p)
    {
        return SD.mkdir(joinPath(pathEnc(p)));
    }

    bool rmDir(const Path &p)
    {
        return SD.rmdir(joinPath(pathEnc(p)));
    }

    bool lsDir(const Path &p)
    {
        File dir = SD.open(joinPath(pathEnc(p)));
        if (!dir || !dir.isDirectory())
            return false;

        File file = dir.openNextFile();
        while (file)
        {
            Serial.println(file.name());
            file = dir.openNextFile();
        }
        return true;
    }

    Buffer readFile(const Path &p, long start, long end)
    {
        Buffer buf;
        File f = SD.open(joinPath(pathEnc(p)), FILE_READ);
        if (!f)
            return buf;
        if (end <= 0 || end > f.size())
            end = f.size();
        f.seek(start);
        while (f.position() < end && f.available())
        {
            buf.push_back(f.read());
        }
        f.close();
        return buf;
    }

    String readFileString(const Path &p)
    {
        Buffer buf = readFile(p, 0, -1);
        return String((char *)buf.data(), buf.size());
    }

    bool writeFile(const Path &p, long start, long end, Buffer data)
    {
        String path = joinPath(pathEnc(p));
        File f = SD.open(path, FILE_WRITE);
        if (!f)
            return false;
        f.seek(start);
        f.write(data.data(), data.size());
        f.close();
        return true;
    }

    bool appendFile(const Path &p, Buffer data)
    {
        String path = joinPath(pathEnc(p));
        File f = SD.open(path, FILE_APPEND);
        if (!f)
            return false;
        f.write(data.data(), data.size());
        f.close();
        return true;
    }

    bool writeFileString(const Path &p, String data)
    {
        Buffer b;
        for (size_t i = 0; i < data.length(); i++)
            b.push_back(data[i]);
        return writeFile(p, 0, data.length(), b);
    }

    bool deleteFile(const Path &p)
    {
        return SD.remove(joinPath(pathEnc(p)));
    }

    // ---------- Storage API ----------

    namespace Storage
    {
        Path makeStoragePath(const String &appId, const String &key)
        {
            String u = Auth::username;
            String pw = Auth::password;

            Path p;
            p.push_back(u);
            p.push_back(hashString("programms" + u + pw));
            p.push_back(hashString(appId + u + pw));
            p.push_back(hashString("user-data-storage" + u + pw));
            p.push_back(hashString(key + u + pw) + ".data");
            return p;
        }

        Buffer get(const String &appId, const String &key)
        {
            return readFile(makeStoragePath(appId, key), 0, -1);
        }

        bool set(const String &appId, const String &key, const Buffer &data)
        {
            return writeFile(makeStoragePath(appId, key), 0, data.size(), data);
        }
    }
}
