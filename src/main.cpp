#include <TFT_eSPI.h>
#include <SD.h>

TFT_eSPI tft(320, 240);

struct Item
{
    String name;
    bool isDir;
};
Item items[50];
uint8_t itemCount = 0;
String currentPath = "/";

#define LINE_HEIGHT 20

// ---------------- FILE LISTING ----------------
void listFiles(String path)
{
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(0, 0);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);

    itemCount = 0;

    if (path != "/")
    {
        tft.println("../");
        items[itemCount].name = "..";
        items[itemCount].isDir = true;
        itemCount++;
    }

    File dir = SD.open(path);
    if (!dir || !dir.isDirectory())
    {
        tft.println("Error opening dir");
        return;
    }

    File file = dir.openNextFile();
    while (file && itemCount < 50)
    {
        String name = file.name();
        if (file.isDirectory())
        {
            name += "/";
            items[itemCount].isDir = true;
        }
        else
        {
            items[itemCount].isDir = false;
        }
        items[itemCount].name = name;

        tft.println(name);
        itemCount++;

        file = dir.openNextFile();
    }
    dir.close();
}

// ---------------- FILE VIEW ----------------
void viewFile(String path)
{
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(0, 0);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setTextSize(1);

    File f = SD.open(path);
    if (!f)
    {
        tft.println("Failed to open file");
        delay(1000);
        return;
    }

    while (f.available())
    {
        tft.print((char)f.read());
    }
    f.close();

    delay(2000);
}

// ---------------- TOUCH HANDLING ----------------
void handleTouch(uint16_t tx, uint16_t ty)
{
    uint8_t index = ty / LINE_HEIGHT;
    if (index >= itemCount)
        return;

    if (items[index].isDir)
    {
        if (items[index].name == "..")
        {
            // Go up
            int slashPos = currentPath.lastIndexOf('/');
            if (slashPos > 0)
            {
                currentPath = currentPath.substring(0, slashPos);
            }
            else
            {
                currentPath = "/";
            }
        }
        else
        {
            if (currentPath == "/")
                currentPath += items[index].name.substring(0, items[index].name.length() - 1);
            else
                currentPath += "/" + items[index].name.substring(0, items[index].name.length() - 1);
        }
        listFiles(currentPath);
    }
    else
    {
        String filePath;
        if (currentPath == "/")
            filePath = currentPath + items[index].name;
        else
            filePath = currentPath + "/" + items[index].name;
        viewFile(filePath);
        listFiles(currentPath);
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
        tft.println("SD init failed!");
        return;
    }

    listFiles(currentPath);
}

uint16_t tx, ty;
void loop()
{
    if (tft.getTouch(&tx, &ty))
    {
        handleTouch(tx, ty);
        delay(200);
    }
}
