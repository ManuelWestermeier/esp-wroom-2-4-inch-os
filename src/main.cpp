// add fs, speedcode, net, audio

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

using namespace ENC_FS;

void setup()
{
    // disable Arduino loop watchdog
    disableCore0WDT();
    disableCore1WDT();

    // esp_task_wdt_delete(NULL); // unregister this task
    Serial.begin(115200);
    Serial.println("Booting MW 2.4i OS...\n");
    pinMode(0, INPUT_PULLUP); // Button is active LOW

    SD_FS::init();
    // tree();
    // SD_FS::lsDirSerial("/");

    // UserWiFi::addPublicWifi("io", "hhhhhh90");
    UserWiFi::start();

    // Audio::init(60);
    Screen::init(120);
    // startAnimationMWOS();

    // Auth::init();
    Auth::login("m", "m");
    // update paint app
    ENC_FS::copyFileFromSPIFFS("/test.lua", {"programs", "a-paint", "entry.lua"});
    ENC_FS::lsDirSerial({"programs"});

    // debug io
    //  readString("what is you age?", "15");
    // Serial.println(filePicker("/"));

    // delete users
    // SD_FS::deleteDir("/a1fce4363854ff888cff4b8e7875d600c2682390412a8cf79b37d0b11148b0fa");
    // SD_FS::deleteDir("/7kBKr4ub09sEDviFMC1pUE");
    // SD_FS::deleteDir("/299bc1dc09b2d73f81ca536ea8e4399a4bbfe6264ed6f3ba25a415fb6299e73a");
    // SD_FS::deleteDir("/-DbczHj-B82S9qgW2N_wH8");
    // SD_FS::deleteDir("/09fc96082d34c2dfc1295d92073b5ea1dc8ef8da95f14dfded011ffb96d3e54b");
    // SD_FS::deleteDir("/62c66a7a5dd70c3146618063c344e531e6d4b59e379808443ce962b3abd63c5a");
    // SD_FS::deleteDir("/1b16b1df538ba12dc3f97edbb85caa7050d46c148134290feba80f8236c83db9");

    startWindowRender();
}

void loop()
{

    // debugTaskLog();
    // Serial.println(ESP.getFreeHeap());
    delay(3000);
    // ArduinoOTA.handle();
}
