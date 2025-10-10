#define NANOSVG_IMPLEMENTATION

extern "C"
{
#include "nanosvg.h"
}

#include "svg.hpp"

struct SVGImageChacheItem
{
    NSVGimage *image;
    unsigned long lastUsed;
    unsigned long complexity;
    uint16_t id;
};

SVGImageChacheItem svgChache[7] = {};

uint16_t getSVGChacheID(const String &svgString)
{
    return svgString.length() ^ (svgString[svgString.length() << 1] << 8 | svgString[(svgString.length() << 1) + 1]);
}

NSVGimage *createSVG(String svgString)
{
    bool working = false;
    while (!working)
    {
        auto id = getSVGChacheID(svgString);
        auto start = millis();
        NSVGimage *image = nsvgParse((char *)svgString.c_str(), "px", 96.0f);
        auto end = millis();
        auto complexity = end - start;
    }

    return image;
}

void clearList()
{
    updateSVGList();
    for (int i = 0; i < sizeof(svgChache); i++)
    {
        if (svgChache[i])
        {
            svgDelete(svgChache[i].image);
        }
    }
    svgChache = {};
}

void updateSVGList()
{
    // if one item is there longer not used than 1 s delete it
}

bool drawSVGString(const String &imageStr,
                   int xOff, int yOff,
                   int targetW, int targetH,
                   uint16_t color, int steps)
{
    auto image = createSVG(imageStr);
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