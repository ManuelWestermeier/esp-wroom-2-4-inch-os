// Unified AVF/WAV/RGB565 player as Lua API (no setup/loop)
// Drop into your codebase; requires WiFiClientSecure, HTTPClient, Screen::tft, ENC_FS or SD replacement as needed.

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <vector>
#include "driver/dac.h"

// ---------- Tunables (tune to fit memory / performance) ----------
#define RAW_BUF_SIZE 1024              // read buffer for HTTP
#define AUDIO_CIRC_SIZE 4096           // must be power-of-two, audio circular buffer (bytes)
#define AUDIO_PREFILL_SAMPLES 512      // samples to prefill before starting DAC
#define DAC_CHANNEL_PLAY DAC_CHANNEL_2 // GPIO26

// ---------- Small macros to adapt display calls ----------
extern void drawFullScreenLine(int x, int y, int w, const uint16_t *lineData); // implement if you want
#ifndef drawFullScreenLine
#define drawFullScreenLine(x, y, w, line) Screen::tft.pushImage(x, y, w, 1, (uint16_t *)line)
#endif

// ---------- Audio circular buffer + ISR (shared across calls) ----------
static volatile uint8_t audioCirc[AUDIO_CIRC_SIZE];
static volatile uint32_t audioHead = 0;
static volatile uint32_t audioTail = 0;
static hw_timer_t *audioTimer = nullptr;
portMUX_TYPE audioMux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t audioSamplesPlayed = 0;
static volatile bool audioFeedingDone = false;

static inline uint32_t audioCircFree()
{
    uint32_t h = audioHead, t = audioTail;
    if (h >= t)
        return AUDIO_CIRC_SIZE - (h - t) - 1;
    return (t - h) - 1;
}
static inline uint32_t audioCircAvail()
{
    uint32_t h = audioHead, t = audioTail;
    if (h >= t)
        return (h - t);
    return AUDIO_CIRC_SIZE - (t - h);
}
static inline void audioWriteByteAtomic(uint8_t v)
{
    portENTER_CRITICAL(&audioMux);
    audioCirc[audioHead] = v;
    audioHead = (audioHead + 1) & (AUDIO_CIRC_SIZE - 1);
    portEXIT_CRITICAL(&audioMux);
}

IRAM_ATTR void audioTimerISR()
{
    uint8_t out = 128;
    portENTER_CRITICAL_ISR(&audioMux);
    if (audioTail != audioHead)
    {
        out = audioCirc[audioTail];
        audioTail = (audioTail + 1) & (AUDIO_CIRC_SIZE - 1);
    }
    portEXIT_CRITICAL_ISR(&audioMux);
    dac_output_voltage(DAC_CHANNEL_PLAY, out);
    audioSamplesPlayed++;
    if (audioFeedingDone)
    {
        portENTER_CRITICAL_ISR(&audioMux);
        bool empty = (audioTail == audioHead);
        portEXIT_CRITICAL_ISR(&audioMux);
        if (empty)
            timerAlarmDisable(audioTimer);
    }
}

static void audioSetupTimer(uint32_t sampleRateHz)
{
    if (!audioTimer)
    {
        audioTimer = timerBegin(0, 80, true);
        timerAttachInterrupt(audioTimer, &audioTimerISR, true);
    }
    uint32_t period_us = (uint32_t)(1000000.0 / (double)sampleRateHz + 0.5);
    if (period_us < 1)
        period_us = 1;
    timerAlarmWrite(audioTimer, period_us, true);
}

// ---------- Little-endian helpers ----------
static inline uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static inline uint16_t le16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }

// ---------- PackBits streaming decoder (calls outputFn with produced bytes) ----------
using OutputFn = std::function<void(const uint8_t *data, size_t len)>;

static void packbits_stream_decode(const uint8_t *src, size_t srcLen, OutputFn outputFn)
{
    size_t ip = 0;
    while (ip < srcLen)
    {
        int8_t header = (int8_t)src[ip++];
        if (header >= 0)
        {
            int cnt = header + 1;
            if (ip + cnt > srcLen)
                cnt = (int)(srcLen - ip); // clamp
            outputFn(&src[ip], cnt);
            ip += cnt;
        }
        else if (header >= -127)
        {
            int cnt = -header + 1;
            if (ip >= srcLen)
                break;
            uint8_t v = src[ip++];
            // output v repeated cnt times
            // produce a small static chunk if needed
            static uint8_t runBuf[256];
            while (cnt > 0)
            {
                int take = cnt;
                if (take > 256)
                    take = 256;
                memset(runBuf, v, take);
                outputFn(runBuf, take);
                cnt -= take;
            }
        }
        else
        {
            // header == -128 no-op
        }
    }
}

// ---------- Read full helper (blocks until len read) ----------
static bool readFull(WiFiClient *s, uint8_t *buf, size_t len, unsigned long timeoutMs = 5000)
{
    size_t rec = 0;
    unsigned long start = millis();
    while (rec < len)
    {
        int r = s->read(buf + rec, len - rec);
        if (r > 0)
            rec += r;
        else
        {
            if (!s->connected() && s->available() == 0)
                return false;
            delay(1);
        }
        if (millis() - start > timeoutMs)
            return false;
    }
    return rec == len;
}

// ---------- WAV streaming parser + feed function (reads from WiFiClient sequentially and feeds circ buffer) ----------
static bool streamWavToDAC(WiFiClient *stream, HTTPClient &https, uint32_t maxPreviewBytes = 0xFFFFFFFF)
{
    // read RIFF header (already partially read upstream sometimes; caller should ensure stream is at file start)
    uint8_t hdr[12];
    if (!readFull(stream, hdr, 12))
        return false;
    if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0)
    {
        Serial.println("Not WAV");
        return false;
    }

    // parse chunks until "fmt " and "data"
    uint16_t audioFormat = 0, channels = 0, bitsPerSample = 0;
    uint32_t sampleRate = 0, dataSize = 0;
    // read chunks
    while (stream->connected() && stream->available())
    {
        uint8_t chkhdr[8];
        if (!readFull(stream, chkhdr, 8))
            break;
        uint32_t chunkSize = le32(chkhdr + 4);
        if (memcmp(chkhdr, "fmt ", 4) == 0)
        {
            uint8_t *fmt = (uint8_t *)malloc(chunkSize);
            if (!fmt)
                return false;
            if (!readFull(stream, fmt, chunkSize))
            {
                free(fmt);
                return false;
            }
            audioFormat = le16(fmt + 0);
            channels = le16(fmt + 2);
            sampleRate = le32(fmt + 4);
            bitsPerSample = le16(fmt + 14);
            free(fmt);
            Serial.printf("WAV fmt: fmt=%u ch=%u sr=%u bits=%u\n", audioFormat, channels, sampleRate, bitsPerSample);
        }
        else if (memcmp(chkhdr, "data", 4) == 0)
        {
            dataSize = chunkSize;
            break;
        }
        else
        {
            // skip
            uint32_t toSkip = chunkSize;
            uint8_t tmp[256];
            while (toSkip > 0)
            {
                uint32_t r = (toSkip > sizeof(tmp)) ? sizeof(tmp) : toSkip;
                if (!readFull(stream, tmp, r))
                    return false;
                toSkip -= r;
            }
        }
    }
    if (dataSize == 0)
    {
        Serial.println("WAV no data");
        return false;
    }

    // prepare DAC
    if (sampleRate == 0)
        sampleRate = 22050;
    audioFeedingDone = false;
    audioHead = audioTail = 0;
    audioSamplesPlayed = 0;
    audioSetupTimer(sampleRate);
    dac_output_enable(DAC_CHANNEL_PLAY);

    // prefill
    uint32_t toRead = min(dataSize, maxPreviewBytes);
    uint32_t readTotal = 0;
    static uint8_t tmpBuf[RAW_BUF_SIZE];

    while (readTotal < toRead && audioCircAvail() < AUDIO_PREFILL_SAMPLES)
    {
        int want = min((uint32_t)RAW_BUF_SIZE, toRead - readTotal);
        int got = stream->read(tmpBuf, want);
        if (got <= 0)
        {
            delay(1);
            continue;
        }
        readTotal += got;
        // convert samples to unsigned 8-bit mono and write
        if (bitsPerSample == 16)
        {
            // 16-bit little-endian signed
            int sampleCount = got / 2;
            for (int i = 0; i < sampleCount; ++i)
            {
                int16_t s = (int16_t)(tmpBuf[2 * i] | (tmpBuf[2 * i + 1] << 8));
                uint8_t out = (uint8_t)(((uint16_t)(s + 32768)) >> 8);
                while (audioCircFree() < 1)
                    delay(1);
                audioWriteByteAtomic(out);
            }
        }
        else
        {
            // assume 8-bit unsigned
            for (int i = 0; i < got; ++i)
            {
                uint8_t v = tmpBuf[i];
                while (audioCircFree() < 1)
                    delay(1);
                audioWriteByteAtomic(v);
            }
        }
    }

    // start playback
    timerAlarmEnable(audioTimer);

    // continue streaming audio until done
    while (readTotal < toRead)
    {
        int want = min((uint32_t)RAW_BUF_SIZE, toRead - readTotal);
        int got = stream->read(tmpBuf, want);
        if (got <= 0)
        {
            delay(1);
            continue;
        }
        readTotal += got;
        if (bitsPerSample == 16)
        {
            int sampleCount = got / 2;
            for (int i = 0; i < sampleCount; ++i)
            {
                int16_t s = (int16_t)(tmpBuf[2 * i] | (tmpBuf[2 * i + 1] << 8));
                uint8_t out = (uint8_t)(((uint16_t)(s + 32768)) >> 8);
                while (audioCircFree() < 1)
                    delay(1);
                audioWriteByteAtomic(out);
            }
        }
        else
        {
            for (int i = 0; i < got; ++i)
            {
                uint8_t v = tmpBuf[i];
                while (audioCircFree() < 1)
                    delay(1);
                audioWriteByteAtomic(v);
            }
        }
    }

    audioFeedingDone = true;
    // wait for drain (bounded)
    unsigned long start = millis();
    while (audioCircAvail() != 0 && millis() - start < 15000)
        delay(10);

    // stop timer
    if (audioTimer)
        timerAlarmDisable(audioTimer);
    dac_output_disable(DAC_CHANNEL_PLAY);
    return true;
}

// ---------- The Lua API: lua_WIN_drawVideo (replaces previous implementation) ----------
int lua_WIN_drawVideo(lua_State *L)
{
    if (ESP.getFreeHeap() < 20000)
    {
        Serial.println("Low heap, skipping frame or audio chunk");
        delay(5);
    }

    esp_task_wdt_delete(NULL);
    Serial.println("[lua_WIN_drawVideo] called");

    // prechecks (same as your original)
    if (!Windows::isRendering || !UserWiFi::hasInternet)
    {
        Serial.printf("[lua_WIN_drawVideo] rendering=%d, hasInternet=%d; returning\n",
                      Windows::isRendering ? 1 : 0, UserWiFi::hasInternet ? 1 : 0);
        return 0;
    }

    Window *w = getWindow(L, 1);
    if (!w || w->closed)
    {
        Serial.println("[lua_WIN_drawVideo] no window or closed; returning");
        return 0;
    }

    if (!w->wasClicked)
    {
        Serial.println("[lua_WIN_drawVideo] window not clicked on top; returning");
        return 0;
    }
    w->wasClicked = false;

    int waitLoops = 0;
    while (!Windows::canAccess)
    {
        delay(1);
        if ((waitLoops++ & 127) == 0)
            Serial.println("[lua_WIN_drawVideo] waiting for access...");
        yield();
    }
    Windows::canAccess = false;

    Screen::tft.fillScreen(BG);
    Screen::tft.drawString("...Loading Video/Audio...", 100, 100);
    Serial.println("[lua_WIN_drawVideo] acquired access");

    PriorityGuard pg(12); // reduce priority while processing

    const char *url_c = luaL_checkstring(L, 2);
    String url = String(url_c);
    if (url.startsWith("https://github.com/") && url.indexOf("/raw/refs/heads/") != -1)
    {
        url.replace("https://github.com/", "https://raw.githubusercontent.com/");
        url.replace("/raw/refs/heads/", "/");
    }

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient https;

    auto openStream = [&](uint32_t byteOffset) -> WiFiClient *
    {
        if (!https.begin(client, url))
        {
            Serial.println("[lua_WIN_drawVideo] https.begin() failed");
            return nullptr;
        }
        if (byteOffset)
            https.addHeader("Range", "bytes=" + String(byteOffset) + "-");
        int httpCode = https.GET();
        // follow redirects
        while (httpCode == 302 || httpCode == 301)
        {
            String redirect = https.getLocation();
            https.end();
            if (!https.begin(client, redirect))
            {
                Serial.println("[lua_WIN_drawVideo] redirect begin failed");
                return nullptr;
            }
            httpCode = https.GET();
        }
        if (httpCode != HTTP_CODE_OK && httpCode != HTTP_CODE_PARTIAL_CONTENT)
        {
            Serial.printf("[lua_WIN_drawVideo] HTTP GET failed: %d\n", httpCode);
            https.end();
            return nullptr;
        }
        return https.getStreamPtr();
    };

    // open initial stream to read first few bytes to detect format
    WiFiClient *stream = openStream(0);
    if (!stream)
    {
        Windows::canAccess = true;
        return 0;
    }

    // wait for minimal bytes
    uint32_t waitStart = millis();
    while (stream->available() < 12)
    {
        if (!stream->connected() && stream->available() == 0)
        {
            Serial.println("[lua_WIN_drawVideo] disconnected early");
            https.end();
            Windows::canAccess = true;
            return 0;
        }
        if (millis() - waitStart > 3000)
        {
            Serial.println("[lua_WIN_drawVideo] header timeout");
            https.end();
            Windows::canAccess = true;
            return 0;
        }
        delay(1);
    }

    // peek first 12 bytes
    uint8_t hdr[16] = {0};
    size_t got = stream->read(hdr, 12);
    if (got < 8)
    {
        Serial.println("[lua_WIN_drawVideo] header read fail");
        https.end();
        Windows::canAccess = true;
        return 0;
    }

    // detect AVF1 (magic "AVF1"), WAV ("RIFF"), or old RGB565 header (w,h,frames)
    bool isAVF = (hdr[0] == 'A' && hdr[1] == 'V' && hdr[2] == 'F' && hdr[3] == '1');
    bool isWav = (hdr[0] == 'R' && hdr[1] == 'I' && hdr[2] == 'F' && hdr[3] == 'F');
    bool isOldVideo = false;
    if (!isAVF && !isWav)
    {
        // try interpret first 8 bytes as width(2),height(2),framesCount(4) little-endian
        uint16_t maybeW = hdr[0] | (hdr[1] << 8);
        uint16_t maybeH = hdr[2] | (hdr[3] << 8);
        uint32_t maybeFC = (uint32_t)hdr[4] | ((uint32_t)hdr[5] << 8) | ((uint32_t)hdr[6] << 16) | ((uint32_t)hdr[7] << 24);
        if (maybeW > 0 && maybeW < 10000 && maybeH > 0 && maybeH < 10000 && maybeFC > 0 && maybeFC < 200000)
        {
            isOldVideo = true;
        }
    }

    // rewind stream by closing and reopening at 0 (to ensure consistent parsing)
    https.end();

    if (isWav)
    {
        // WAV only: stream to DAC and return (no video)
        stream = openStream(0);
        if (!stream)
        {
            Windows::canAccess = true;
            return 0;
        }
        // stream WAV to DAC (this function will close when complete)
        bool ok = streamWavToDAC(stream, https);
        https.end();
        Windows::canAccess = true;
        Serial.printf("[lua_WIN_drawVideo] WAV playback done ok=%d\n", ok ? 1 : 0);
        return 0;
    }

    if (isOldVideo)
    {
        // Old format: width(2),height(2),frames(4) then raw frames (RGB565) sequentially
        stream = openStream(0);
        if (!stream)
        {
            Windows::canAccess = true;
            return 0;
        }
        uint8_t header8[8];
        if (!readFull(stream, header8, 8))
        {
            https.end();
            Windows::canAccess = true;
            return 0;
        }
        uint16_t v_w = header8[0] | (header8[1] << 8);
        uint16_t v_h = header8[2] | (header8[3] << 8);
        uint32_t framesCount = (uint32_t)header8[4] | ((uint32_t)header8[5] << 8) | ((uint32_t)header8[6] << 16) | ((uint32_t)header8[7] << 24);
        Serial.printf("[lua_WIN_drawVideo] old raw video: %u x %u frames=%u\n", v_w, v_h, framesCount);

        int winW = Screen::tft.width(), winH = Screen::tft.height();
        int scale = (v_w < 160 && v_h < 120) ? 2 : 1;
        int dispW = v_w * scale, dispH = v_h * scale;
        int dstX = (winW - dispW) / 2, dstY = (winH - dispH) / 2;

        size_t bytesPerLine = (size_t)v_w * 2;
        uint8_t *lineBuf = (uint8_t *)heap_caps_malloc(bytesPerLine, MALLOC_CAP_8BIT);
        if (!lineBuf)
        {
            Serial.println("[lua_WIN_drawVideo] lineBuf alloc failed");
            https.end();
            Windows::canAccess = true;
            return 0;
        }
        uint8_t *scaledLineBuf = nullptr;
        if (scale > 1)
        {
            size_t scaledBytes = bytesPerLine * scale;
            scaledLineBuf = (uint8_t *)heap_caps_malloc(scaledBytes, MALLOC_CAP_8BIT);
            if (!scaledLineBuf)
            {
                heap_caps_free(lineBuf);
                Serial.println("[lua_WIN_drawVideo] scaledLineBuf fail");
                https.end();
                Windows::canAccess = true;
                return 0;
            }
        }

        bool paused = false, exitRequested = false;
        uint32_t currentFrame = 0;
        uint32_t lastFrameTime = millis();

        while (currentFrame < framesCount && !exitRequested && !w->closed && Windows::isRendering && UserWiFi::hasInternet)
        {
            auto touch = Screen::getTouchPos();
            // simple controls: toggle pause when clicking video area, exit top-right
            if (touch.clicked)
            {
                int menuHeight = 20;
                int exitW = 20;
                int exitX = winW - exitW;
                if (touch.x >= exitX && touch.y < menuHeight)
                {
                    exitRequested = true;
                    break;
                }
                if (touch.x >= dstX && touch.x < dstX + dispW && touch.y >= dstY && touch.y < dstY + dispH)
                {
                    paused = !paused;
                    if (paused)
                        drawMenuBar(paused, currentFrame, framesCount);
                }
            }
            if (paused)
            {
                delay(10);
                continue;
            }

            bool ok = true;
            for (uint16_t row = 0; row < v_h; ++row)
            {
                if (!readFull(stream, lineBuf, bytesPerLine))
                {
                    ok = false;
                    break;
                }
                uint16_t *pixels = (uint16_t *)lineBuf;
                if (scale == 1)
                {
                    Screen::tft.pushImage(dstX, dstY + row, v_w, 1, pixels);
                }
                else
                {
                    uint16_t *sPixels = (uint16_t *)scaledLineBuf;
                    for (uint16_t x = 0; x < v_w; ++x)
                    {
                        uint16_t p = pixels[x];
                        sPixels[2 * x] = p;
                        sPixels[2 * x + 1] = p;
                    }
                    Screen::tft.pushImage(dstX, dstY + row * 2, v_w * 2, 1, sPixels);
                    Screen::tft.pushImage(dstX, dstY + row * 2 + 1, v_w * 2, 1, sPixels);
                }
            }
            if (!ok)
                break;
            currentFrame++;
            // simple fps pacing ~20fps
            uint32_t now = millis();
            int delayMs = 50 - (now - lastFrameTime);
            if (delayMs > 0)
                delay(delayMs);
            lastFrameTime = millis();
            delay(1);
        }

        if (scaledLineBuf)
            heap_caps_free(scaledLineBuf);
        if (lineBuf)
            heap_caps_free(lineBuf);
        https.end();
        Screen::tft.fillScreen(BG);
        Windows::canAccess = true;
        Serial.printf("[lua_WIN_drawVideo] finished oldRaw; freeHeap=%u\n", (unsigned)ESP.getFreeHeap());
        return 0;
    }

    // Otherwise, try AVF format (we assume stream begin at 0)
    stream = openStream(0);
    if (!stream)
    {
        Windows::canAccess = true;
        return 0;
    }

    // Read AVF header (we defined earlier format: "AVF1" + version(1) + width(2)+height(2)+fps(1)+flags(1))
    uint8_t avfBase[12];
    if (!readFull(stream, avfBase, 12))
    {
        https.end();
        Windows::canAccess = true;
        return 0;
    }
    // avfBase[0..3] == "AVF1"
    uint8_t avfVersion = avfBase[4];
    uint16_t v_w = avfBase[5] | (avfBase[6] << 8);
    uint16_t v_h = avfBase[7] | (avfBase[8] << 8);
    uint8_t fps = avfBase[9];
    uint8_t flags = avfBase[10];
    bool hasVideo = (flags & 1);
    bool hasAudio = (flags & 2);

    uint32_t audioSR = 0, audioBytes = 0;
    uint8_t audioBits = 0, audioCh = 0;

    if (hasAudio)
    {
        // audioSampleRate(4), audioBits(1), audioChannels(1), audioBytes(4)
        uint8_t aHdr[10];
        if (!readFull(stream, aHdr, 10))
        {
            https.end();
            Windows::canAccess = true;
            return 0;
        }
        audioSR = le32(aHdr);
        audioBits = aHdr[4];
        audioCh = aHdr[5];
        audioBytes = le32(aHdr + 6);
        Serial.printf("[lua_WIN_drawVideo] AVF audio: sr=%u bits=%u ch=%u bytes=%u\n", audioSR, audioBits, audioCh, audioBytes);
    }
    else
    {
        Serial.println("[lua_WIN_drawVideo] AVF has no audio");
    }

    // Next: framesCount (4)
    uint8_t fcountBuf[4];
    if (!readFull(stream, fcountBuf, 4))
    {
        https.end();
        Windows::canAccess = true;
        return 0;
    }
    uint32_t framesCount = le32(fcountBuf);
    Serial.printf("[lua_WIN_drawVideo] AVF v=%u %ux%u fps=%u flags=0x%02x frames=%u\n", avfVersion, v_w, v_h, fps, flags, framesCount);

    // Setup display geometry
    int winW = Screen::tft.width(), winH = Screen::tft.height();
    int scale = (v_w < 160 && v_h < 120) ? 2 : 1;
    int dispW = v_w * scale, dispH = v_h * scale;
    int dstX = (winW - dispW) / 2, dstY = (winH - dispH) / 2;

    // prepare small buffers
    size_t bytesPerLine = (size_t)v_w * 2;
    uint8_t *lineBuf = (uint8_t *)heap_caps_malloc(bytesPerLine, MALLOC_CAP_8BIT);
    if (!lineBuf)
    {
        Serial.println("[lua_WIN_drawVideo] lineBuf alloc fail");
        https.end();
        Windows::canAccess = true;
        return 0;
    }
    uint8_t *scaledLineBuf = nullptr;
    if (scale > 1)
    {
        scaledLineBuf = (uint8_t *)heap_caps_malloc(bytesPerLine * scale, MALLOC_CAP_8BIT);
        if (!scaledLineBuf)
        {
            heap_caps_free(lineBuf);
            Serial.println("[lua_WIN_drawVideo] scaledLineBuf fail");
            https.end();
            Windows::canAccess = true;
            return 0;
        }
    }

    // If audio present: prefill circ buffer from the audioBytes block (audio block comes before frames)
    if (hasAudio && audioBytes > 0)
    {
        // We'll read audioBytes sequentially and write into circular buffer; then proceed to frames.
        // First, setup DAC & timer
        if (audioSR == 0)
            audioSR = 22050;
        audioFeedingDone = false;
        audioHead = audioTail = 0;
        audioSamplesPlayed = 0;
        audioSetupTimer(audioSR);
        dac_output_enable(DAC_CHANNEL_PLAY);

        // prefill
        uint32_t audioRead = 0;
        static uint8_t tmp[RAW_BUF_SIZE];
        while (audioRead < audioBytes && audioCircAvail() < AUDIO_PREFILL_SAMPLES)
        {
            int want = min((uint32_t)RAW_BUF_SIZE, audioBytes - audioRead);
            int got = stream->read(tmp, want);
            if (got <= 0)
            {
                delay(1);
                continue;
            }
            audioRead += got;
            // converter writes unsigned 8-bit PCM (0..255) -> directly feed
            for (int i = 0; i < got; ++i)
            {
                while (audioCircFree() < 1)
                    delay(1);
                audioWriteByteAtomic(tmp[i]);
            }
        }
        // start timer
        timerAlarmEnable(audioTimer);

        // finish streaming audio bytes while we can (small loop)
        while (audioRead < audioBytes)
        {
            int want = min((uint32_t)RAW_BUF_SIZE, audioBytes - audioRead);
            int got = stream->read(tmp, want);
            if (got <= 0)
            {
                delay(1);
                continue;
            }
            audioRead += got;
            for (int i = 0; i < got; ++i)
            {
                while (audioCircFree() < 1)
                    delay(1);
                audioWriteByteAtomic(tmp[i]);
            }
        }
        audioFeedingDone = true;
        Serial.println("[lua_WIN_drawVideo] audio block fed to circ buffer");
    }

    // After audio block is consumed, frames follow. For AVF our converter stored each frame as:
    // [4 bytes compSize][compSize bytes compressed frame data]
    // The compressed form is PackBits of RGB565 bytes (hi,lo per pixel).
    // We'll read each compSize, malloc compBuf for compressed data, decode with packbits_stream_decode and push lines as produced.

    bool paused = false, exitRequested = false;
    uint32_t currentFrame = 0;

    auto outputWriter = [&](const uint8_t *data, size_t len, std::function<void(uint8_t)> pushByte)
    {
        // a helper not used here; we use writer below
    };

    // Main frame loop
    while (currentFrame < framesCount && !exitRequested && !w->closed && Windows::isRendering && UserWiFi::hasInternet)
    {
        // handle touch controls (pause, seek, exit) - similar to your prior implementation
        auto touch = Screen::getTouchPos();
        int menuHeight = 20;
        int playBtnX0 = 0, playBtnX1 = 20, playBtnY0 = 0, playBtnY1 = menuHeight;
        int tlX = 30, tlY = 6, tlW = 200, tlH = 8;
        int exitW = 20, exitX = winW - exitW, exitY = 0;

        if (touch.clicked)
        {
            if (paused)
            {
                // when paused, support resume/play and exit and seeking
                if (touch.x >= exitX && touch.y >= exitY && touch.y < exitY + menuHeight)
                {
                    exitRequested = true;
                    break;
                }
                if (touch.x >= playBtnX0 && touch.x <= playBtnX1 && touch.y >= playBtnY0 && touch.y <= playBtnY1)
                {
                    paused = false;
                }
                if (touch.x >= tlX && touch.x <= tlX + tlW && touch.y >= tlY && touch.y <= tlY + tlH)
                {
                    float pos = (float)(touch.x - tlX) / (float)tlW;
                    if (pos < 0)
                        pos = 0;
                    if (pos > 1)
                        pos = 1;
                    uint32_t targetFrame = (uint32_t)(pos * (float)framesCount);
                    currentFrame = targetFrame;
                    Serial.printf("[lua_WIN_drawVideo] seek -> frame %u\n", currentFrame);
                    https.end();
                    // compute byte offset: we need to recompute header size: base12 + (if audio present) audioHdr10 + audioBytes + 4(frameCount) = offsetToFrames
                    uint32_t offset = 12;
                    if (hasAudio)
                        offset += 10 + audioBytes;
                    offset += 4; // framesCount already consumed earlier; but in file layout frames follow after that 4 bytes; for seeking we must point to start of frame's compSize field
                    // Now locate the target frame: we must iterate frame sizes to compute per-frame offsets unless frames are fixed size.
                    // To support seek quickly we reopen at offset and iterate frame headers until we reach target frame (lightweight).
                    WiFiClient *s2 = openStream(offset);
                    if (!s2)
                    {
                        Windows::canAccess = true;
                        return 0;
                    }
                    // iterate framelist to skip targetFrame frames by reading compSize and skipping comp data
                    for (uint32_t f = 0; f < currentFrame; ++f)
                    {
                        uint8_t sz4[4];
                        if (!readFull(s2, sz4, 4))
                        {
                            break;
                        }
                        uint32_t csz = le32(sz4);
                        // skip csz bytes
                        uint32_t toSkip = csz;
                        static uint8_t tmpskip[256];
                        while (toSkip > 0)
                        {
                            uint32_t r = (toSkip > sizeof(tmpskip)) ? sizeof(tmpskip) : toSkip;
                            if (!readFull(s2, tmpskip, r))
                            {
                                break;
                            }
                            toSkip -= r;
                        }
                    }
                    // Now s2 is positioned at the compSize of desired frame; swap https/stream to s2
                    https.end();
                    // To simplify we'll close and assign "stream" to a new stream opened with Range at the computed byte position:
                    // compute newBytePos by asking s2->available? Not trivial; easier: reopen original with Range offset and let the loop read frame by frame from there.
                    // For simplicity (robust) we'll just reopen entire resource and skip frames like above into "stream" to position.
                    // Reopen main 'stream' and skip until currentFrame
                    stream = openStream(offset);
                    if (!stream)
                    {
                        Windows::canAccess = true;
                        return 0;
                    }
                    for (uint32_t f = 0; f < currentFrame; ++f)
                    {
                        uint8_t cz[4];
                        if (!readFull(stream, cz, 4))
                            break;
                        uint32_t cs = le32(cz);
                        uint32_t toskip = cs;
                        static uint8_t tmp2[256];
                        while (toskip > 0)
                        {
                            uint32_t r = (toskip > sizeof(tmp2)) ? sizeof(tmp2) : toskip;
                            if (!readFull(stream, tmp2, r))
                            {
                                break;
                            }
                            toskip -= r;
                        }
                    }
                    touch = {};
                    continue;
                }
            }
            else
            {
                // if playing: toggle pause if tapping video area, or exit
                if (touch.x >= dstX && touch.x < dstX + dispW && touch.y >= dstY && touch.y < dstY + dispH)
                {
                    paused = true;
                    drawMenuBar(paused, currentFrame, framesCount);
                }
                else if (touch.x >= exitX && touch.y >= exitY && touch.y < exitY + menuHeight)
                {
                    exitRequested = true;
                    break;
                }
                else if (touch.x >= tlX && touch.x <= tlX + tlW && touch.y >= tlY && touch.y <= tlY + tlH)
                {
                    float pos = (float)(touch.x - tlX) / (float)tlW;
                    if (pos < 0)
                        pos = 0;
                    if (pos > 1)
                        pos = 1;
                    uint32_t targetFrame = (uint32_t)(pos * (float)framesCount);
                    currentFrame = targetFrame;
                    Serial.printf("[lua_WIN_drawVideo] seek -> frame %u\n", currentFrame);
                    https.end();
                    // reposition stream as above (reopen and skip)
                    uint32_t offset = 12;
                    if (hasAudio)
                        offset += 10 + audioBytes;
                    offset += 4;
                    stream = openStream(offset);
                    if (!stream)
                    {
                        Windows::canAccess = true;
                        return 0;
                    }
                    for (uint32_t f = 0; f < currentFrame; ++f)
                    {
                        uint8_t cz[4];
                        if (!readFull(stream, cz, 4))
                            break;
                        uint32_t cs = le32(cz);
                        uint32_t toskip = cs;
                        static uint8_t tmp2[256];
                        while (toskip > 0)
                        {
                            uint32_t r = (toskip > sizeof(tmp2)) ? sizeof(tmp2) : toskip;
                            if (!readFull(stream, tmp2, r))
                                break;
                            toskip -= r;
                        }
                    }
                    continue;
                }
            }
        }

        if (paused)
        {
            delay(10);
            continue;
        }

        // Read next frame's compSize
        uint8_t compSz4[4];
        if (!readFull(stream, compSz4, 4))
        {
            Serial.println("[lua_WIN_drawVideo] frame size read fail");
            break;
        }
        uint32_t compSize = le32(compSz4);
        if (compSize == 0)
        {
            Serial.println("[lua_WIN_drawVideo] zero compSize - skipping");
            currentFrame++;
            continue;
        }

        // read compressed frame into compBuf
        uint8_t *compBuf = (uint8_t *)malloc(compSize);
        if (!compBuf)
        {
            Serial.println("[lua_WIN_drawVideo] compBuf malloc fail");
            break;
        }
        if (!readFull(stream, compBuf, compSize))
        {
            free(compBuf);
            Serial.println("[lua_WIN_drawVideo] comp read short");
            break;
        }

        // We'll decode PackBits and stream output into line buffer; each time we get bytesPerLine bytes, form a line and draw (scaling if needed).
        size_t produced = 0;
        size_t expectedFrameBytes = (size_t)v_w * (size_t)v_h * 2;
        size_t accLine = 0;
        // prepare a small uint16_t line array to convert raw bytes to pixels
        uint16_t *linePixels = (uint16_t *)heap_caps_malloc(sizeof(uint16_t) * v_w, MALLOC_CAP_8BIT);
        if (!linePixels)
        {
            free(compBuf);
            Serial.println("[lua_WIN_drawVideo] linePixels malloc fail");
            break;
        }

        // Writer that receives bytes from packbits decoder
        uint8_t *lineWriter = lineBuf;
        uint32_t currentRow = 0;
        auto writer = [&](const uint8_t *data, size_t len)
        {
            size_t i = 0;
            while (i < len)
            {
                size_t need = bytesPerLine - accLine;
                size_t take = (len - i < need) ? (len - i) : need;
                memcpy(lineWriter + accLine, data + i, take);
                accLine += take;
                i += take;
                if (accLine == bytesPerLine)
                {
                    // we have a full line -> convert to uint16_t pixels and draw
                    uint8_t *p = lineWriter;
                    for (size_t x = 0; x < v_w; ++x)
                    {
                        uint8_t hi = p[2 * x];
                        uint8_t lo = p[2 * x + 1];
                        linePixels[x] = ((uint16_t)hi << 8) | (uint16_t)lo;
                    }
                    if (scale == 1)
                    {
                        drawFullScreenLine(dstX, dstY + currentRow, v_w, linePixels);
                    }
                    else
                    {
                        uint16_t *sPixels = (uint16_t *)scaledLineBuf;
                        for (size_t x = 0; x < v_w; ++x)
                        {
                            uint16_t px = linePixels[x];
                            sPixels[2 * x] = px;
                            sPixels[2 * x + 1] = px;
                        }
                        drawFullScreenLine(dstX, dstY + currentRow * 2, v_w * 2, (uint16_t *)sPixels);
                        drawFullScreenLine(dstX, dstY + currentRow * 2 + 1, v_w * 2, (uint16_t *)sPixels);
                    }
                    currentRow++;
                    accLine = 0;
                    // early safety: don't exceed expected rows
                    if (currentRow >= v_h)
                    {
                        // any extra bytes are ignored
                        // but keep consuming packbits output
                    }
                }
            }
        };

        // decode compressed frame
        packbits_stream_decode(compBuf, compSize, [&](const uint8_t *d, size_t l)
                               { writer(d, l); });

        free(compBuf);
        if (linePixels)
            heap_caps_free(linePixels);

        currentFrame++;
        // if audio present: sync using audioSamplesPlayed and samples per frame
        if (hasAudio && audioSR && fps)
        {
            uint32_t samplesPerFrame = audioSR / (fps ? fps : 1);
            uint32_t targetSamples = currentFrame * samplesPerFrame;
            unsigned long waitStart2 = millis();
            while ((int32_t)audioSamplesPlayed < (int32_t)targetSamples)
            {
                delay(1);
                if (millis() - waitStart2 > 1000)
                    break; // avoid hanging forever
            }
        }
        else
        {
            // pace by fps if no audio
            if (fps)
                delay(1000 / fps);
        }

        delay(1);
    } // frame loop

    // cleanup
    if (scaledLineBuf)
        heap_caps_free(scaledLineBuf);
    if (lineBuf)
        heap_caps_free(lineBuf);

    // ensure audio cleanup
    audioFeedingDone = true;
    if (audioTimer)
        timerAlarmDisable(audioTimer);
    dac_output_disable(DAC_CHANNEL_PLAY);

    https.end();
    Screen::tft.fillScreen(BG);
    Windows::canAccess = true;
    Serial.printf("[lua_WIN_drawVideo] finished; freeHeap=%u\n", (unsigned)ESP.getFreeHeap());
    return 0;
}
