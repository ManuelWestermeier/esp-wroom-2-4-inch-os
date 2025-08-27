#pragma once

#include <Arduino.h>
#include <vector>

#include "../auth/auth.hpp"

namespace ENC_FS
{
    typedef std::vector<String> Path;
    typedef std::vector<uint8_t> Buffer;

    Path str2Path(String)
    {
    }
    Path pathEnc(Path)
    {
    }
    Path pathDec(Path)
    {
    }

    bool exists(Path) {}

    bool mkDir(Path) {}
    bool rmDir(Path) {}
    bool lsDir(Path) {}

    Buffer readFile(Path, long start, long end)
    {
    }
    String readFileString(Path)
    {
    }
    bool writeFile(Path, long start, long end, Buffer data)
    {
    }
    bool appendFile(Path, Buffer data)
    {
    }
    bool writeFileString(Path, String data)
    {
    }
    bool deleteFile(Path)
    {
    }

    // add some metadata getters, ...

    namespace Storage
    {
        Buffer get(String appId, long start, long end)
        {
        }
        bool set(String appId, long start, long end)
        {
        }
    }
}