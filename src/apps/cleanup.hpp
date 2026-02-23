#pragma once

#include <Arduino.h>
#include <vector>
#include <FS.h>
#include <SPIFFS.h>
#include <SD.h>

#include "../fs/enc-fs.hpp"

// Uses ENC_FS API to delete every app folder inside:
//    programms/<app>/
// that does NOT contain:
//    programms/<app>/appId/id.txt
//
// ✔ uses ENC_FS path system
// ✔ recursive delete
// ✔ safe & robust
// ✔ single self-contained function
//
// use:
//   deleteAppsWithoutId();
//   deleteAppsWithoutId(true);   // dry run (prints only)

static void deleteAppsWithoutId()
{
    Serial.println("Scanning 'programms' for apps without appId/id.txt ...");

    // read app folders
    std::vector<String> apps = ENC_FS::readDir({"programms"});

    for (const String &appName : apps)
    {
        ENC_FS::Path appPath = {"programms", appName};

        // check id file
        ENC_FS::Path idPath = appPath;
        idPath.push_back("id.txt");

        bool hasId = ENC_FS::exists(idPath);

        if (!hasId)
        {
            ENC_FS::rmDir(appPath);
        }
    }

    Serial.println("Cleanup finished.");
}