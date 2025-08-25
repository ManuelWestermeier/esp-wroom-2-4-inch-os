#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>

#include "audio/index.hpp"

#define WIFI_SSID "io"
#define WIFI_PASS "hhhhhh90"
#define WAV_URL "https://manuelwestermeier.github.io/test.wav"

// chunk constants
constexpr int CHUNK_SAMPLES = 1024;               // max output mono samples per chunk
constexpr int TEMP_BYTES = CHUNK_SAMPLES * 2 * 2; // bytes for reading (worst-case stereo 16-bit: 2 channels * 2 bytes * CHUNK_SAMPLES)

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
            Serial.println("\nWiFi connect timeout");
            return;
        }
    }
    Serial.println("\nWiFi connected");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
}

static uint16_t le16(const uint8_t *b) { return (uint16_t)b[0] | ((uint16_t)b[1] << 8); }
static uint32_t le32(const uint8_t *b) { return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24); }

// Convert 16-bit PCM (little endian) to 8-bit DAC samples (0..255)
// Supports mono or stereo downmix (channels == 1 or 2)
void convertPCM16to8_maybeStereo(uint8_t *dest, const uint8_t *src, size_t frames, int channels)
{
    // frames = number of samples PER CHANNEL to produce (mono output length)
    if (channels == 1)
    {
        for (size_t i = 0; i < frames; ++i)
        {
            int16_t v = (int16_t)((uint16_t)src[i * 2] | ((uint16_t)src[i * 2 + 1] << 8));
            dest[i] = (uint8_t)((v + 32768) >> 8);
        }
    }
    else if (channels == 2)
    {
        // src layout: L0_l L0_h R0_l R0_h L1_l L1_h R1_l R1_h ...
        for (size_t i = 0; i < frames; ++i)
        {
            int16_t l = (int16_t)((uint16_t)src[i * 4] | ((uint16_t)src[i * 4 + 1] << 8));
            int16_t r = (int16_t)((uint16_t)src[i * 4 + 2] | ((uint16_t)src[i * 4 + 3] << 8));
            int32_t mix = (int32_t)l + (int32_t)r;
            mix /= 2;
            dest[i] = (uint8_t)((mix + 32768) >> 8);
        }
    }
    else
    {
        // unsupported channel count -> zero output
        memset(dest, 128, frames);
    }
}

void setup()
{
    Serial.begin(115200);
    Serial.println();
    Serial.println("Starting Audio Stream with WAV parsing...");

    Audio::init();
    Audio::setVolume(20); // try a moderate volume (0..255)
    connectWiFi();

    HTTPClient http;
    http.begin(WAV_URL);
    int httpCode = http.GET();

    if (httpCode != 200)
    {
        Serial.printf("Failed to download WAV file: %d\n", httpCode);
        http.end();
        return;
    }

    WiFiClient *stream = http.getStreamPtr();
    if (!stream)
    {
        Serial.println("No stream pointer");
        http.end();
        return;
    }

    // buffers (static to avoid stack overflow)
    static uint8_t readBuf[TEMP_BYTES];
    static uint8_t outBuf[CHUNK_SAMPLES];

    // --- Parse WAV header and find 'fmt ' and 'data' chunk ---
    // Read first 12 bytes: RIFF + size + WAVE
    uint8_t header12[12];
    if (stream->readBytes((char *)header12, 12) != 12)
    {
        Serial.println("Failed to read RIFF header");
        http.end();
        return;
    }
    if (!(memcmp(header12, "RIFF", 4) == 0 && memcmp(header12 + 8, "WAVE", 4) == 0))
    {
        Serial.println("Not a valid WAV (no RIFF/WAVE)");
        http.end();
        return;
    }

    int16_t audioFormat = 0;
    int16_t numChannels = 0;
    uint32_t sampleRate = 0;
    int16_t bitsPerSample = 0;
    uint32_t dataChunkSize = 0;

    // loop over chunks until 'data' found
    bool foundFmt = false;
    while (stream->available())
    {
        // read chunk header 8 bytes
        uint8_t chunkHdr[8];
        if (stream->readBytes((char *)chunkHdr, 8) != 8)
        {
            Serial.println("Failed reading chunk header");
            http.end();
            return;
        }
        uint32_t chunkSize = le32(chunkHdr + 4);
        // chunk id in chunkHdr[0..3]
        if (memcmp(chunkHdr, "fmt ", 4) == 0)
        {
            // read fmt chunk (chunkSize bytes; fmt is usually 16 or 18 or more)
            if (chunkSize > sizeof(readBuf))
            {
                // allocate in smaller reads
                if (stream->readBytes((char *)readBuf, sizeof(readBuf)) != (int)sizeof(readBuf))
                {
                    Serial.println("Failed reading large fmt chunk (part)");
                    http.end();
                    return;
                }
                // skip remaining bytes
                uint32_t remain = chunkSize - sizeof(readBuf);
                while (remain)
                {
                    int toRead = (remain > sizeof(readBuf)) ? sizeof(readBuf) : remain;
                    stream->readBytes((char *)readBuf, toRead); // skip
                    remain -= toRead;
                }
            }
            else
            {
                if (stream->readBytes((char *)readBuf, chunkSize) != (int)chunkSize)
                {
                    Serial.println("Failed reading fmt chunk");
                    http.end();
                    return;
                }
            }

            if (chunkSize >= 16)
            {
                audioFormat = (int16_t)le16(readBuf + 0);
                numChannels = (int16_t)le16(readBuf + 2);
                sampleRate = le32(readBuf + 4);
                bitsPerSample = (int16_t)le16(readBuf + 14);
                foundFmt = true;
                Serial.printf("fmt: format=%d channels=%d samplerate=%u bits=%d\n", audioFormat, numChannels, sampleRate, bitsPerSample);
            }
            else
            {
                Serial.println("fmt chunk too small");
            }
        }
        else if (memcmp(chunkHdr, "data", 4) == 0)
        {
            dataChunkSize = chunkSize;
            Serial.printf("Found data chunk, size=%u bytes\n", dataChunkSize);
            break; // data chunk follows immediately
        }
        else
        {
            // skip unknown chunk
            // some chunk sizes are odd -> padded
            uint32_t toSkip = chunkSize;
            while (toSkip)
            {
                int step = (toSkip > sizeof(readBuf)) ? sizeof(readBuf) : toSkip;
                if (stream->readBytes((char *)readBuf, step) != step)
                {
                    Serial.println("Failed skipping chunk");
                    http.end();
                    return;
                }
                toSkip -= step;
            }
        }

        // continue loop looking for data chunk
    }

    if (!foundFmt)
    {
        Serial.println("No fmt chunk found");
        http.end();
        return;
    }
    if (dataChunkSize == 0)
    {
        Serial.println("No data chunk found");
        http.end();
        return;
    }

    if (audioFormat != 1)
    {
        Serial.printf("Unsupported WAV format (only PCM=1 supported). format=%d\n", audioFormat);
        http.end();
        return;
    }
    if (bitsPerSample != 16)
    {
        Serial.printf("Unsupported bitsPerSample=%d (only 16 supported)\n", bitsPerSample);
        http.end();
        return;
    }
    if (!(numChannels == 1 || numChannels == 2))
    {
        Serial.printf("Unsupported channel count=%d\n", numChannels);
        http.end();
        return;
    }

    if (sampleRate != Audio::SAMPLE_RATE)
    {
        Serial.printf("Warning: WAV sample rate %u != DAC sample rate %d. Playback speed will be incorrect unless you implement resampling.\n", sampleRate, Audio::SAMPLE_RATE);
    }

    // --- Now read audio data in loop ---
    // dataChunkSize bytes remaining
    uint32_t remaining = dataChunkSize;
    while (remaining > 0 && stream->connected())
    {
        // For stereo: each frame (per channel sample) is 4 bytes (2 bytes L + 2 bytes R).
        // For mono: each frame is 2 bytes.
        uint32_t bytesPerFrame = (uint32_t)numChannels * 2; // 2 bytes per sample (16-bit)
        uint32_t wantedFrames = CHUNK_SAMPLES;
        uint32_t bytesToRead = wantedFrames * bytesPerFrame;
        if (bytesToRead > remaining)
        {
            // last partial chunk -> adjust frames
            bytesToRead = remaining - (remaining % bytesPerFrame);
            wantedFrames = (bytesToRead / bytesPerFrame);
            if (wantedFrames == 0)
                break;
        }
        if (bytesToRead > (uint32_t)sizeof(readBuf))
        {
            bytesToRead = sizeof(readBuf) - (sizeof(readBuf) % bytesPerFrame);
            wantedFrames = bytesToRead / bytesPerFrame;
        }

        int br = stream->readBytes((char *)readBuf, bytesToRead);
        if (br <= 0)
            break;
        remaining -= br;

        // Convert frames -> mono 8-bit
        convertPCM16to8_maybeStereo(outBuf, readBuf, wantedFrames, numChannels);

        // Try to add and play
        // pass byte length = wantedFrames (each outBuf entry = 1 byte)
        while (!Audio::tryToAddTrack(outBuf, (int)wantedFrames))
            delay(1);
        Audio::trackLoop();

        // wait for chunk playback done (simple blocking)
        while (Audio::isPlaying())
            delay(1);
    }

    http.end();
    Serial.println("Stream finished");
}

void loop()
{
    // nothing here
}
