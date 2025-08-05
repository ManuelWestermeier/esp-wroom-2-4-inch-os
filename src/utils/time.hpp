#pragma once

#include <time.h>

namespace UserTime
{
    extern int isConfigured; // << Nur Deklaration

    void set(int off = 3600);
    tm get();
}
