#include "index.hpp"

#include <Arduino.h>

#include "../styles/global.hpp"
#include "../sys-apps/designer.hpp"
#include "../led/index.hpp"
#include "../fs/index.hpp"

using namespace Screen;

// single TFT instance (if you already define it in another file, remove this definition)
TFT_eSPI Screen::tft = TFT_eSPI(320, 240);

// Global threshold for movement timing
int Screen::MOVEMENT_TIME_THRESHOLD = 250;

// Internal state for touch calculations (adapted to allow remote override)
static uint16_t touchY = 0, touchX = 0;
static uint16_t lastTouchY = UINT16_MAX, lastTouchX = 0;
static uint32_t lastTime = 0;
static int screenBrightNess = -1;

// Remote override variables (set by SPI_Screen when remote DOWN/UP received)
static volatile bool remoteOverrideClicked = false;
static volatile uint16_t remoteOverrideX = 0;
static volatile uint16_t remoteOverrideY = 0;

// Mutex for TFT access (prevents concurrent access from other tasks)
static SemaphoreHandle_t tftMutex = NULL;

void Screen::setBrightness(byte b, bool store)
{
    if (b < BRIGHTNESS_MIN && !store)
        b = BRIGHTNESS_MIN;
    analogWrite(TFT_BL, b);
    screenBrightNess = b;

    LED::refresh(b);

    if (store)
        SD_FS::writeFile("/settings/screen-brightness.txt", String(b));
}

byte Screen::getBrightness()
{
    if (screenBrightNess == -1)
    {
        int val = SD_FS::readFile("/settings/screen-brightness.txt").toInt();
        if (val < 0)
            val = 200;

        screenBrightNess = constrain((byte)val, (byte)BRIGHTNESS_MIN, (byte)255);
    }

    return screenBrightNess;
}

void Screen::init()
{
    applyColorPalette();

    if (tftMutex == NULL)
        tftMutex = xSemaphoreCreateMutex();

    tft.init();
    tft.setRotation(2);

    tft.fillScreen(BG);
    tft.setTextColor(TEXT);
    tft.setTextSize(2);
    tft.setCursor(0, 0);

    auto brightness = getBrightness();

#ifndef USE_STARTUP_ANIMATION
    setBrightness(brightness);
#endif

#ifdef TOUCH_CS
    tft.begin();
#endif
}

// isTouched considers local touch sensor *or* remote override clicks from the web client
bool Screen::isTouched()
{
    // remote override has priority (so web clicks simulate touch)
    if (remoteOverrideClicked)
        return true;

    // otherwise query the real touch controller
    return tft.getTouch(&touchY, &touchX);
}

Screen::TouchPos Screen::getTouchPos()
{
    TouchPos pos{};
    uint32_t now = millis();

    // If remote override is active, return the override coordinates as a stable "click"
    if (remoteOverrideClicked)
    {
        pos.clicked = true;
        pos.x = remoteOverrideX;
        pos.y = remoteOverrideY;

        // Movement from remote is not provided, return zeroed movement
        pos.move = {0, 0};

        // seed lastTouch values so local touch movement calculations continue later
        lastTouchX = pos.x;
        lastTouchY = pos.y;
        lastTime = now;
        return pos;
    }

    // Local touch path
    pos.clicked = tft.getTouch(&touchY, &touchX);
    if (pos.clicked)
    {
        // Map raw touch to screen coords (same mapping as original)
        pos.x = touchX * 32 / 24;
        pos.y = (320 - touchY) * 24 / 32;

        if (lastTouchY == UINT16_MAX)
        {
            lastTouchY = pos.y;
            lastTouchX = pos.x;
        }

        uint32_t delta = now - lastTime;
        if (delta < MOVEMENT_TIME_THRESHOLD)
        {
            pos.move.x = pos.x - lastTouchX;
            pos.move.y = pos.y - lastTouchY;
        }
        else
        {
            pos.move = {0, 0};
        }

        lastTouchY = pos.y;
        lastTouchX = pos.x;
        lastTime = now;
    }

    return pos;
}

void Screen::drawImageFromSD(const char *filename, int x, int y)
{
    File f = SD.open(filename, FILE_READ);
    if (!f)
        return;
    uint16_t w = f.read() << 8 | f.read();
    uint16_t h = f.read() << 8 | f.read();
    for (int j = 0; j < h; j++)
    {
        for (int i = 0; i < w; i++)
        {
            uint16_t color = f.read() << 8 | f.read();
            // guard tft access with mutex
            if (tftMutex)
                xSemaphoreTake(tftMutex, portMAX_DELAY);
            tft.drawPixel(x + i, y + j, color);
            if (tftMutex)
                xSemaphoreGive(tftMutex);
        }
    }
    f.close();
}

//
// SPI_Screen implementation: simple framed protocol over Serial (USB CDC).
// Robust to other Serial prints by scanning for headers and using chunk framing.
//
// Protocol (host -> device):
//  Header: 0xAA 0x55
//  Cmd: 0x01 = GET_FRAME (no payload)
//       0x02 = DOWN (payload: x_hi x_lo y_hi y_lo) (uint16 big-endian)
//       0x03 = UP (no payload)
//  Checksum: 1 byte (sum of all bytes after header up to checksum-1) & 0xFF
//
// Protocol (device -> host) for frame chunks:
//  ASCII header 'F' 'R' (0x46 0x52)
//  row: uint16_t big-endian (row index 0..239)
//  count: uint16_t big-endian (number of pixels in this chunk; typically 320)
//  payload: count * 2 bytes (RGB565 big-endian per pixel)
//  checksum: uint8_t (sum of header bytes + row bytes + count bytes + payload bytes) & 0xFF
//
// The viewer expects rows sent in any order but typically sequential 0..239. After each chunk we yield to let other tasks (and stray Serial prints) run.

namespace Screen
{
    namespace SPI_Screen
    {
        static const uint8_t CMD_GET_FRAME = 0x01;
        static const uint8_t CMD_DOWN = 0x02;
        static const uint8_t CMD_UP = 0x03;

        // helper to read uint16 BE
        static inline uint16_t be16(const uint8_t *p) { return (uint16_t(p[0]) << 8) | uint16_t(p[1]); }

        // utility: write 2-byte big-endian
        static inline void write_be16(uint16_t v)
        {
            Serial.write((v >> 8) & 0xFF);
            Serial.write(v & 0xFF);
        }

        void setRemoteDown(int16_t x, int16_t y)
        {
            remoteOverrideX = (uint16_t)x;
            remoteOverrideY = (uint16_t)y;
            remoteOverrideClicked = true;
        }

        void setRemoteUp()
        {
            remoteOverrideClicked = false;
        }

        // send a single row chunk (row index [0..239], count px, payload 2*count bytes)
        static void sendRowChunk(uint16_t row, const uint16_t *pixels, uint16_t count)
        {
            // header 'FR'
            Serial.write('F');
            Serial.write('R');

            // accumulate checksum
            uint8_t chksum = 0;
            chksum += 'F';
            chksum += 'R';

            // row BE
            uint8_t rhi = (row >> 8) & 0xFF;
            uint8_t rlo = row & 0xFF;
            Serial.write(rhi);
            chksum += rhi;
            Serial.write(rlo);
            chksum += rlo;

            // count BE
            uint8_t chi = (count >> 8) & 0xFF;
            uint8_t clo = count & 0xFF;
            Serial.write(chi);
            chksum += chi;
            Serial.write(clo);
            chksum += clo;

            // payload: each pixel big-endian
            for (uint16_t i = 0; i < count; ++i)
            {
                uint16_t color = pixels[i];
                uint8_t hi = (color >> 8) & 0xFF;
                uint8_t lo = color & 0xFF;
                Serial.write(hi);
                chksum += hi;
                Serial.write(lo);
                chksum += lo;
            }

            Serial.write(chksum);
        }

        // Primary screenTask
        void screenTask(void *pvParameters)
        {
            // Buffer to hold a single row of pixels
            static uint16_t rowBuf[320];

            for (;;)
            {
                // Non-blocking read loop: scan for header sequence 0xAA 0x55
                if (Serial.available())
                {
                    // try to find header
                    int b = Serial.read();
                    if (b != 0xAA)
                    {
                        // skip and continue
                        vTaskDelay(1);
                        continue;
                    }
                    // try next
                    uint32_t t0 = millis();
                    while (!Serial.available())
                    {
                        if (millis() - t0 > 50)
                            break;
                        vTaskDelay(1);
                    }
                    if (!Serial.available())
                        continue;
                    int b2 = Serial.read();
                    if (b2 != 0x55)
                        continue;

                    // read command and payload+checksum properly
                    // read cmd
                    while (!Serial.available())
                        vTaskDelay(1);
                    uint8_t cmd = Serial.read();

                    // Determine payload length
                    if (cmd == CMD_GET_FRAME)
                    {
                        // GET_FRAME: send full framebuffer in row chunks.
                        // Acquire tft mutex while reading each row (short lock)
                        for (uint16_t row = 0; row < 240; ++row)
                        {
                            // read row pixels into rowBuf
                            if (tftMutex)
                                xSemaphoreTake(tftMutex, portMAX_DELAY);
                            // tft.readPixel is used repeatedly — may be slow but acceptable
                            for (uint16_t x = 0; x < 320; ++x)
                            {
                                uint16_t c = tft.readPixel(x, row);
                                rowBuf[x] = c;
                            }
                            if (tftMutex)
                                xSemaphoreGive(tftMutex);

                            // send row chunk
                            sendRowChunk(row, rowBuf, 320);

                            // small yield so other tasks and stray Serial prints can run
                            vTaskDelay(1);
                        }
                    }
                    else if (cmd == CMD_DOWN)
                    {
                        // read payload (4 bytes) + checksum (1)
                        // wait for 5 bytes
                        uint32_t t1 = millis();
                        while (Serial.available() < 5)
                        {
                            if (millis() - t1 > 200)
                                break;
                            vTaskDelay(1);
                        }
                        // read 4 payload bytes
                        uint8_t p[4] = {0,0,0,0};
                        for (int i = 0; i < 4 && Serial.available(); ++i)
                            p[i] = Serial.read();
                        // read checksum
                        uint8_t chk = 0;
                        if (Serial.available())
                            chk = Serial.read();

                        // verify checksum: cmd + payload sum
                        uint8_t sum = cmd;
                        for (int i = 0; i < 4; ++i)
                            sum += p[i];
                        if (sum == chk)
                        {
                            uint16_t x = be16(p);
                            uint16_t y = be16(p + 2);
                            // clamp to screen
                            if (x >= 320)
                                x = 319;
                            if (y >= 240)
                                y = 239;
                            setRemoteDown(x, y);
                        }
                    }
                    else if (cmd == CMD_UP)
                    {
                        // read checksum byte
                        uint32_t t2 = millis();
                        while (!Serial.available())
                        {
                            if (millis() - t2 > 200)
                                break;
                            vTaskDelay(1);
                        }
                        if (Serial.available())
                        {
                            uint8_t chk = Serial.read();
                            // verify checksum is equal to cmd
                            if (chk == cmd)
                                setRemoteUp();
                        }
                        else
                        {
                            setRemoteUp();
                        }
                    }
                    else
                    {
                        // unknown command: skip
                    }
                }
                // small delay
                vTaskDelay(2);
            } // for
        }

        void startScreen()
        {
            // create the task pinned to core 1, high priority
            if (tftMutex == NULL)
                tftMutex = xSemaphoreCreateMutex();

            const BaseType_t priority = configMAX_PRIORITIES - 2; // high but not absolute max
            const uint32_t stack = 8 * 1024;
            xTaskCreatePinnedToCore(screenTask, "ScreenTask", stack / sizeof(StackType_t), NULL, priority, NULL, 1);
            // ensure Serial is started (USB CDC)
            Serial.begin(115200);
            // small pause
            delay(50);
        }
    } // namespace SPI_Screen
} // namespace Screen
