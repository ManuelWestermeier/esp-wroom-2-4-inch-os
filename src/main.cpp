#include <Arduino.h>

#include "fs/index.hpp"
#include "sys-apps/file-picker.hpp"
#include "fs/debug/tree.hpp"
#include "fs/debug/hex-folder-rename.hpp"
#include "audio/index.hpp"
#include "screen/index.hpp"
#include "apps/windows.hpp"
#include "apps/index.hpp"
#include "wifi/index.hpp"

#include "anim/entry.hpp"
#include "auth/auth.hpp"
#include "sys/initialize.hpp"
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
