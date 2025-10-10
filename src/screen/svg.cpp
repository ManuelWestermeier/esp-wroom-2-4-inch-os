#define NANOSVG_IMPLEMENTATION
extern "C"
{
#include "nanosvg.h"
}

#include "svg.hpp"
#include <Arduino.h>
#include <cstdint>
#include <algorithm>
#include <cstring>

// Cache size
static constexpr int SVG_CACHE_SIZE = 7;

struct SVGImageChacheItem
{
    NSVGimage *image = nullptr;
    unsigned long lastUsed = 0;   // millis() when last used
    unsigned long complexity = 0; // ms it took to parse
    uint16_t id = 0;              // small id derived from fingerprint
    uint32_t fp = 0;              // 32-bit fingerprint (FNV-1a)
    bool occupied = false;
};

// single cache instance
static SVGImageChacheItem svgChache[SVG_CACHE_SIZE] = {};

// FNV-1a 32-bit
static uint32_t fnv1a32(const char *data, size_t len)
{
    const uint32_t FNV_PRIME = 0x01000193u;
    uint32_t hash = 0x811C9DC5u;
    for (size_t i = 0; i < len; ++i)
    {
        hash ^= (uint8_t)data[i];
        hash *= FNV_PRIME;
    }
    return hash;
}

static uint16_t getSVGChacheID(const String &svgString)
{
    // 16-bit id as xor of fingerprint halves and length
    const char *s = svgString.c_str();
    size_t len = svgString.length();
    uint32_t fp = fnv1a32(s, len);
    return (uint16_t)((fp & 0xFFFFu) ^ ((fp >> 16) & 0xFFFFu) ^ (uint16_t)len);
}

static uint32_t getSVGFingerprint(const String &svgString)
{
    return fnv1a32(svgString.c_str(), svgString.length());
}

static size_t cacheSize()
{
    return SVG_CACHE_SIZE;
}

void updateSVGList()
{
    unsigned long now = millis();
    for (size_t i = 0; i < cacheSize(); ++i)
    {
        if (svgChache[i].occupied && svgChache[i].image)
        {
            // subtract safely to handle millis overflow
            unsigned long age = now - svgChache[i].lastUsed;
            if (age > 1000UL) // older than 1 second
            {
                nsvgDelete(svgChache[i].image);
                svgChache[i].image = nullptr;
                svgChache[i].occupied = false;
                svgChache[i].fp = 0;
                svgChache[i].id = 0;
                svgChache[i].complexity = 0;
                svgChache[i].lastUsed = 0;
            }
        }
    }
}

void clearList()
{
    for (size_t i = 0; i < cacheSize(); ++i)
    {
        if (svgChache[i].occupied)
        {
            if (svgChache[i].image)
            {
                nsvgDelete(svgChache[i].image);
            }
            svgChache[i].image = nullptr;
            svgChache[i].occupied = false;
            svgChache[i].fp = 0;
            svgChache[i].id = 0;
            svgChache[i].complexity = 0;
            svgChache[i].lastUsed = 0;
        }
    }
}

// Return true if cache is empty
static bool cacheIsEmpty()
{
    for (size_t i = 0; i < cacheSize(); ++i)
        if (svgChache[i].occupied)
            return false;
    return true;
}

static NSVGimage *tryParseSVG(const String &svgString)
{
    // Duplicate string to writable buffer because some builds expect char*
    size_t len = svgString.length();
    char *buf = (char *)malloc(len + 1);
    if (!buf)
        return nullptr;
    memcpy(buf, svgString.c_str(), len + 1);

    NSVGimage *image = nsvgParse(buf, "px", 96.0f);
    free(buf);
    return image;
}

NSVGimage *createSVG(const String &svgString)
{
    if (svgString.length() == 0)
        return nullptr;

    const uint32_t fp = getSVGFingerprint(svgString);
    const uint16_t id = getSVGChacheID(svgString);
    unsigned long now = millis();

    // 1) try to find in cache by fingerprint + id
    for (size_t i = 0; i < cacheSize(); ++i)
    {
        if (svgChache[i].occupied && svgChache[i].fp == fp && svgChache[i].id == id && svgChache[i].image)
        {
            // cache hit
            svgChache[i].lastUsed = now;
            return svgChache[i].image;
        }
    }

    // Not found: attempt to parse
    unsigned long t0 = millis();
    NSVGimage *image = tryParseSVG(svgString);
    unsigned long t1 = millis();
    unsigned long complexity = (image) ? (t1 - t0) : 0;

    if (!image)
    {
        // Parsing failed: free existing cache and attempt parse again (user requested)
        clearList();

        // Try parsing again after clearing cache (memory might now be available)
        t0 = millis();
        image = tryParseSVG(svgString);
        t1 = millis();
        complexity = (image) ? (t1 - t0) : 0;

        if (!image)
        {
            // Still failed: return nullptr
            return nullptr;
        }
        // else we have an image after clearing cache — continue to insert into cache
    }

    // find a slot: first empty, else the one with largest age (not used longest)
    int chosen = -1;
    for (size_t i = 0; i < cacheSize(); ++i)
    {
        if (!svgChache[i].occupied)
        {
            chosen = (int)i;
            break;
        }
    }

    unsigned long now2 = millis();
    if (chosen == -1)
    {
        // all occupied: pick the one with oldest lastUsed (largest now - lastUsed)
        unsigned long bestAge = 0;
        int bestIndex = 0;
        for (size_t i = 0; i < cacheSize(); ++i)
        {
            unsigned long age = now2 - svgChache[i].lastUsed;
            if (age >= bestAge)
            {
                bestAge = age;
                bestIndex = (int)i;
            }
        }
        chosen = bestIndex;
    }

    // Replace chosen slot (free previous image if any)
    if (svgChache[chosen].occupied && svgChache[chosen].image)
    {
        nsvgDelete(svgChache[chosen].image);
    }

    svgChache[chosen].image = image;
    svgChache[chosen].lastUsed = now2;
    svgChache[chosen].complexity = complexity;
    svgChache[chosen].id = id;
    svgChache[chosen].fp = fp;
    svgChache[chosen].occupied = true;

    return image;
}

bool drawSVGString(const String &imageStr,
                   int xOff, int yOff,
                   int targetW, int targetH,
                   uint16_t color, int steps)
{
    NSVGimage *image = createSVG(imageStr);
    if (!image || image->width <= 0 || image->height <= 0)
    {
        return false;
    }

    // Compute scale once
    const float scale = std::min(
        (float)targetW / image->width,
        (float)targetH / image->height);

    // Loop shapes
    for (NSVGshape *shape = image->shapes; shape; shape = shape->next)
    {
        for (NSVGpath *path = shape->paths; path; path = path->next)
        {
            const float *pts = path->pts;
            const int npts = path->npts;

            // Iterate through cubic bezier segments
            for (int i = 0; i < npts - 1; i += 3)
            {
                // Pre-scale + offset once per control point
                const float x1 = pts[i * 2 + 0] * scale + xOff;
                const float y1 = pts[i * 2 + 1] * scale + yOff;
                const float x2 = pts[(i + 1) * 2 + 0] * scale + xOff;
                const float y2 = pts[(i + 1) * 2 + 1] * scale + yOff;
                const float x3 = pts[(i + 2) * 2 + 0] * scale + xOff;
                const float y3 = pts[(i + 2) * 2 + 1] * scale + yOff;
                const float x4 = pts[(i + 3) * 2 + 0] * scale + xOff;
                const float y4 = pts[(i + 3) * 2 + 1] * scale + yOff;

                float px = x1, py = y1;

                // Use De Casteljau’s algorithm to avoid repeated powf/multiplications
                for (int s = 1; s <= steps; s++)
                {
                    const float t = (float)s / steps;
                    const float it = 1.0f - t;

                    const float bx =
                        it * it * it * x1 +
                        3 * it * it * t * x2 +
                        3 * it * t * t * x3 +
                        t * t * t * x4;

                    const float by =
                        it * it * it * y1 +
                        3 * it * it * t * y2 +
                        3 * it * t * t * y3 +
                        t * t * t * y4;

                    Screen::tft.drawLine((int)px, (int)py, (int)bx, (int)by, color);
                    px = bx;
                    py = by;
                }
            }
        }
    }

    return true;
}
