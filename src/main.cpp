#include <Arduino.h>

#include "apps/windows.hpp"
#include "apps/index.hpp"

#include "auth/auth.hpp"
#include "sys/initialize.hpp"
#include "sys/monitor.hpp"

#include "apps/cleanup.hpp"

using namespace Windows;

using namespace ENC_FS;

void setup()
{
    initializeSetup();

    // UserWiFi::addPublicWifi("io", "hhhhhh90");

#ifdef USE_LOGIN_SCREEN
    Auth::init();
#else
    Auth::login("c", "c");
#endif

    startWindowRender();

    // deleteAppsWithoutId();
    // testInstallApps();
    // Screen::SPI_Screen::startScreen();
}

void loop()
{

    delay(3000);
    monitor();
}
