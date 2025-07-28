#include "system.hpp"
#include "app.hpp"

#include <SPIFFS.h>
#include <Arduino.h>

namespace LuaApps
{

    void initialize()
    {
        Serial.begin(115200);
        SPIFFS.begin(true);
        Serial.println("LuaApps initialized.");
    }

    int runApp(const String &path, const std::vector<String> &args)
    {
        App app(path, "/system", args);
        return app.run();
    }

}
