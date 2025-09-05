#define NANOSVG_IMPLEMENTATION

extern "C"
{
#include "nanosvg.h"
}

#include "svg.hpp"

bool ESP32_SVG::drawString(const String &svgString,
                           int xOff, int yOff,
                           int targetW, int targetH,
                           uint16_t color)
{
    NSVGimage *image = nsvgParse((char *)svgString.c_str(), "px", 96.0f);
    if (!image)
        return false;

    float scaleX = (float)targetW / image->width;
    float scaleY = (float)targetH / image->height;
    float scale = min(scaleX, scaleY);

    for (NSVGshape *shape = image->shapes; shape; shape = shape->next)
    {
        for (NSVGpath *path = shape->paths; path; path = path->next)
        {
            float *pts = path->pts;
            int npts = path->npts;

            for (int i = 0; i < npts - 1; i += 3)
            {
                float x1 = pts[i * 2 + 0] * scale + xOff;
                float y1 = pts[i * 2 + 1] * scale + yOff;
                float x2 = pts[(i + 1) * 2 + 0] * scale + xOff;
                float y2 = pts[(i + 1) * 2 + 1] * scale + yOff;
                float x3 = pts[(i + 2) * 2 + 0] * scale + xOff;
                float y3 = pts[(i + 2) * 2 + 1] * scale + yOff;
                float x4 = pts[(i + 3) * 2 + 0] * scale + xOff;
                float y4 = pts[(i + 3) * 2 + 1] * scale + yOff;

                const int steps = 20;
                float px = x1, py = y1;
                for (int s = 1; s <= steps; s++)
                {
                    float t = (float)s / steps;
                    float it = 1.0f - t;
                    float bx = it * it * it * x1 + 3 * it * it * t * x2 + 3 * it * t * t * x3 + t * t * t * x4;
                    float by = it * it * it * y1 + 3 * it * it * t * y2 + 3 * it * t * t * y3 + t * t * t * y4;
                    Screen::tft.drawLine((int)px, (int)py, (int)bx, (int)by, color);
                    px = bx;
                    py = by;
                }
            }
        }
    }

    nsvgDelete(image);
    return true;
}
