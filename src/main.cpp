#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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

using namespace Windows;

void setup()
{
    // esp_task_wdt_delete(NULL); // unregister this task
    Serial.begin(115200);
    Serial.println("Booting MW 2.4i OS...\n");
    pinMode(0, INPUT_PULLUP); // Button is active LOW

    Audio::init(60);
    Screen::init(150);
    readString("what is you age?", "14");

    SD_FS::init();
    // tree();
    SD_FS::copyFileFromSPIFFS("/test.lua", "/public/programs/a-paint/entry.lua");
    Auth::login("m", "m");

    UserWiFi::addPublicWifi("io", "hhhhhh90");
    UserWiFi::start();

    startAnimationMWOS();

    Auth::init();
    // ENC_FS::lsDirSerial(ENC_FS::str2Path(""));
    // filePicker();
    startWindowRender();
}

void loop()
{
    // debugTaskLog();
    delay(3000);
}
