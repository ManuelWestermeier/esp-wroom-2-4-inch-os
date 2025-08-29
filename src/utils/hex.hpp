#pragma once
#include <Arduino.h>

// Wandelt Binärdaten -> Hexstring
inline String toHex(const String &inp)
{
    const char hexChars[] = "0123456789ABCDEF";
    String out;
    out.reserve(inp.length() * 2); // Performance
    for (int i = 0; i < inp.length(); i++)
    {
        uint8_t b = (uint8_t)inp[i];
        out += hexChars[(b >> 4) & 0x0F];
        out += hexChars[b & 0x0F];
    }
    return out;
}

// Wandelt Hexstring -> Binärdaten
inline String fromHex(const String &inp)
{
    String out;
    if (inp.length() % 2 != 0)
        return out; // ungültig
    out.reserve(inp.length() / 2);

    auto hexVal = [](char c) -> int
    {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        return -1; // ungültig
    };

    for (int i = 0; i < inp.length(); i += 2)
    {
        int hi = hexVal(inp[i]);
        int lo = hexVal(inp[i + 1]);
        if (hi < 0 || lo < 0)
            return String(); // Fehler
        out += char((hi << 4) | lo);
    }
    return out;
}
