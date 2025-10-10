#define NANOSVG_IMPLEMENTATION
extern "C"
{
#include "nanosvg.h"
}

#include "svg.hpp"
#include <vector>
#include <algorithm>

constexpr size_t SVG_CACHE_MAX_SIZE = 20 * 1024; // 20 KB
constexpr unsigned long CACHE_EXPIRE_MS = 1000;  // 1 second
constexpr size_t BLACKLIST_MAX_SIZE = 10;

struct SvgCacheEntry
{
    String id;              // Unique ID: length + middle 4 chars
    size_t memCost;         // Memory used by parsed SVG
    uint32_t uses;          // Access count
    NSVGimage *image;       // Parsed image
    unsigned long lastUsed; // millis() timestamp
};

// Cache storage
static std::vector<SvgCacheEntry> svgCache;
static size_t svgCacheUsed = 0;

// Blacklist: simple hash of unrenderable SVGs
static std::vector<String> svgBlacklist;

// ------------------------------------------------------
// Generate identifier: length + middle 4 chars
// ------------------------------------------------------
static String makeSVGId(const String &s)
{
    int len = s.length();
    String mid = "";
    if (len >= 4)
    {
        int start = len / 2 - 2;
        mid = s.substring(start, start + 4);
    }
    return String(len) + "_" + mid;
}

// ------------------------------------------------------
// Check blacklist
// ------------------------------------------------------
static bool isBlacklisted(const String &id)
{
    for (auto &b : svgBlacklist)
    {
        if (b == id)
            return true;
    }
    return false;
}

// ------------------------------------------------------
// Estimate memory usage
// ------------------------------------------------------
static size_t estimateSVGSize(NSVGimage *img)
{
    if (!img)
        return 0;
    size_t total = sizeof(NSVGimage);
    for (NSVGshape *s = img->shapes; s; s = s->next)
    {
        total += sizeof(NSVGshape);
        for (NSVGpath *p = s->paths; p; p = p->next)
            total += sizeof(NSVGpath) + (p->npts * 2 * sizeof(float));
    }
    return total;
}

// ------------------------------------------------------
// Prune cache until enough memory
// ------------------------------------------------------
static void pruneCacheForce(size_t requiredFree)
{
    unsigned long now = millis();

    while (svgCacheUsed + requiredFree > SVG_CACHE_MAX_SIZE && !svgCache.empty())
    {
        auto it = std::max_element(svgCache.begin(), svgCache.end(),
                                   [=](const SvgCacheEntry &a, const SvgCacheEntry &b)
                                   {
                                       unsigned long ageA = now - a.lastUsed;
                                       unsigned long ageB = now - b.lastUsed;
                                       float scoreA = (ageA > CACHE_EXPIRE_MS ? 1000.0f : 0.0f) + (float)a.memCost / (a.uses + 1);
                                       float scoreB = (ageB > CACHE_EXPIRE_MS ? 1000.0f : 0.0f) + (float)b.memCost / (b.uses + 1);
                                       return scoreA < scoreB;
                                   });

        if (it != svgCache.end())
        {
            if (it->image)
                nsvgDelete(it->image);
            svgCacheUsed -= it->memCost;
            svgCache.erase(it);
        }
        else
            break;
    }
}

// ------------------------------------------------------
// Create or fetch SVG from cache (with blacklist & retry)
// ------------------------------------------------------
NSVGimage *createSVG(const String &svgString)
{
    String id = makeSVGId(svgString);
    unsigned long now = millis();

    if (isBlacklisted(id))
        return nullptr;

    // Check cache
    for (auto &entry : svgCache)
    {
        if (entry.id == id)
        {
            entry.uses++;
            entry.lastUsed = now;
            return entry.image;
        }
    }

    // First attempt
    NSVGimage *img = nsvgParse((char *)svgString.c_str(), "px", 96.0f);
    if (!img)
    {
        // Free all cache and retry
        for (auto &entry : svgCache)
            if (entry.image)
                nsvgDelete(entry.image);
        svgCache.clear();
        svgCacheUsed = 0;

        img = nsvgParse((char *)svgString.c_str(), "px", 96.0f);
        if (!img)
        {
            // Add to blacklist
            if (svgBlacklist.size() >= BLACKLIST_MAX_SIZE)
                svgBlacklist.erase(svgBlacklist.begin());
            svgBlacklist.push_back(id);
            return nullptr;
        }
    }

    size_t cost = estimateSVGSize(img);

    pruneCacheForce(cost);

    // Skip caching if too big
    if (cost > SVG_CACHE_MAX_SIZE)
        return img;

    SvgCacheEntry entry;
    entry.id = id;
    entry.memCost = cost;
    entry.uses = 1;
    entry.image = img;
    entry.lastUsed = now;

    svgCache.push_back(entry);
    svgCacheUsed += cost;

    return img;
}

#include "svg.hpp"

// Draw an SVG string directly on screen, with blacklist & smart cache
bool drawSVGString(const String &imageStr,
                   int xOff,
                   int yOff,
                   int targetW,
                   int targetH,
                   uint16_t color,
                   int steps)
{
    // Check blacklist early
    String id = makeSVGId(imageStr);
    if (isBlacklisted(id))
        return false;

    // Try to get/create the SVG
    NSVGimage *image = createSVG(imageStr);
    if (!image || image->width <= 0 || image->height <= 0)
    {
        return false;
    }

    float scale = std::min((float)targetW / image->width, (float)targetH / image->height);

    for (NSVGshape *shape = image->shapes; shape; shape = shape->next)
    {
        for (NSVGpath *path = shape->paths; path; path = path->next)
        {
            if (!path->pts || path->npts < 4)
                continue;

            const float *pts = path->pts;
            int npts = path->npts;

            for (int i = 0; i + 3 < npts; i += 3)
            {
                float x1 = pts[(i + 0) * 2 + 0] * scale + xOff;
                float y1 = pts[(i + 0) * 2 + 1] * scale + yOff;
                float x2 = pts[(i + 1) * 2 + 0] * scale + xOff;
                float y2 = pts[(i + 1) * 2 + 1] * scale + yOff;
                float x3 = pts[(i + 2) * 2 + 0] * scale + xOff;
                float y3 = pts[(i + 2) * 2 + 1] * scale + yOff;
                float x4 = pts[(i + 3) * 2 + 0] * scale + xOff;
                float y4 = pts[(i + 3) * 2 + 1] * scale + yOff;

                float px = x1, py = y1;
                for (int s = 1; s <= steps; s++)
                {
                    float t = (float)s / steps;
                    float it = 1.0f - t;
                    float bx = it * it * it * x1 + 3 * it * it * t * x2 + 3 * it * t * t * x3 + t * t * t * x4;
                    float by = it * it * it * y1 + 3 * it * it * t * y2 + 3 * it * t * t * y3 + t * t * t * y4;

                    if (!isnan(bx) && !isnan(by))
                        Screen::tft.drawLine((int)px, (int)py, (int)bx, (int)by, color);

                    px = bx;
                    py = by;
                }
            }

            // Close path if needed
            if (path->closed)
            {
                float xStart = pts[0] * scale + xOff;
                float yStart = pts[1] * scale + yOff;
                float xEnd = pts[(npts - 1) * 2 + 0] * scale + xOff;
                float yEnd = pts[(npts - 1) * 2 + 1] * scale + yOff;
                Screen::tft.drawLine((int)xEnd, (int)yEnd, (int)xStart, (int)yStart, color);
            }
        }
    }

    // Free image if not cached (big objects may not fit)
    bool cached = false;
    for (auto &e : svgCache)
    {
        if (e.id == id)
        {
            cached = true;
            break;
        }
    }
    if (!cached)
        nsvgDelete(image);

    return true;
}
