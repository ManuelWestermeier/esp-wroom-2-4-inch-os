#include <Arduino.h>

#include "apps/windows.hpp"
#include "apps/index.hpp"

#include "auth/auth.hpp"
#include "sys/initialize.hpp"
#include "anim/entry.hpp"
#include "sys/monitor.hpp"

using namespace Windows;

using namespace ENC_FS;

void setup()
{
    initializeSetup();
    Serial.println("Booting MW 2.4i OS...\n");

    startAnimationMWOS();
    // UserWiFi::addPublicWifi("io", "hhhhhh90");

    Auth::init();
    // Auth::login("m", "m");

    startWindowRender();
}

void loop()
{

    delay(3000);
    monitor();
}
