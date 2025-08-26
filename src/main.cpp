#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "audio/index.hpp"

#define WIFI_SSID "io"
#define WIFI_PASS "hhhhhh90"
#define WAV_URL "https://manuelwestermeier.github.io/test.wav"

// Chunk size for reading (frames per chunk)
constexpr int CHUNK_SAMPLES = 1024;
constexpr int TEMP_BYTES = CHUNK_SAMPLES * 2 * 2; // max stereo 16-bit

static uint16_t le16(const uint8_t *b) { return (uint16_t)b[0] | ((uint16_t)b[1] << 8); }
static uint32_t le32(const uint8_t *b) { return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24); }

void connectWiFi()
{
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("Connecting to WiFi");
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED)
    {
        Serial.print(".");
        delay(500);
        if (millis() - start > 20000)
        {
            Serial.println("\nWiFi timeout");
            return;
        }
    }
    Serial.println("\nWiFi connected. IP: " + WiFi.localIP().toString());
}

// Convert 16-bit PCM -> 8-bit, mono, supports stereo mix
void convertPCM16to8(uint8_t *dest, const uint8_t *src, size_t frames, int channels)
{
    if (channels == 1)
    {
        for (size_t i = 0; i < frames; i++)
        {
            int16_t v = (int16_t)((uint16_t)src[i * 2] | ((uint16_t)src[i * 2 + 1] << 8));
            dest[i] = (uint8_t)((v + 32768) >> 8);
        }
    }
    else if (channels == 2)
    {
        for (size_t i = 0; i < frames; i++)
        {
            int16_t l = (int16_t)((uint16_t)src[i * 4] | ((uint16_t)src[i * 4 + 1] << 8));
            int16_t r = (int16_t)((uint16_t)src[i * 4 + 2] | ((uint16_t)src[i * 4 + 3] << 8));
            int32_t mix = (int32_t)(l + r) / 2;
            dest[i] = (uint8_t)((mix + 32768) >> 8);
        }
    }
    else
    {
        memset(dest, 128, frames);
    }
}

void setup()
{
    Serial.begin(115200);
    Serial.println("\nStarting WAV Stream...");
    Audio::init();
    Audio::setVolume(50);
    connectWiFi();

    HTTPClient http;
    http.begin(WAV_URL);
    if (http.GET() != 200)
    {
        Serial.println("Failed to download WAV");
        http.end();
        return;
    }
    WiFiClient *stream = http.getStreamPtr();
    if (!stream)
    {
        Serial.println("No stream");
        http.end();
        return;
    }

    // Buffers
    static uint8_t readBuf[TEMP_BYTES];
    static uint8_t outBuf[CHUNK_SAMPLES];

    // Parse 12-byte RIFF header
    uint8_t hdr[12];
    stream->readBytes((char *)hdr, 12);
    if (!(memcmp(hdr, "RIFF", 4) == 0 && memcmp(hdr + 8, "WAVE", 4) == 0))
    {
        Serial.println("Not WAV");
        http.end();
        return;
    }

    int16_t audioFormat = 0, numChannels = 0, bitsPerSample = 0;
    uint32_t sampleRate = 0, dataSize = 0;
    bool fmtFound = false;

    while (stream->available())
    {
        uint8_t chunkHdr[8];
        stream->readBytes((char *)chunkHdr, 8);
        uint32_t chunkSize = le32(chunkHdr + 4);
        if (memcmp(chunkHdr, "fmt ", 4) == 0)
        {
            stream->readBytes((char *)readBuf, chunkSize > sizeof(readBuf) ? sizeof(readBuf) : chunkSize);
            audioFormat = le16(readBuf + 0);
            numChannels = le16(readBuf + 2);
            sampleRate = le32(readBuf + 4);
            bitsPerSample = le16(readBuf + 14);
            fmtFound = true;
            Serial.printf("fmt: format=%d channels=%d samplerate=%u bits=%d\n", audioFormat, numChannels, sampleRate, bitsPerSample);
            if (chunkSize > sizeof(readBuf))
                stream->readBytes((char *)readBuf, chunkSize - sizeof(readBuf)); // skip rest
        }
        else if (memcmp(chunkHdr, "data", 4) == 0)
        {
            dataSize = chunkSize;
            break;
        }
        else
        {
            uint32_t skip = chunkSize;
            while (skip)
            {
                int step = (skip > sizeof(readBuf) ? sizeof(readBuf) : skip);
                stream->readBytes((char *)readBuf, step);
                skip -= step;
            }
        }
    }

    if (!fmtFound || dataSize == 0 || audioFormat != 1 || bitsPerSample != 16 || !(numChannels == 1 || numChannels == 2))
    {
        Serial.println("Unsupported WAV");
        http.end();
        return;
    }

    uint32_t remaining = dataSize;
    while (remaining > 0 && stream->connected())
    {
        uint32_t frameBytes = 2 * numChannels;
        uint32_t framesToRead = CHUNK_SAMPLES;
        uint32_t bytesToRead = framesToRead * frameBytes;
        if (bytesToRead > remaining)
        {
            bytesToRead = remaining - (remaining % frameBytes);
            framesToRead = bytesToRead / frameBytes;
        }
        if (bytesToRead > sizeof(readBuf))
        {
            bytesToRead = sizeof(readBuf) - (sizeof(readBuf) % frameBytes);
            framesToRead = bytesToRead / frameBytes;
        }

        int br = stream->readBytes((char *)readBuf, bytesToRead);
        if (br <= 0)
            break;
        remaining -= br;

        convertPCM16to8(outBuf, readBuf, framesToRead, numChannels);

        while (!Audio::tryToAddTrack(outBuf, (int)framesToRead))
            delay(1);

        Audio::trackLoop();
    }

    http.end();
    Serial.println("Stream finished");
}

void loop() {}
