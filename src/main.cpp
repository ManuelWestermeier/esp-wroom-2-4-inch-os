#include <TFT_eSPI.h>
#include <SD.h>

TFT_eSPI tft(320, 240);

void listRootFiles()
{
    File root = SD.open("/");
    if (!root)
    {
        Serial.println("Failed to open root directory");
        tft.println("Failed to open root");
        return;
    }
    if (!root.isDirectory())
    {
        Serial.println("Root is not a directory");
        tft.println("Not a directory");
        return;
    }

    tft.fillScreen(TFT_BLACK);
    tft.setCursor(0, 0);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);

    File file = root.openNextFile();
    while (file)
    {
        String name = file.name();
        if (file.isDirectory())
        {
            name += "/";
        }

        // Print to Serial
        Serial.println(name);

        // Print to TFT
        tft.println(name);

        file = root.openNextFile();
    }
}

void setup()
{
    Serial.begin(115200);
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);

    tft.init();
    tft.setRotation(3);

    if (!SD.begin(5))
    {
        Serial.println("SD init failed!");
        tft.println("SD init failed!");
        return;
    }

    listRootFiles();
}

uint16_t x = 0, y = 0;
void loop()
{
    uint8_t t = tft.getTouch(&x, &y);
    if (t)
    {
        tft.fillRect(0, 280, 240, 40, TFT_BLACK);
        tft.setCursor(10, 280);
        tft.println(t);
        tft.setCursor(10, 300);
        tft.println(String(x) + "|" + y);
        delay(20);
    }
}
