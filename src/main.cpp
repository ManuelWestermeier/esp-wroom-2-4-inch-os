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

void testSegments()
{
    // Beispielpfad in Klartext
    String plainPathStr = "/apps/demo/config.txt";
    Serial.println("Original path: " + plainPathStr);

    // Zerlegen
    Path plainPath = str2Path(plainPathStr);
    Serial.println("Segments:");
    for (auto &seg : plainPath)
    {
        Serial.println("  " + seg);
    }

    // Jedes Segment verschlüsseln
    Path encPath;
    for (auto &seg : plainPath)
    {
        String enc = encryptSegment(seg);
        encPath.push_back(enc);
        Serial.println("Enc seg: " + enc);

        // Gleich zurücktesten
        String dec;
        if (decryptSegment(enc, dec))
        {
            Serial.println(" -> Dec: " + dec);
        }
        else
        {
            Serial.println(" -> ❌ Decrypt failed!");
        }
    }

    // Ganzen verschlüsselten Pfad als String
    String joinedEnc = path2Str(encPath);
    Serial.println("Joined Enc Path: " + joinedEnc);

    // Ganzen verschlüsselten Pfad wieder entschlüsseln
    Path decPath;
    for (auto &seg : encPath)
    {
        String dec;
        if (decryptSegment(seg, dec))
        {
            decPath.push_back(dec);
        }
        else
        {
            decPath.push_back("[ERR]");
        }
    }

    String decPathStr = path2Str(decPath);
    Serial.println("Decrypted Path: " + decPathStr);

    if (decPathStr == plainPathStr)
    {
        Serial.println("✅ Segment roundtrip erfolgreich!");
    }
    else
    {
        Serial.println("❌ Segment roundtrip fehlgeschlagen!");
    }
}

void setup()
{
    // esp_task_wdt_delete(NULL); // unregister this task
    Serial.begin(115200);
    Serial.println("Booting MW 2.4i OS...\n");
    pinMode(0, INPUT_PULLUP); // Button is active LOW

    testSegments();

    return;
    // Audio::init(60);
    Screen::init(150);
    // readString("what is you age?", "15");

    SD_FS::init();

    // SD_FS::deleteDir("/a1fce4363854ff888cff4b8e7875d600c2682390412a8cf79b37d0b11148b0fa");
    // SD_FS::deleteDir("/7kBKr4ub09sEDviFMC1pUE");
    // SD_FS::deleteDir("/299bc1dc09b2d73f81ca536ea8e4399a4bbfe6264ed6f3ba25a415fb6299e73a");
    // SD_FS::deleteDir("/-DbczHj-B82S9qgW2N_wH8");
    // SD_FS::deleteDir("/09fc96082d34c2dfc1295d92073b5ea1dc8ef8da95f14dfded011ffb96d3e54b");
    // SD_FS::deleteDir("/62c66a7a5dd70c3146618063c344e531e6d4b59e379808443ce962b3abd63c5a");
    // SD_FS::deleteDir("/1b16b1df538ba12dc3f97edbb85caa7050d46c148134290feba80f8236c83db9");

    Auth::login("$", "$");
    SD_FS::lsDirSerial("/");
    // tree();
    // ENC_FS::copyFileFromSPIFFS("/test.lua", {"programs", "a-paint", "entry.lua"});

    // UserWiFi::addPublicWifi("io", "hhhhhh90");
    UserWiFi::start();

    // startAnimationMWOS();

    // Auth::init();
    Serial.println(ENC_FS::writeFileString({"programs", "a-paint", "entry.lua"}, "HELLO WORLD!!"));
    Serial.println(ENC_FS::readFileString({"programs", "a-paint", "entry.lua"}));
    Serial.println("--- LSDIRS ---");
    ENC_FS::lsDirSerial(ENC_FS::str2Path("/"));
    Serial.println("--- LSDIRS PROGRAMS ---");
    ENC_FS::lsDirSerial(ENC_FS::str2Path("/programs"));
    Serial.println("--- LSDIRS END ---");
    // filePicker();
    startWindowRender();
}

void loop()
{
    // debugTaskLog();
    delay(3000);
}
