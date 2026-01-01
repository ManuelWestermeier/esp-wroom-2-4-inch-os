#pragma once

#include <Arduino.h>
#include <cstring>

#include "driver/dac.h"

namespace Audio
{
    constexpr dac_channel_t DAC_CH = DAC_CHANNEL_2; // GPIO26
    constexpr int SAMPLE_RATE = 44100;
    constexpr float BUFFER_DURATION = 0.25f;
    constexpr int BUFFER_SIZE = int(SAMPLE_RATE * BUFFER_DURATION);

    // Internal state
    static uint8_t buffer[BUFFER_SIZE]{};
    static int readIndex = 0;
    static int trackLength = 0;
    static bool playing = false;
    static uint8_t volume = 50;
    static hw_timer_t *timer = nullptr;

    // Timer ISR: output 8-bit DAC sample
    static void IRAM_ATTR onTimer();

    void init(byte vol = 50);

    bool tryToAddTrack(const uint8_t *data, int len);

    void trackLoop();

    void setVolume(uint8_t vol);
    uint8_t getVolume();
    bool isPlaying();
}
