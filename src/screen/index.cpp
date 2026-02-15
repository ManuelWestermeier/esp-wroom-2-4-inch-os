#include "index.hpp"
#include <Arduino.h>
#include "../apps/windows.hpp"
#include "../styles/global.hpp"
#include "../sys-apps/designer.hpp"
#include "../led/index.hpp"
#include "../fs/index.hpp"
#include "../apps/index.hpp"

using namespace Screen;

// single TFT instance
TFT_eSPI Screen::tft = TFT_eSPI(320, 240);

// Global threshold for movement timing
int Screen::MOVEMENT_TIME_THRESHOLD = 250;

// Remote timeout (ms)
static const uint32_t REMOTE_TIMEOUT_MS = 500;

// Internal state for touch calculations
static uint16_t touchX = 0, touchY = 0;
static uint16_t lastTouchX = UINT16_MAX, lastTouchY = 0;
static uint32_t lastTime = 0;
static int screenBrightNess = -1;

// --- RESTORED REMOTE VARIABLES ---
static volatile bool remoteOverrideClicked = false;
static volatile uint16_t remoteOverrideX = 0;
static volatile uint16_t remoteOverrideY = 0;
static volatile uint32_t lastRemoteMillis = 0;

// Mutex for TFT access (ESSENTIAL for SPI_Screen task)
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

    auto brightness = getBrightness();
#ifndef USE_STARTUP_ANIMATION
    setBrightness(brightness);
#endif

#ifdef TOUCH_CS
    tft.begin();
#endif
}

bool Screen::isTouched()
{
    return tft.getTouch(&touchY, &touchX);
}

Screen::TouchPos Screen::getTouchPos()
{
    TouchPos pos{};
    uint32_t now = millis();

    pos.clicked = tft.getTouch(&touchY, &touchX);
    if (pos.clicked)
    {
        // Map raw touch to screen coords
        pos.x = touchX * 32 / 24;
        pos.y = (320 - touchY) * 24 / 32;

        // First touch ever: seed last positions
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
            if (tftMutex)
                xSemaphoreTake(tftMutex, portMAX_DELAY);
            tft.drawPixel(x + i, y + j, color);
            if (tftMutex)
                xSemaphoreGive(tftMutex);
        }
    }
    f.close();
}

namespace Screen
{
    namespace SPI_Screen
    {
        static const uint8_t CMD_GET_FRAME = 0x01;
        static const uint8_t CMD_DOWN = 0x02;
        static const uint8_t CMD_UP = 0x03;
        static const uint8_t CMD_MOVE = 0x04;

        static inline uint16_t be16(const uint8_t *p) { return (uint16_t(p[0]) << 8) | uint16_t(p[1]); }

        void setRemoteDown(int16_t x, int16_t y)
        {
            remoteOverrideX = (uint16_t)x;
            remoteOverrideY = (uint16_t)y;
            remoteOverrideClicked = true;
            lastRemoteMillis = millis();
        }

        void setRemoteUp()
        {
            remoteOverrideClicked = false;
            lastRemoteMillis = millis();
        }

        void setRemoteMove(int16_t x, int16_t y)
        {
            remoteOverrideX = (uint16_t)x;
            remoteOverrideY = (uint16_t)y;
            lastRemoteMillis = millis();
        }

        static void sendRowChunk(uint16_t row, const uint16_t *pixels, uint16_t count)
        {
            Serial.write('F');
            Serial.write('R');
            uint8_t chksum = 'F' + 'R';

            uint8_t rhi = row >> 8;
            uint8_t rlo = row & 0xFF;
            Serial.write(rhi);
            chksum += rhi;
            Serial.write(rlo);
            chksum += rlo;

            // count now means pixel count (still 320)
            uint8_t chi = count >> 8;
            uint8_t clo = count & 0xFF;
            Serial.write(chi);
            chksum += chi;
            Serial.write(clo);
            chksum += clo;

            for (uint16_t i = 0; i < count; i += 2)
            {
                auto toGray4 = [](uint16_t c) -> uint8_t
                {
                    uint8_t r = (c >> 11) & 0x1F;
                    uint8_t g = (c >> 5) & 0x3F;
                    uint8_t b = c & 0x1F;

                    // expand to 8-bit
                    uint8_t r8 = (r << 3) | (r >> 2);
                    uint8_t g8 = (g << 2) | (g >> 4);
                    uint8_t b8 = (b << 3) | (b >> 2);

                    // luminance
                    uint8_t gray8 = (r8 * 30 + g8 * 59 + b8 * 11) / 100;

                    return gray8 >> 4; // 0–15 (4-bit)
                };

                uint8_t g1 = toGray4(pixels[i]);
                uint8_t g2 = (i + 1 < count) ? toGray4(pixels[i + 1]) : g1;

                uint8_t packed = (g1 << 4) | g2;

                Serial.write(packed);
                chksum += packed;
            }

            Serial.write(chksum);
        }

        void screenTask(void *pvParameters)
        {
            static uint16_t rowBuf[320];
            for (;;)
            {
                if (Serial.available())
                {
                    if (Serial.read() != 0xAA)
                    {
                        vTaskDelay(1);
                        continue;
                    }
                    uint32_t t0 = millis();
                    while (!Serial.available() && (millis() - t0 < 50))
                        vTaskDelay(1);
                    if (Serial.read() != 0x55)
                        continue;

                    while (!Serial.available())
                        vTaskDelay(1);
                    uint8_t cmd = Serial.read();

                    // Inside Screen::SPI_Screen::screenTask loop
                    if (cmd == CMD_GET_FRAME)
                    {
                        // Discard client checksum
                        uint32_t tchk = millis();
                        while (!Serial.available() && (millis() - tchk < 100))
                            vTaskDelay(1);
                        if (Serial.available())
                            Serial.read();

                        for (uint16_t row = 0; row < 240; ++row)
                        {
                            if (tftMutex && xSemaphoreTake(tftMutex, pdMS_TO_TICKS(50)) == pdTRUE)
                            {
                                for (uint16_t x = 0; x < 320; ++x)
                                {
                                    // Use the public readPixel method
                                    rowBuf[x] = tft.readPixel(x, row);
                                }
                                xSemaphoreGive(tftMutex);
                            }
                            else
                            {
                                memset(rowBuf, 0, sizeof(rowBuf));
                            }
                            vTaskDelay(1);
                            // Send the high-quality 16-bit data to the browser
                            sendRowChunk(row, rowBuf, 320);

                            // Slight yield to keep the ESP32 stable
                            vTaskDelay(1);
                        }
                    }
                    else if (cmd == CMD_DOWN || cmd == CMD_MOVE)
                    {
                        uint8_t p[5];
                        Serial.readBytes(p, 5); // 4 bytes payload + 1 checksum
                        uint16_t x = be16(p);
                        uint16_t y = be16(p + 2);
                        if (cmd == CMD_DOWN)
                            setRemoteDown(x, y);
                        else
                            setRemoteMove(x, y);
                    }
                    else if (cmd == CMD_UP)
                    {
                        setRemoteUp();
                    }
                }
                vTaskDelay(2);
            }
        }

        void startScreen()
        {
            if (tftMutex == NULL)
                tftMutex = xSemaphoreCreateMutex();
            xTaskCreatePinnedToCore(screenTask, "ScreenTask", 8192, NULL, configMAX_PRIORITIES - 2, NULL, 1);
        }
    }
}