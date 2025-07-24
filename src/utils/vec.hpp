#pragma once

#include <Arduino.h>

struct Vec 
{
    int x;
    int y;
    void print() 
    {
        Serial.println(String("vec<x=")+x+", y="+y+">");
    }
};