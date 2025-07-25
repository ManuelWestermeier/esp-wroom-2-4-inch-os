#include <Arduino.h>

#include "FS/index.hpp"
#include "apps/window.hpp"

Window win;

void setup()
{
    Serial.begin(115200);
    delay(500);

    while (!SD_FS::init())
    {
        Serial.println("!NO SD! use an SD card (format=fat32)");
        delay(1000);
    }

    SD_FS::writeFile("/test.txt", "Hallo von ESP32!\n");
    String inhalt = SD_FS::readFile("/test.txt");
    Serial.println("📖 Inhalt:\n" + inhalt);

    SD_FS::writeFile("/demo.txt", "Testinhalt\n");
    SD_FS::getFileInfo("/demo.txt");

    Serial.printf("Größe: %zu Bytes\n", SD_FS::fileSize("/demo.txt"));
    Serial.printf("Letzte Änderung: %lu\n", SD_FS::getModifiedTime("/demo.txt"));

    SD_FS::getUsageSummary();

    // Screen::init();
    // win.init("Hello World Test");
}

void loop()
{
    // win.loop();
}