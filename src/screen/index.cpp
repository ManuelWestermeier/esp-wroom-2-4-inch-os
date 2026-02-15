// index.hpp (full file as provided, updated)
#include "index.hpp"

#include <Arduino.h>

#include "../apps/windows.hpp"
#include "../styles/global.hpp"
#include "../sys-apps/designer.hpp"
#include "../led/index.hpp"
#include "../fs/index.hpp"
#include "../apps/index.hpp"

using namespace Screen;

// single TFT instance (if you already define it in another file, remove this definition)
TFT_eSPI Screen::tft = TFT_eSPI(320, 240);

// Global threshold for movement timing
int Screen::MOVEMENT_TIME_THRESHOLD = 250;

// Remote timeout (ms) — if last remote cursor update is older than this,
// remote input is ignored and local touch is used.
static const uint32_t REMOTE_TIMEOUT_MS = 500;

// Internal state for touch calculations (adapted to allow remote override)
static uint16_t touchX = 0, touchY = 0;
static uint16_t lastTouchX = UINT16_MAX, lastTouchY = 0;
static uint32_t lastTime = 0;
static int screenBrightNess = -1;

// Remote override variables (set by SPI_Screen when remote DOWN/UP/ MOVE received)
static volatile bool remoteOverrideClicked = false;
static volatile uint16_t remoteOverrideX = 0;
static volatile uint16_t remoteOverrideY = 0;
static volatile uint32_t lastRemoteMillis = 0; // timestamp of last remote activity (down/move/up)

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

// isTouched considers local touch sensor *or* a recent remote override click from the web client
bool Screen::isTouched()
{
    // remote override click has priority but only when recent
    if (remoteOverrideClicked && (millis() - lastRemoteMillis <= REMOTE_TIMEOUT_MS))
        return true;

    // otherwise query the real touch controller once
    // NOTE: getTouch expects pointers x,y in that order
    bool touched = false;
    if (tftMutex)
    {
        // attempt to take mutex but don't block forever
        if (xSemaphoreTake(tftMutex, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            touched = tft.getTouch(&touchX, &touchY);
            xSemaphoreGive(tftMutex);
        }
        else
        {
            // could not obtain mutex: assume not touched to avoid blocking UI
            touched = false;
        }
    }
    else
    {
        touched = tft.getTouch(&touchX, &touchY);
    }

    return touched;
}

Screen::TouchPos Screen::getTouchPos()
{
    TouchPos pos{};
    uint32_t now = millis();

    // If a remote click override is very recent, treat it as a stable "click"
    if (remoteOverrideClicked && (now - lastRemoteMillis <= REMOTE_TIMEOUT_MS))
    {
        pos.clicked = true;
        pos.x = remoteOverrideX;
        pos.y = remoteOverrideY;

        // Movement from remote click-down is not provided, return zeroed movement
        pos.move = {0, 0};

        // seed lastTouch values so local touch movement calculations continue later
        lastTouchX = pos.x;
        lastTouchY = pos.y;
        lastTime = now;
        return pos;
    }

    // If a remote cursor position (non-click) is recent, use it as positional input
    if ((now - lastRemoteMillis <= REMOTE_TIMEOUT_MS) && lastRemoteMillis != 0)
    {
        // Remote sends cursor updates (MOVE) without click. Provide the coordinates
        // but mark clicked = false so UI knows it's only a cursor.
        pos.clicked = false;
        pos.x = remoteOverrideX;
        pos.y = remoteOverrideY;

        // compute move similar to local touch movement logic
        if (lastTouchX == UINT16_MAX)
        {
            lastTouchX = pos.x;
            lastTouchY = pos.y;
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

        lastTouchX = pos.x;
        lastTouchY = pos.y;
        lastTime = now;
        return pos;
    }

    // Local touch path - call getTouch only once
    uint16_t rawX = 0, rawY = 0;
    bool clicked = false;

    if (tftMutex)
    {
        if (xSemaphoreTake(tftMutex, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            clicked = tft.getTouch(&rawX, &rawY);
            xSemaphoreGive(tftMutex);
        }
        else
        {
            clicked = false;
        }
    }
    else
    {
        clicked = tft.getTouch(&rawX, &rawY);
    }

    if (clicked)
    {
        // Map raw touch to screen coords.
        // Typical raw values are 0..4095 for resistive touch; clamp defensively.
        const uint16_t rawMax = 4095;
        uint16_t sw = tft.width();
        uint16_t sh = tft.height();

        // scale and clamp
        uint32_t sx = (uint32_t)rawX * (uint32_t)sw;
        uint32_t sy = (uint32_t)rawY * (uint32_t)sh;
        pos.x = constrain((int)(sx / rawMax), 0, sw - 1);
        pos.y = constrain((int)(sy / rawMax), 0, sh - 1);

        // If your rotation inverts axes, adjust here (example commented):
        // uint8_t rot = tft.getRotation();
        // if (rot == 1 || rot == 3) { swap(pos.x, pos.y); /* and/or invert */ }

        if (lastTouchX == UINT16_MAX)
        {
            lastTouchX = pos.x;
            lastTouchY = pos.y;
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

        lastTouchX = pos.x;
        lastTouchY = pos.y;
        lastTime = now;
    }

    pos.clicked = clicked;
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
//
namespace Screen
{
    namespace SPI_Screen
    {
        static const uint8_t CMD_GET_FRAME = 0x01;
        static const uint8_t CMD_DOWN = 0x02;
        static const uint8_t CMD_UP = 0x03;
        static const uint8_t CMD_MOVE = 0x04; // new: update cursor position without click

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
            lastRemoteMillis = millis();
        }

        void setRemoteUp()
        {
            remoteOverrideClicked = false;
            lastRemoteMillis = millis();
        }

        // set remote cursor position (no click)
        void setRemoteMove(int16_t x, int16_t y)
        {
            remoteOverrideX = (uint16_t)x;
            remoteOverrideY = (uint16_t)y;
            lastRemoteMillis = millis();
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
                if (Serial.available())
                {
                    // try to find header 0xAA
                    int b = Serial.read();
                    if (b != 0xAA)
                    {
                        vTaskDelay(1);
                        continue;
                    }

                    // wait for second header byte
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

                    // read cmd (wait)
                    while (!Serial.available())
                        vTaskDelay(1);
                    uint8_t cmd = Serial.read();

                    // If GET_FRAME, consume checksum byte if present (client sends checksum byte)
                    if (cmd == CMD_GET_FRAME)
                    {
                        // read/discard checksum (do not block forever)
                        uint32_t tchk = millis();
                        while (!Serial.available())
                        {
                            if (millis() - tchk > 200)
                                break;
                            vTaskDelay(1);
                        }
                        if (Serial.available())
                            (void)Serial.read();

                        while (!Windows::canAccess)
                        {
                            delay(rand() % 2);
                        }
                        Windows::canAccess = false;

                        // GET_FRAME: send full framebuffer in row chunks.
                        for (uint16_t row = 0; row < 240; ++row)
                        {
                            // copy row pixels into rowBuf under mutex, but avoid blocking forever
                            if (tftMutex)
                            {
                                if (xSemaphoreTake(tftMutex, pdMS_TO_TICKS(200)) == pdTRUE)
                                {
                                    // read pixels (may be slow depending on driver)
                                    for (uint16_t x = 0; x < 320; ++x)
                                    {
                                        rowBuf[x] = tft.readPixel(x, row);
                                    }
                                    xSemaphoreGive(tftMutex);
                                }
                                else
                                {
                                    // failed to lock: fill with blank row to avoid much delay for client
                                    for (uint16_t x = 0; x < 320; ++x)
                                        rowBuf[x] = 0;
                                }
                            }
                            else
                            {
                                for (uint16_t x = 0; x < 320; ++x)
                                    rowBuf[x] = tft.readPixel(x, row);
                            }

                            // send row chunk (this may block for USB; it's expected)
                            sendRowChunk(row, rowBuf, 320);

                            // yield so other tasks can run
                            vTaskDelay(1);
                        }
                        Windows::canAccess = true;
                    }
                    else if (cmd == CMD_DOWN)
                    {
                        // read payload (4 bytes) + checksum (1) with timeout
                        uint32_t t1 = millis();
                        while (Serial.available() < 5)
                        {
                            if (millis() - t1 > 200)
                                break;
                            vTaskDelay(1);
                        }
                        uint8_t p[4] = {0, 0, 0, 0};
                        for (int i = 0; i < 4 && Serial.available(); ++i)
                            p[i] = Serial.read();
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
                        // read checksum byte (non-blocking with timeout)
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
                            if (chk == cmd)
                                setRemoteUp();
                        }
                        else
                        {
                            setRemoteUp();
                        }
                    }
                    else if (cmd == CMD_MOVE)
                    {
                        // new: cursor move without click. read payload (4 bytes) + checksum (1) with timeout
                        uint32_t t3 = millis();
                        while (Serial.available() < 5)
                        {
                            if (millis() - t3 > 200)
                                break;
                            vTaskDelay(1);
                        }
                        uint8_t p[4] = {0, 0, 0, 0};
                        for (int i = 0; i < 4 && Serial.available(); ++i)
                            p[i] = Serial.read();
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
                            setRemoteMove(x, y);
                        }
                    }
                    else
                    {
                        // unknown command -> ignore
                    }
                }
                // small delay so CPU isn't fully busy
                vTaskDelay(2);
            } // for
        }

        void startScreen()
        {
            if (tftMutex == NULL)
                tftMutex = xSemaphoreCreateMutex();

            const BaseType_t priority = configMAX_PRIORITIES - 2;
            const uint32_t stack = 8 * 1024;
            xTaskCreatePinnedToCore(screenTask, "ScreenTask", stack / sizeof(StackType_t), NULL, priority, NULL, 1);
            delay(50);
        }
    } // namespace SPI_Screen
} // namespace Screen
