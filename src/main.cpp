#include <Arduino.h>

#include "apps/windows.hpp"
#include "apps/index.hpp"

#include "auth/auth.hpp"
#include "sys/initialize.hpp"
#include "sys/monitor.hpp"

using namespace Windows;

using namespace ENC_FS;

void setup()
{
    initializeSetup();

    // UserWiFi::addPublicWifi("io", "hhhhhh90");

    // Auth::init();
    Auth::login("m", "m");

    startWindowRender();
    
    Screen::SPI_Screen::startScreen();
}

void loop()
{

    delay(3000);
    monitor();
}
