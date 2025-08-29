#pragma once

#include <SD.h>
#include <SPI.h>

void listTree(const char *dirname = "/", uint8_t levels = 6)
{
    File root = SD.open(dirname);
    if (!root || !root.isDirectory())
    {
        Serial.println("Failed to open directory");
        return;
    }

    File file = root.openNextFile();
    while (file)
    {
        String fullPath = String(dirname) + "/" + file.name();
        if (file.isDirectory())
        {
            Serial.println("[DIR]  " + fullPath);
            if (levels)
            {
                listTree(fullPath.c_str(), levels - 1);
            }
        }
        else
        {
            Serial.print("  FILE  ");
            Serial.print(fullPath);
            Serial.print("\tSIZE: ");
            Serial.println(file.size());
        }
        file = root.openNextFile();
    }
}

void tree(const char *dirname = "/", uint8_t levels = 6)
{
    Serial.println("---- SD Card Content (tree) ----");
    listTree(dirname, levels);
    Serial.println("---- SD Card Content (tree) end ----");
}