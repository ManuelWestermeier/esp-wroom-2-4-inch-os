#pragma once

#include <Arduino.h>
#include "driver/dac.h"

namespace Audio
{
    constexpr dac_channel_t DAC_CH = DAC_CHANNEL_2; // GPIO26
    constexpr int SAMPLE_RATE = 44100;
    constexpr float BUFFER_DURATION = 0.2f;
    constexpr int BUFFER_SIZE = int(SAMPLE_RATE * BUFFER_DURATION);

    // Internal state
    static uint8_t buffer[BUFFER_SIZE]{};
    static int readIndex = 0;
    static bool playing = false;
    static uint8_t volume = 255;
    static hw_timer_t* timer = nullptr;

    // Timer callback
    static void IRAM_ATTR onTimer()
    {
        if (readIndex < BUFFER_SIZE)
        {
            uint16_t val = (buffer[readIndex] * volume) / 255;
            dac_output_voltage(DAC_CH, val);
            readIndex++;
        }
        else
        {
            dac_output_voltage(DAC_CH, 0);
            playing = false;

            if (timer)
            {
                timerAlarmDisable(timer);
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

    // Try to add a track to the buffer
    bool tryToAddTrack(const uint8_t* data)
    {
        if (playing) return false;
        memcpy(buffer, data, BUFFER_SIZE);
        return true;
    }

    // Start playback
    void trackLoop()
    {
        if (playing) return;

        readIndex = 0;
        playing = true;

        timer = timerBegin(0, 80, true); // 1 MHz
        timerAttachInterrupt(timer, &Audio::onTimer, true); // <- use correct function
        timerAlarmWrite(timer, 1000000 / SAMPLE_RATE, true);
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
