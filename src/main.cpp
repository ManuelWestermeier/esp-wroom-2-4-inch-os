#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "fs/index.hpp"
#include "fs/debug/tree.hpp"
#include "audio/index.hpp"
#include "screen/index.hpp"
#include "apps/windows.hpp"
#include "apps/index.hpp"
#include "wifi/index.hpp"

#include "anim/entry.hpp"
#include "auth/auth.hpp"

using namespace Windows;

void setup()
{
    Serial.begin(115200);
    Serial.println("Booting MW 2.4i OS...\n");
    pinMode(0, INPUT_PULLUP); // Button is active LOW

    Audio::init(60);
    Screen::init(150);

    SD_FS::init();
    tree();
    SD_FS::copyFileFromSPIFFS("/test.lua", "/public/programs/a-paint/entry.lua");

    UserWiFi::start();

    startAnimationMWOS();

    Auth::init();

    LuaApps::initialize();
    startWindowRender();
}

void loop()
{
    // debugTaskLog();
    delay(3000);
}
