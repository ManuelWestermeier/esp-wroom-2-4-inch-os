#pragma once

#include <WiFi.h>
#include <time.h>

#include "../wifi/index.hpp"

namespace UserTime
{
    extern int isConfigured; // << Nur Deklaration

    void set(int off = 3600);
    tm get();
}
