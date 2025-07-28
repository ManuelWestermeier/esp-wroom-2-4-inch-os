#include "network.hpp"

namespace LuaApps::Network {

    bool connectWiFi(const char *ssid, const char *password) {
        WiFi.begin(ssid, password);
        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
            delay(500);
        }
        return WiFi.status() == WL_CONNECTED;
    }

    WiFiServer *createServer(uint16_t port) {
        WiFiServer *server = new WiFiServer(port);
        server->begin();
        return server;
    }

    WiFiClient connectToHost(const char *host, uint16_t port) {
        WiFiClient client;
        client.connect(host, port);
        return client;
    }

}
