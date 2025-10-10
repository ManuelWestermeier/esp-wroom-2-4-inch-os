#define NANOSVG_IMPLEMENTATION
extern "C"
{
#include "nanosvg.h"
}

#include "svg.hpp"
#include <vector>
#include <algorithm>
#include <cmath>

constexpr size_t SVG_CACHE_MAX_SIZE = 20 * 1024; // 20 KB
constexpr unsigned long CACHE_EXPIRE_MS = 1000;  // 1 second

struct SvgCacheEntry
{
    String id; // length + 4 middle chars
    size_t memCost;
    uint32_t uses;
    NSVGimage *image;
    unsigned long lastUsed;
};

static std::vector<SvgCacheEntry> svgCache;
static size_t svgCacheUsed = 0;

// ------------------------------------------------------
// Generate ID: length + middle 4 chars
// ------------------------------------------------------
static String makeSVGId(const String &s)
{
    int len = s.length();
    String mid = "";
    if (len >= 4)
    {
        mid = s.substring(len / 2 - 2, len / 2 + 2);
    }
    return String(len) + "_" + mid;
}

// ------------------------------------------------------
// Estimate memory used by SVG
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
// Prune cache only if memory needed
// ------------------------------------------------------
static void pruneCacheForce(size_t requiredFree)
{
    unsigned long now = millis();

    while (svgCacheUsed + requiredFree > SVG_CACHE_MAX_SIZE && !svgCache.empty())
    {
        // Find entry not used recently (>1s) with lowest usage score
        auto it = std::max_element(svgCache.begin(), svgCache.end(),
                                   [=](const SvgCacheEntry &a, const SvgCacheEntry &b)
                                   {
                                       unsigned long ageA = now - a.lastUsed;
                                       unsigned long ageB = now - b.lastUsed;
                                       float scoreA = (ageA > CACHE_EXPIRE_MS ? 1000.0f : 0.0f) + (float)a.memCost / (a.uses + 1);
                                       float scoreB = (ageB > CACHE_EXPIRE_MS ? 1000.0f : 0.0f) + (float)b.memCost / (b.uses + 1);
                                       return scoreA < scoreB;
                                   });

        if (it != svgCache.end() && now - it->lastUsed > CACHE_EXPIRE_MS)
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
// Create/fetch SVG (full accuracy)
// ------------------------------------------------------
NSVGimage *createSVG(const String &svgString)
{
    String id = makeSVGId(svgString);
    unsigned long now = millis();

    for (auto &entry : svgCache)
    {
        if (entry.id == id)
        {
            entry.uses++;
            entry.lastUsed = now;
            return entry.image;
        }
    }

    NSVGimage *img = nsvgParse((char *)svgString.c_str(), "px", 96.0f);
    if (!img)
        return nullptr;

    size_t cost = estimateSVGSize(img);
    pruneCacheForce(cost);

    if (cost <= SVG_CACHE_MAX_SIZE)
    {
        SvgCacheEntry entry = {id, cost, 1, img, now};
        svgCache.push_back(entry);
        svgCacheUsed += cost;
    }

    return img;
}

// ------------------------------------------------------
// Draw SVG exactly as original
// ------------------------------------------------------
bool drawSVGString(const String &imageStr,
                   int xOff,
                   int yOff,
                   int targetW,
                   int targetH,
                   uint16_t color,
                   int steps)
{
    NSVGimage *image = createSVG(imageStr);
    if (!image || image->width <= 0 || image->height <= 0)
        return false;

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

                    // Full precision drawing
                    Screen::tft.drawLine((int)px, (int)py, (int)bx, (int)by, color);

                    px = bx;
                    py = by;
                }
            }

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

    // Free memory only if not cached
    String id = makeSVGId(imageStr);
    bool cached = false;
    for (auto &e : svgCache)
        if (e.id == id)
        {
            cached = true;
            break;
        }
    if (!cached)
        nsvgDelete(image);

    return true;
}
