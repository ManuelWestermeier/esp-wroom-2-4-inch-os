#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "fs/index.hpp"
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

    SD_FS::init();
    UserWiFi::start();
    Screen::init();

    startAnimationMWOS();
    Auth::init();

    LuaApps::initialize();
    startWindowRender();
    executeApplication({"/public/programs/test.lua", "Arg1", "Hi"});
}

void loop()
{
    // debugTaskLog();
    delay(3000);
}
