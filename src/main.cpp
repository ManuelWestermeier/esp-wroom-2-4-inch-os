#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "fs/index.hpp"
#include "screen/index.hpp"
#include "apps/windows.hpp"
#include "apps/index.hpp"
#include "wifi/index.hpp"

using namespace Windows;

// ---------------------- Setup ----------------------
void setup()
{
    Serial.begin(115200);
    Serial.println("Booting MW 2.4i OS...\n");

    SD_FS::init();
    Screen::init();
    UserWiFi::start();
    LuaApps::initialize();

    executeApplication({"/test.lua", "Arg1", "Hi"});
    delay(300);
    startWindowRender();
}

void loop()
{
    debugTaskLog();
    delay(3000);
}
