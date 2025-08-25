#pragma once

#include <Arduino.h>
#include "driver/dac.h"
#include <cstring>

namespace Audio
{
    constexpr dac_channel_t DAC_CH = DAC_CHANNEL_2; // GPIO26
    constexpr int SAMPLE_RATE = 44100;
    constexpr float BUFFER_DURATION = 0.2f;
    constexpr int BUFFER_SIZE = int(SAMPLE_RATE * BUFFER_DURATION);

    // Internal state
    static uint8_t buffer[BUFFER_SIZE]{}; // global/static OK
    static int readIndex = 0;
    static int trackLength = 0; // actual bytes to play
    static bool playing = false;
    static uint8_t volume = 255;
    static hw_timer_t *timer = nullptr;

    // Timer callback
    static void IRAM_ATTR onTimer()
    {
        if (readIndex < trackLength)
        {
            uint16_t val = (buffer[readIndex] * volume) / 255;
            dac_output_voltage(DAC_CH, (uint8_t)val);
            readIndex++;
        }
        else
        {
            // stop playback
            dac_output_voltage(DAC_CH, 0);
            playing = false;

            if (timer)
            {
                timerAlarmDisable(timer);
                timerDetachInterrupt(timer);
                timerEnd(timer);
                timer = nullptr;
            }
        }
    }

    // Initialize DAC
    void init()
    {
        dac_output_enable(DAC_CH);
        dac_output_voltage(DAC_CH, 0);
    }

    // Try to add a track to the buffer (safely copy `len` bytes)
    // returns true if accepted (not currently playing)
    bool tryToAddTrack(const uint8_t *data, int len)
    {
        if (playing)
            return false;
        if (len <= 0)
            return false;
        if (len > BUFFER_SIZE)
            len = BUFFER_SIZE;
        memcpy(buffer, data, len);
        trackLength = len;
        return true;
    }

    // Start playback
    void trackLoop()
    {
        if (playing)
            return;

        readIndex = 0;
        playing = true;

        // Configure timer: timer 0, prescaler 80 -> 1 MHz clock
        timer = timerBegin(0, 80, true);
        timerAttachInterrupt(timer, &Audio::onTimer, true);
        timerAlarmWrite(timer, 1000000 / SAMPLE_RATE, true); // period in microseconds
        timerAlarmEnable(timer);
    }

    // Set volume 0-255
    void setVolume(uint8_t vol)
    {
        volume = vol;
    }

    bool isPlaying()
    {
        return playing;
    }
}
