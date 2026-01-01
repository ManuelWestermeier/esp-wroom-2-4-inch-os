#include "index.hpp"

namespace Audio
{
    static void IRAM_ATTR onTimer()
    {
        if (readIndex < trackLength)
        {
            // Centered volume scaling: 128 = silence
            int16_t s = (int16_t)buffer[readIndex++] - 128;
            int16_t scaled = (s * (int32_t)volume) / 255;
            uint8_t out = (uint8_t)(scaled + 128);
            dac_output_voltage(DAC_CH, out);
        }
        else
        {
            dac_output_voltage(DAC_CH, 128); // silence
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

    void init(byte vol)
    {
        volume = vol;
        dac_output_enable(DAC_CH);
        dac_output_voltage(DAC_CH, 128); // silence
    }

    bool tryToAddTrack(const uint8_t *data, int len)
    {
        if (playing || len <= 0)
            return false;
        if (len > BUFFER_SIZE)
            len = BUFFER_SIZE;
        memcpy(buffer, data, len);
        trackLength = len;
        return true;
    }

    void trackLoop()
    {
        if (playing)
            return;
        readIndex = 0;
        playing = true;

        timer = timerBegin(0, 80, true);
        timerAttachInterrupt(timer, &Audio::onTimer, true);
        timerAlarmWrite(timer, 1000000 / SAMPLE_RATE, true);
        timerAlarmEnable(timer);
    }

    void setVolume(uint8_t vol) { volume = vol; }
    uint8_t getVolume() { return volume; }
    bool isPlaying() { return playing; }
}
