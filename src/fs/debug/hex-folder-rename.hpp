#pragma once

#include "../index.hpp"
#include "../../utils/hex.hpp"

void hexFolderRename(String path = "/public/wifi")
{
    File dir = SD.open(path);
    if (!dir || !dir.isDirectory())
    {
        Serial.println("❌ /public/wifi not found or not a directory");
        return;
    }

    File file = dir.openNextFile();
    while (file)
    {
        String name = file.name(); // full path e.g. "/public/wifi/12.wifi"
        if (!file.isDirectory() && name.endsWith(".wifi"))
        {
            // Extract base filename
            String base = name.substring(name.lastIndexOf("/") + 1); // "12.wifi"
            String name = base.substring(0, base.indexOf("."));      // "12"

            String hexName = toHex(name); // hex string (lowercase)

            String newPath = path + "/" + hexName + ".wifi";

            if (SD_FS::renameFile(file.path(), newPath))
            {
                Serial.printf("✅ Renamed %s -> %s\n", name.c_str(), newPath.c_str());
            }
            else
            {
                Serial.printf("❌ Failed to rename %s\n", name.c_str());
            }
        }
        file = dir.openNextFile();
    }
    dir.close();
}
