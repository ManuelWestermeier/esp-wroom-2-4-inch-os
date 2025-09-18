#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Priority guard sets task priority to 'p' and restores to 2 on destruction
struct PriorityGuard
{
    PriorityGuard(int p)
    {
        prev = uxTaskPriorityGet(NULL);
        vTaskPrioritySet(NULL, p);
    }
    ~PriorityGuard()
    {
        vTaskPrioritySet(NULL, 2);
    }
    int prev;
};