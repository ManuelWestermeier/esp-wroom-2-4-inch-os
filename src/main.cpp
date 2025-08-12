#include <Arduino.h>

#include <TFT_eSPI.h>
#include <SD.h>

#define SD_CS 5

TFT_eSPI tft(320, 240);

void readDir(const String &path, uint8_t levels = 1)
{
    File root = SD.open(path);
    if (!root || !root.isDirectory())
    {
        Serial.printf("❌ Not a dir: %s\n", path.c_str());
        return;
    }

    File file = root.openNextFile();
    while (file)
    {
        Serial.print(file.isDirectory() ? "DIR : " : "FILE: ");
        Serial.print(file.name());
        if (!file.isDirectory())
        {
            Serial.print("\tSIZE: ");
            Serial.println(file.size());
            tft.println(file.size());
        }
        else
        {
            Serial.println();
            if (levels > 0)
                readDir(file.name(), levels - 1);
        }
        file = root.openNextFile();
    }
}

void setup()
{
    pinMode(27, OUTPUT);
    digitalWrite(27, HIGH);

    Serial.begin(115200);

    SD.begin(SD_CS);

    tft.init();
    tft.setRotation(2);
    tft.setCursor(0, 10);
    tft.setTextSize(1);
    tft.fillScreen(0xffff);
    tft.setTextColor(0x0000);
    tft.println("Hello World");

    readDir("/");
}

void loop()
{
}