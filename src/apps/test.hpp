// #include "./apps/index.hpp"

// void setup()
// {
//     Serial.begin(115200);
//     Serial.println("Booting...");

//     LuaApps::initialize(); // Initialisiere Serial + SPIFFS
//     Serial.println("Running Lua app...");
//     // Führt /test.lua im Sandbox-Modus aus
//     int result = LuaApps::runApp("/test.lua", {"Arg1", "Hi"});
//     Serial.printf("Lua App exited with code: %d\n", result);
// }
