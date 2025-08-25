#pragma once

#include <Arduino.h>
#include "driver/dac.h"

namespace Audio
{
#define DAC_CHANNEL DAC_CHANNEL_2 // GPIO26
    void init()
    {
        dac_output_enable(DAC_CHANNEL);
        dac_output_voltage(DAC_CHANNEL, 0);
    }

    void out(byte v = 0)
    {
        dac_output_voltage(DAC_CHANNEL, v);
    }
}