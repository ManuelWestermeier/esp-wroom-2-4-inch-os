// network.cpp
// Implementation for LuaApps::Network
//
// Note: This implementation targets typical ESP32 Arduino environment.
// It uses WiFiClient, WiFiServer, WiFiUDP and WiFiClientSecure.
// Async HTTP uses std::thread when available (ESP32). If threads are not available,
// httpRequestAsync returns NetError::NOT_IMPLEMENTED.
//
// This file provides a practical, self-contained implementation of the header API
// provided earlier. It's written to be robust and straightforward; further
// improvements (timeouts, non-blocking sockets, finer error handling) are possible.

#include "network.hpp"

#include <WiFiClient.h>
#include <WiFiServer.h>
#include <WiFiUdp.h>
#include <WiFiClientSecure.h>

#include <sstream>
#include <algorithm>
#include <cstring>
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <queue>

    namespace LuaApps::Network
{

    /* --- Internal storage / helpers --- */

    static std::mutex g_lock;

    // Simple handle generation
    static std::atomic<int32_t> g_nextSocketHandle{1};
    static std::atomic<int32_t> g_nextServerHandle{1};
    static std::atomic<int32_t> g_nextUdpHandle{1};
    static std::atomic<int32_t> g_nextTlsHandle{1};

    // Maps for resources
    static std::map<SocketHandle, WiFiClient *> g_tcpClients;
    static std::map<ServerHandle, WiFiServer *> g_tcpServers;
    static std::map<ServerHandle, std::function<void(SocketHandle)>> g_serverCallbacks;
    static std::map<UdpHandle, WiFiUDP *> g_udpHandles;
    static std::map<TlsHandle, WiFiClientSecure *> g_tlsClients;

    // Queue for async HTTP callbacks/results
    struct AsyncHttpTask
    {
        std::function<void(const HttpResponse &)> callback;
        HttpResponse response;
    };
    static std::queue<AsyncHttpTask> g_asyncHttpQueue;
    static std::mutex g_asyncQueueLock;

    // Helper to convert NetError to string
    const char *netErrorToString(NetError e)
    {
        switch (e)
        {
        case NetError::OK:
            return "OK";
        case NetError::UNKNOWN:
            return "UNKNOWN";
        case NetError::TIMEOUT:
            return "TIMEOUT";
        case NetError::BAD_ARG:
            return "BAD_ARG";
        case NetError::NOT_CONNECTED:
            return "NOT_CONNECTED";
        case NetError::ALREADY_CONNECTED:
            return "ALREADY_CONNECTED";
        case NetError::NO_RESOURCES:
            return "NO_RESOURCES";
        case NetError::DNS_FAIL:
            return "DNS_FAIL";
        case NetError::TLS_FAIL:
            return "TLS_FAIL";
        case NetError::IO:
            return "IO";
        case NetError::NOT_IMPLEMENTED:
            return "NOT_IMPLEMENTED";
        default:
            return "UNKNOWN_ERROR";
        }
    }

    /* --- WiFi helpers --- */

    bool hasInternet()
    {
        // If user-provided UserWiFi exists (header included), prefer its hasInternet.
        // But since we can't assume its API at compile-time here, use WiFi.status() heuristic.
        // WiFi.status() == WL_CONNECTED is a reasonable indicator.
        return WiFi.status() == WL_CONNECTED;
    }

    wl_status_t wifiStatus()
    {
        return WiFi.status();
    }

    /* --- DNS --- */

    NetResult dnsResolve(const String &hostname, IPAddress &outIp, uint32_t timeoutMs)
    {
        if (hostname.length() == 0)
            return NetResult(NetError::BAD_ARG, "hostname empty");

        // Use WiFi.hostByName which returns bool immediately but may block internally.
        // We implement a simple loop with timeout by repeatedly calling hostByName until success or timeout.
        auto start = std::chrono::steady_clock::now();
        IPAddress ip;
        while (true)
        {
            if (WiFi.hostByName(hostname.c_str(), ip))
            {
                outIp = ip;
                return NetResult(NetError::OK);
            }
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() >= timeoutMs)
            {
                return NetResult(NetError::DNS_FAIL, "DNS lookup timed out");
            }
            delay(10);
        }
        // unreachable
        return NetResult(NetError::UNKNOWN, "unexpected");
    }

    /* --- TCP client API --- */

    std::pair<SocketHandle, NetResult> tcpConnect(const String &host, uint16_t port, uint32_t timeoutMs)
    {
        if (host.length() == 0 || port == 0)
        {
            return {-1, NetResult(NetError::BAD_ARG, "invalid host/port")};
        }

        WiFiClient *client = new WiFiClient();
        if (!client)
            return {-1, NetResult(NetError::NO_RESOURCES, "alloc failed")};

        // WiFiClient::connect(host, port, timeout) exists on some builds.
        // We'll implement a timed loop using connect() without blocking if necessary.
        bool connected = false;
#if defined(ARDUINO_ARCH_ESP32)
        // On ESP32, WiFiClient::connect(host, port, timeout) is typically available.
        connected = client->connect(host.c_str(), port, timeoutMs);
#else
        // Fallback: attempt connect and wait up to timeoutMs, polling.
        client->connect(host.c_str(), port);
        auto start = std::chrono::steady_clock::now();
        while (!client->connected())
        {
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() >= timeoutMs)
            {
                break;
            }
            delay(5);
        }
        connected = client->connected();
#endif

        if (!connected)
        {
            delete client;
            return {-1, NetResult(NetError::NOT_CONNECTED, "connect failed")};
        }

        SocketHandle h = g_nextSocketHandle.fetch_add(1);
        std::lock_guard<std::mutex> guard(g_lock);
        g_tcpClients[h] = client;
        return {h, NetResult(NetError::OK)};
    }

    ssize_t tcpSend(SocketHandle sock, const uint8_t *data, size_t len, uint32_t timeoutMs)
    {
        if (!data || len == 0)
            return -1;
        std::lock_guard<std::mutex> guard(g_lock);
        auto it = g_tcpClients.find(sock);
        if (it == g_tcpClients.end())
            return -1;
        WiFiClient *client = it->second;
        if (!client->connected())
            return -1;

        // WiFiClient::setTimeout affects read operations. For write we attempt to write directly.
        size_t written = 0;
        auto start = std::chrono::steady_clock::now();
        while (written < len)
        {
            int w = client->write(data + written, len - written);
            if (w > 0)
            {
                written += w;
            }
            else
            {
                // no progress; check timeout
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() >= timeoutMs)
                {
                    return -1;
                }
                delay(1);
            }
        }
        return (ssize_t)written;
    }

    ssize_t tcpRecv(SocketHandle sock, uint8_t *buffer, size_t bufferLen, uint32_t timeoutMs)
    {
        if (!buffer || bufferLen == 0)
            return -1;
        std::lock_guard<std::mutex> guard(g_lock);
        auto it = g_tcpClients.find(sock);
        if (it == g_tcpClients.end())
            return -1;
        WiFiClient *client = it->second;
        if (!client->connected() && client->available() == 0)
            return 0; // EOF

        // Wait up to timeoutMs for data
        uint32_t waited = 0;
        const uint32_t step = 5;
        while (client->available() == 0 && waited < timeoutMs)
        {
            delay(step);
            waited += step;
        }
        if (client->available() == 0)
            return -1; // timeout/no data

        size_t toRead = std::min(bufferLen, (size_t)client->available());
        int r = client->read(buffer, toRead);
        if (r < 0)
            return -1;
        return (ssize_t)r;
    }

    NetResult tcpClose(SocketHandle sock)
    {
        std::lock_guard<std::mutex> guard(g_lock);
        auto it = g_tcpClients.find(sock);
        if (it == g_tcpClients.end())
            return NetResult(NetError::BAD_ARG, "invalid socket");
        WiFiClient *client = it->second;
        if (client)
        {
            client->stop();
            delete client;
        }
        g_tcpClients.erase(it);
        return NetResult(NetError::OK);
    }

    /* --- TCP server API --- */

    std::pair<ServerHandle, NetResult> tcpServerListen(uint16_t port, std::function<void(SocketHandle client)> onClient, uint16_t backlog)
    {
        if (port == 0)
            return {-1, NetResult(NetError::BAD_ARG, "port 0 invalid")};
        WiFiServer *server = new WiFiServer(port);
        if (!server)
            return {-1, NetResult(NetError::NO_RESOURCES, "alloc failed")};
        server->setNoDelay(true);
        server->begin(backlog);

        ServerHandle h = g_nextServerHandle.fetch_add(1);
        {
            std::lock_guard<std::mutex> guard(g_lock);
            g_tcpServers[h] = server;
            g_serverCallbacks[h] = onClient;
        }
        return {h, NetResult(NetError::OK)};
    }

    NetResult tcpServerStop(ServerHandle server)
    {
        std::lock_guard<std::mutex> guard(g_lock);
        auto it = g_tcpServers.find(server);
        if (it == g_tcpServers.end())
            return NetResult(NetError::BAD_ARG, "invalid server");
        WiFiServer *sv = it->second;
        if (sv)
        {
            sv->close();
            delete sv;
        }
        g_tcpServers.erase(it);
        g_serverCallbacks.erase(server);
        return NetResult(NetError::OK);
    }

    /* --- UDP API --- */

    std::pair<UdpHandle, NetResult> udpOpen(uint16_t localPort)
    {
        WiFiUDP *u = new WiFiUDP();
        if (!u)
            return {-1, NetResult(NetError::NO_RESOURCES, "alloc failed")};
        if (localPort != 0)
        {
            if (!u->begin(localPort))
            {
                delete u;
                return {-1, NetResult(NetError::IO, "bind failed")};
            }
        }
        else
        {
            // begin without port allocates when sending
            u->begin(0);
        }
        UdpHandle h = g_nextUdpHandle.fetch_add(1);
        std::lock_guard<std::mutex> guard(g_lock);
        g_udpHandles[h] = u;
        return {h, NetResult(NetError::OK)};
    }

    ssize_t udpSendTo(UdpHandle h, const String &hostOrIp, uint16_t port, const uint8_t *data, size_t len)
    {
        if (!data || len == 0)
            return -1;
        std::lock_guard<std::mutex> guard(g_lock);
        auto it = g_udpHandles.find(h);
        if (it == g_udpHandles.end())
            return -1;
        WiFiUDP *u = it->second;
        if (!u)
            return -1;
        // beginPacket + write + endPacket
        if (!u->beginPacket(hostOrIp.c_str(), port))
        {
            return -1;
        }
        size_t written = u->write(data, len);
        if (written != len)
        {
            // still call endPacket
            u->endPacket();
            return (ssize_t)written;
        }
        int res = u->endPacket();
        if (res == 1)
            return (ssize_t)written;
        return -1;
    }

    ssize_t udpReceiveFrom(UdpHandle h, uint8_t *buffer, size_t bufferLen, String &outFrom, uint16_t &outPort, uint32_t timeoutMs)
    {
        if (!buffer || bufferLen == 0)
            return -1;
        std::lock_guard<std::mutex> guard(g_lock);
        auto it = g_udpHandles.find(h);
        if (it == g_udpHandles.end())
            return -1;
        WiFiUDP *u = it->second;
        if (!u)
            return -1;

        uint32_t waited = 0;
        const uint32_t step = 5;
        while (u->parsePacket() == 0 && waited < timeoutMs)
        {
            delay(step);
            waited += step;
        }
        int pktSize = u->parsePacket();
        if (pktSize <= 0)
            return -1;
        int toRead = std::min((int)bufferLen, pktSize);
        int r = u->read(buffer, toRead);
        IPAddress remoteIp = u->remoteIP();
        outFrom = remoteIp.toString();
        outPort = u->remotePort();
        if (r < 0)
            return -1;
        return (ssize_t)r;
    }

    NetResult udpClose(UdpHandle h)
    {
        std::lock_guard<std::mutex> guard(g_lock);
        auto it = g_udpHandles.find(h);
        if (it == g_udpHandles.end())
            return NetResult(NetError::BAD_ARG, "invalid udp handle");
        WiFiUDP *u = it->second;
        if (u)
        {
            u->stop();
            delete u;
        }
        g_udpHandles.erase(it);
        return NetResult(NetError::OK);
    }

    /* --- TLS / HTTPS helpers --- */

    std::pair<TlsHandle, NetResult> tlsConnect(const String &host, uint16_t port, uint32_t timeoutMs)
    {
        if (host.length() == 0)
            return {-1, NetResult(NetError::BAD_ARG, "host empty")};
        WiFiClientSecure *c = new WiFiClientSecure();
        if (!c)
            return {-1, NetResult(NetError::NO_RESOURCES, "alloc failed")};
        // On many ESP32 builds, setInsecure or setCACert can be used. We'll default to insecure to avoid certificate complexity.
        c->setInsecure();

#if defined(ARDUINO_ARCH_ESP32)
        bool ok = c->connect(host.c_str(), port, timeoutMs);
#else
        c->connect(host.c_str(), port);
        auto start = std::chrono::steady_clock::now();
        bool ok = false;
        while (!c->connected())
        {
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() >= timeoutMs)
                break;
            delay(5);
        }
        ok = c->connected();
#endif

        if (!ok)
        {
            delete c;
            return {-1, NetResult(NetError::TLS_FAIL, "tls connect failed")};
        }

        TlsHandle h = g_nextTlsHandle.fetch_add(1);
        std::lock_guard<std::mutex> guard(g_lock);
        g_tlsClients[h] = c;
        return {h, NetResult(NetError::OK)};
    }

    ssize_t tlsSend(TlsHandle t, const uint8_t *data, size_t len, uint32_t timeoutMs)
    {
        if (!data || len == 0)
            return -1;
        std::lock_guard<std::mutex> guard(g_lock);
        auto it = g_tlsClients.find(t);
        if (it == g_tlsClients.end())
            return -1;
        WiFiClientSecure *c = it->second;
        if (!c->connected())
            return -1;
        size_t written = 0;
        auto start = std::chrono::steady_clock::now();
        while (written < len)
        {
            int w = c->write(data + written, len - written);
            if (w > 0)
            {
                written += w;
            }
            else
            {
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() >= timeoutMs)
                {
                    return -1;
                }
                delay(1);
            }
        }
        return (ssize_t)written;
    }

    ssize_t tlsRecv(TlsHandle t, uint8_t *buffer, size_t bufferLen, uint32_t timeoutMs)
    {
        if (!buffer || bufferLen == 0)
            return -1;
        std::lock_guard<std::mutex> guard(g_lock);
        auto it = g_tlsClients.find(t);
        if (it == g_tlsClients.end())
            return -1;
        WiFiClientSecure *c = it->second;
        if (!c->connected() && c->available() == 0)
            return 0;

        uint32_t waited = 0;
        const uint32_t step = 5;
        while (c->available() == 0 && waited < timeoutMs)
        {
            delay(step);
            waited += step;
        }
        if (c->available() == 0)
            return -1;
        size_t toRead = std::min(bufferLen, (size_t)c->available());
        int r = c->read(buffer, toRead);
        if (r < 0)
            return -1;
        return (ssize_t)r;
    }

    NetResult tlsClose(TlsHandle t)
    {
        std::lock_guard<std::mutex> guard(g_lock);
        auto it = g_tlsClients.find(t);
        if (it == g_tlsClients.end())
            return NetResult(NetError::BAD_ARG, "invalid tls handle");
        WiFiClientSecure *c = it->second;
        if (c)
        {
            c->stop();
            delete c;
        }
        g_tlsClients.erase(it);
        return NetResult(NetError::OK);
    }

    /* --- HTTP synchronous request --- */

    // Very small HTTP request builder + response parser
    static HttpResponse performHttpRequestInternal(const String &method,
                                                   const String &host,
                                                   const String &path,
                                                   const std::map<String, String> &headers,
                                                   const String &body,
                                                   uint16_t port,
                                                   bool useTls,
                                                   uint32_t timeoutMs)
    {
        HttpResponse res;
        res.result = NetResult(NetError::UNKNOWN, "not executed");

        if (method.length() == 0 || host.length() == 0)
        {
            res.result = NetResult(NetError::BAD_ARG, "method/host empty");
            return res;
        }
        String requestPath = path.length() ? path : "/";

        uint16_t actualPort = port;
        if (actualPort == 0)
            actualPort = useTls ? 443 : 80;

        // connect
        bool connected = false;
        WiFiClient *rawClient = nullptr;
        WiFiClientSecure *secureClient = nullptr;
        if (useTls)
        {
            secureClient = new WiFiClientSecure();
            if (!secureClient)
            {
                res.result = NetResult(NetError::NO_RESOURCES, "alloc tls client failed");
                return res;
            }
            secureClient->setInsecure();
#if defined(ARDUINO_ARCH_ESP32)
            connected = secureClient->connect(host.c_str(), actualPort, timeoutMs);
#else
            secureClient->connect(host.c_str(), actualPort);
            auto start = std::chrono::steady_clock::now();
            while (!secureClient->connected())
            {
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() >= timeoutMs)
                    break;
                delay(5);
            }
            connected = secureClient->connected();
#endif
        }
        else
        {
            rawClient = new WiFiClient();
            if (!rawClient)
            {
                res.result = NetResult(NetError::NO_RESOURCES, "alloc client failed");
                return res;
            }
#if defined(ARDUINO_ARCH_ESP32)
            connected = rawClient->connect(host.c_str(), actualPort, timeoutMs);
#else
            rawClient->connect(host.c_str(), actualPort);
            auto start = std::chrono::steady_clock::now();
            while (!rawClient->connected())
            {
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() >= timeoutMs)
                    break;
                delay(5);
            }
            connected = rawClient->connected();
#endif
        }

        if (!connected)
        {
            if (secureClient)
            {
                delete secureClient;
            }
            if (rawClient)
            {
                delete rawClient;
            }
            res.result = NetResult(NetError::NOT_CONNECTED, "connect failed");
            return res;
        }

        // build request lines
        std::ostringstream ss;
        ss << (const char *)method.c_str() << " " << (const char *)requestPath.c_str() << " HTTP/1.1\r\n";
        ss << "Host: " << (const char *)host.c_str() << "\r\n";
        ss << "User-Agent: LuaApps/1.0\r\n";
        if (body.length() > 0)
        {
            ss << "Content-Length: " << body.length() << "\r\n";
        }
        // add headers
        for (const auto &h : headers)
        {
            ss << (const char *)h.first.c_str() << ": " << (const char *)h.second.c_str() << "\r\n";
        }
        ss << "Connection: close\r\n";
        ss << "\r\n";
        if (body.length() > 0)
            ss << (const char *)body.c_str();

        String reqStr = ss.str().c_str();

        WiFiClient *rw = nullptr;
        if (useTls)
        {
            rw = secureClient;
        }
        else
        {
            rw = rawClient;
        }

        // write request
        size_t total = reqStr.length();
        size_t written = 0;
        uint32_t startMs = millis();
        while (written < total)
        {
            int w = rw->write((const uint8_t *)reqStr.c_str() + written, total - written);
            if (w > 0)
                written += w;
            else
            {
                if (millis() - startMs > timeoutMs)
                {
                    // timeout
                    rw->stop();
                    if (secureClient)
                        delete secureClient;
                    if (rawClient)
                        delete rawClient;
                    res.result = NetResult(NetError::TIMEOUT, "write timeout");
                    return res;
                }
                delay(1);
            }
        }

        // read status line
        // wait for data up to timeout
        uint32_t waited = 0;
        const uint32_t step = 5;
        while (!rw->available() && waited < timeoutMs)
        {
            delay(step);
            waited += step;
        }
        if (!rw->available())
        {
            rw->stop();
            if (secureClient)
                delete secureClient;
            if (rawClient)
                delete rawClient;
            res.result = NetResult(NetError::TIMEOUT, "no response");
            return res;
        }

        // read header lines
        String statusLine = rw->readStringUntil('\n'); // includes \r often
        statusLine.trim();
        // parse status code
        int status = 0;
        {
            // Example: HTTP/1.1 200 OK
            int pos1 = statusLine.indexOf(' ');
            if (pos1 > 0)
            {
                int pos2 = statusLine.indexOf(' ', pos1 + 1);
                String codeStr;
                if (pos2 > pos1)
                    codeStr = statusLine.substring(pos1 + 1, pos2);
                else
                    codeStr = statusLine.substring(pos1 + 1);
                status = codeStr.toInt();
            }
        }
        res.status_code = status;

        // Headers
        std::map<String, String> respHeaders;
        while (true)
        {
            String line = rw->readStringUntil('\n');
            line.trim();
            if (line.length() == 0)
                break;
            int colon = line.indexOf(':');
            if (colon > 0)
            {
                String k = line.substring(0, colon);
                String v = line.substring(colon + 1);
                v.trim();
                respHeaders[k] = v;
            }
        }
        res.headers = respHeaders;

        // Body: if Content-Length present, read exact amount; otherwise read until connection closed.
        size_t contentLength = 0;
        bool hasContentLength = false;
        auto it = respHeaders.find("Content-Length");
        if (it != respHeaders.end())
        {
            contentLength = (size_t)it->second.toInt();
            hasContentLength = true;
        }

        String bodyStr;
        if (hasContentLength)
        {
            size_t remaining = contentLength;
            while (remaining > 0)
            {
                if (rw->available())
                {
                    int toread = std::min((size_t)rw->available(), remaining);
                    char chunk[256];
                    int r = rw->read((uint8_t *)chunk, toread);
                    if (r > 0)
                    {
                        bodyStr += String(chunk, r);
                        remaining -= r;
                    }
                    else
                    {
                        break;
                    }
                }
                else
                {
                    // wait a bit
                    delay(5);
                }
            }
        }
        else
        {
            // read until EOF/connection close or small timeout
            uint32_t idle = 0;
            while (true)
            {
                while (rw->available())
                {
                    char chunk[256];
                    int r = rw->read((uint8_t *)chunk, sizeof(chunk));
                    if (r > 0)
                        bodyStr += String(chunk, r);
                }
                if (!rw->connected())
                    break;
                delay(5);
                idle += 5;
                if (idle > timeoutMs)
                    break;
            }
        }
        res.body = bodyStr;
        res.result = NetResult(NetError::OK);
        rw->stop();
        if (secureClient)
            delete secureClient;
        if (rawClient)
            delete rawClient;
        return res;
    }

    HttpResponse httpRequest(const String &method,
                             const String &host,
                             const String &path,
                             const std::map<String, String> &headers,
                             const String &body,
                             uint16_t port,
                             bool useTls,
                             uint32_t timeoutMs)
    {
        return performHttpRequestInternal(method, host, path, headers, body, port, useTls, timeoutMs);
    }

    NetResult httpRequestAsync(const String &method,
                               const String &host,
                               const String &path,
                               std::function<void(const HttpResponse &)> callback,
                               const std::map<String, String> &headers,
                               const String &body,
                               uint16_t port,
                               bool useTls,
                               uint32_t timeoutMs)
    {
#if defined(ARDUINO_ARCH_ESP32) || defined(__cpp_lib_thread)
        // Launch worker thread to perform blocking request, then push result to queue for main-loop callback.
        try
        {
            std::thread worker([method, host, path, headers, body, port, useTls, timeoutMs, callback]()
                               {
            HttpResponse r = performHttpRequestInternal(method, host, path, headers, body, port, useTls, timeoutMs);
            // push to queue
            {
                std::lock_guard<std::mutex> qguard(g_asyncQueueLock);
                g_asyncHttpQueue.push(AsyncHttpTask{callback, r});
            } });
            worker.detach();
            return NetResult(NetError::OK);
        }
        catch (...)
        {
            return NetResult(NetError::NO_RESOURCES, "thread creation failed");
        }
#else
        (void)method;
        (void)host;
        (void)path;
        (void)callback;
        (void)headers;
        (void)body;
        (void)port;
        (void)useTls;
        (void)timeoutMs;
        return NetResult(NetError::NOT_IMPLEMENTED, "threads not available");
#endif
    }

    /* --- Lua binding helpers --- */

    // push headers map as Lua table
    void push_headers_table(lua_State * L, const std::map<String, String> &hdrs)
    {
        lua_newtable(L);
        for (const auto &h : hdrs)
        {
            lua_pushlstring(L, h.first.c_str(), h.first.length());
            lua_pushlstring(L, h.second.c_str(), h.second.length());
            lua_settable(L, -3);
        }
    }

    void push_http_response(lua_State * L, const HttpResponse &r)
    {
        lua_newtable(L);
        // status
        lua_pushstring(L, "status");
        lua_pushinteger(L, r.status_code);
        lua_settable(L, -3);
        // body
        lua_pushstring(L, "body");
        lua_pushlstring(L, r.body.c_str(), r.body.length());
        lua_settable(L, -3);
        // headers
        lua_pushstring(L, "headers");
        push_headers_table(L, r.headers);
        lua_settable(L, -3);
        // ok
        lua_pushstring(L, "ok");
        lua_pushboolean(L, r.result.ok());
        lua_settable(L, -3);
        // err_message
        lua_pushstring(L, "err_message");
        lua_pushlstring(L, r.result.message.c_str(), r.result.message.length());
        lua_settable(L, -3);
    }

    // Helper: check and convert Lua table to http options
    NetResult lua_check_http_options(lua_State * L, int index,
                                     String &outMethod, String &outHost, String &outPath,
                                     std::map<String, String> &outHeaders, String &outBody,
                                     uint16_t &outPort, bool &outUseTls, uint32_t &outTimeoutMs)
    {
        if (!lua_istable(L, index))
            return NetResult(NetError::BAD_ARG, "options must be table");
        // method
        lua_getfield(L, index, "method");
        if (lua_isstring(L, -1))
            outMethod = lua_tostring(L, -1);
        lua_pop(L, 1);
        // host
        lua_getfield(L, index, "host");
        if (lua_isstring(L, -1))
            outHost = lua_tostring(L, -1);
        lua_pop(L, 1);
        // path
        lua_getfield(L, index, "path");
        if (lua_isstring(L, -1))
            outPath = lua_tostring(L, -1);
        lua_pop(L, 1);
        // headers
        lua_getfield(L, index, "headers");
        if (lua_istable(L, -1))
        {
            lua_pushnil(L);
            while (lua_next(L, -2) != 0)
            {
                // key at -2, value at -1
                if (lua_isstring(L, -2) && lua_isstring(L, -1))
                {
                    outHeaders[String(lua_tostring(L, -2))] = String(lua_tostring(L, -1));
                }
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
        // body
        lua_getfield(L, index, "body");
        if (lua_isstring(L, -1))
            outBody = lua_tostring(L, -1);
        lua_pop(L, 1);
        // port
        lua_getfield(L, index, "port");
        if (lua_isnumber(L, -1))
            outPort = (uint16_t)lua_tointeger(L, -1);
        lua_pop(L, 1);
        // tls
        lua_getfield(L, index, "tls");
        if (lua_isboolean(L, -1))
            outUseTls = lua_toboolean(L, -1);
        lua_pop(L, 1);
        // timeout_ms
        lua_getfield(L, index, "timeout_ms");
        if (lua_isnumber(L, -1))
            outTimeoutMs = (uint32_t)lua_tointeger(L, -1);
        lua_pop(L, 1);

        if (outMethod.length() == 0)
            return NetResult(NetError::BAD_ARG, "method missing");
        if (outHost.length() == 0)
            return NetResult(NetError::BAD_ARG, "host missing");
        return NetResult(NetError::OK);
    }

    /* --- Polling / event pumping --- */

    void poll_network_events()
    {
        // 1) Check servers for new clients and accept them
        std::vector<std::pair<ServerHandle, WiFiClient *>> accepted;
        {
            std::lock_guard<std::mutex> guard(g_lock);
            for (auto &p : g_tcpServers)
            {
                ServerHandle sh = p.first;
                WiFiServer *sv = p.second;
                if (!sv)
                    continue;
                WiFiClient client = sv->available();
                if (client)
                {
                    // Allocate new WiFiClient pointer and transfer
                    WiFiClient *cptr = new WiFiClient(client);
                    SocketHandle newSock = g_nextSocketHandle.fetch_add(1);
                    g_tcpClients[newSock] = cptr;
                    // get callback
                    auto itcb = g_serverCallbacks.find(sh);
                    if (itcb != g_serverCallbacks.end())
                    {
                        // Call callback outside lock to avoid deadlocks
                        std::function<void(SocketHandle)> cb = itcb->second;
                        // unlock before calling
                        // we'll call it after collecting accepted sockets
                        accepted.emplace_back(sh, cptr);
                        // We also want to call cb with newSock; store mapping in a temp vector
                        // We'll call below
                        (void)cb;
                    }
                    else
                    {
                        // no callback; leave client available in map
                    }
                }
            }
        }
        // Now call callbacks for accepted connections, mapping cptr -> socket handle
        // To map pointer to handle:
        {
            std::lock_guard<std::mutex> guard(g_lock);
            for (auto &entry : accepted)
            {
                WiFiClient *cptr = entry.second;
                // find handle
                SocketHandle found = -1;
                for (auto &kp : g_tcpClients)
                {
                    if (kp.second == cptr)
                    {
                        found = kp.first;
                        break;
                    }
                }
                if (found == -1)
                    continue;
                // find server callback
                ServerHandle sh = entry.first;
                auto itcb = g_serverCallbacks.find(sh);
                if (itcb != g_serverCallbacks.end())
                {
                    // call callback (careful: user callback may assume main thread)
                    try
                    {
                        itcb->second(found);
                    }
                    catch (...)
                    {
                        // swallow exceptions to avoid crashing
                    }
                }
            }
        }

        // 2) Process async HTTP queue: invoke callbacks on main thread
        std::vector<AsyncHttpTask> tasks;
        {
            std::lock_guard<std::mutex> qguard(g_asyncQueueLock);
            while (!g_asyncHttpQueue.empty())
            {
                tasks.push_back(g_asyncHttpQueue.front());
                g_asyncHttpQueue.pop();
            }
        }
        for (auto &t : tasks)
        {
            try
            {
                if (t.callback)
                    t.callback(t.response);
            }
            catch (...)
            {
                // swallow
            }
        }
    }

    /* --- UDP/TCP/TLS lifetime cleanup on system end (optional) --- */

    static void cleanup_all_resources()
    {
        std::lock_guard<std::mutex> guard(g_lock);
        for (auto &p : g_tcpClients)
        {
            if (p.second)
            {
                p.second->stop();
                delete p.second;
            }
        }
        g_tcpClients.clear();

        for (auto &p : g_tcpServers)
        {
            if (p.second)
            {
                p.second->close();
                delete p.second;
            }
        }
        g_tcpServers.clear();
        g_serverCallbacks.clear();

        for (auto &p : g_udpHandles)
        {
            if (p.second)
            {
                p.second->stop();
                delete p.second;
            }
        }
        g_udpHandles.clear();

        for (auto &p : g_tlsClients)
        {
            if (p.second)
            {
                p.second->stop();
                delete p.second;
            }
        }
        g_tlsClients.clear();
    }

    /* --- Lua userdata wrappers for sockets/udp/tls --- */

    /*
      We'll create simple userdata objects that store an integer handle (SocketHandle/UdpHandle/TlsHandle)
      and a metatable with methods send, recv, close.

      For brevity the userdata layout will be:
        struct LuaHandle { int32_t handle; int type; }
      where type discriminates between types.
    */

    enum LuaHandleType
    {
        LH_TCP = 1,
        LH_UDP = 2,
        LH_TLS = 3
    };

    struct LuaHandle
    {
        int32_t handle;
        int32_t type;
    };

    // Helper to check and fetch LuaHandle pointer
    static LuaHandle *check_luahandle(lua_State * L, int idx, int expectedType)
    {
        void *ud = luaL_checkudata(L, idx, "net.handle");
        if (!ud)
            return nullptr;
        LuaHandle *lh = (LuaHandle *)ud;
        if (expectedType != 0 && lh->type != expectedType)
        {
            luaL_error(L, "invalid handle type");
            return nullptr;
        }
        return lh;
    }

    // tcp_connect(host, port, timeout_ms)
    static int l_tcp_connect(lua_State * L)
    {
        const char *host = luaL_checkstring(L, 1);
        int port = (int)luaL_checkinteger(L, 2);
        uint32_t timeout = NETWORK_DEFAULT_TIMEOUT_MS;
        if (lua_gettop(L) >= 3 && lua_isnumber(L, 3))
            timeout = (uint32_t)lua_tointeger(L, 3);

        auto pr = tcpConnect(String(host), (uint16_t)port, timeout);
        if (!pr.second.ok())
        {
            lua_pushnil(L);
            lua_pushstring(L, pr.second.message.c_str());
            return 2;
        }
        SocketHandle h = pr.first;
        // create userdata
        LuaHandle *lh = (LuaHandle *)lua_newuserdata(L, sizeof(LuaHandle));
        lh->handle = h;
        lh->type = LH_TCP;
        luaL_getmetatable(L, "net.tcp");
        lua_setmetatable(L, -2);
        return 1;
    }

    // tcp_send(sock, data)
    static int l_tcp_send(lua_State * L)
    {
        LuaHandle *lh = check_luahandle(L, 1, LH_TCP);
        size_t len = 0;
        const char *data = nullptr;
        if (lua_isstring(L, 2))
        {
            data = lua_tolstring(L, 2, &len);
        }
        else if (lua_isuserdata(L, 2))
        {
            data = (const char *)lua_tolstring(L, 2, &len);
        }
        else
        {
            luaL_error(L, "data must be string or binary");
            return 0;
        }
        ssize_t r = tcpSend(lh->handle, (const uint8_t *)data, len);
        if (r < 0)
        {
            lua_pushnil(L);
            lua_pushstring(L, "send failed");
            return 2;
        }
        lua_pushinteger(L, r);
        return 1;
    }

    // tcp_recv(sock, max_bytes, timeout_ms)
    static int l_tcp_recv(lua_State * L)
    {
        LuaHandle *lh = check_luahandle(L, 1, LH_TCP);
        int maxBytes = (int)luaL_checkinteger(L, 2);
        uint32_t timeout = NETWORK_DEFAULT_TIMEOUT_MS;
        if (lua_gettop(L) >= 3 && lua_isnumber(L, 3))
            timeout = (uint32_t)lua_tointeger(L, 3);
        if (maxBytes <= 0)
        {
            lua_pushnil(L);
            lua_pushstring(L, "invalid max_bytes");
            return 2;
        }
        std::vector<char> buf(maxBytes);
        ssize_t r = tcpRecv(lh->handle, (uint8_t *)buf.data(), buf.size(), timeout);
        if (r < 0)
        {
            lua_pushnil(L);
            lua_pushstring(L, "recv failed");
            return 2;
        }
        lua_pushlstring(L, buf.data(), (size_t)r);
        return 1;
    }

    // tcp_close(sock)
    static int l_tcp_close(lua_State * L)
    {
        LuaHandle *lh = check_luahandle(L, 1, LH_TCP);
        NetResult nr = tcpClose(lh->handle);
        if (!nr.ok())
        {
            lua_pushnil(L);
            lua_pushstring(L, nr.message.c_str());
            return 2;
        }
        // invalidate userdata
        lh->handle = -1;
        lua_pushboolean(L, 1);
        return 1;
    }

    // udp_open([local_port])
    static int l_udp_open(lua_State * L)
    {
        uint16_t localPort = 0;
        if (lua_gettop(L) >= 1 && lua_isnumber(L, 1))
            localPort = (uint16_t)lua_tointeger(L, 1);
        auto pr = udpOpen(localPort);
        if (!pr.second.ok())
        {
            lua_pushnil(L);
            lua_pushstring(L, pr.second.message.c_str());
            return 2;
        }
        UdpHandle h = pr.first;
        LuaHandle *lh = (LuaHandle *)lua_newuserdata(L, sizeof(LuaHandle));
        lh->handle = h;
        lh->type = LH_UDP;
        luaL_getmetatable(L, "net.udp");
        lua_setmetatable(L, -2);
        return 1;
    }

    // udp_send_to(udp_handle, host, port, data)
    static int l_udp_send_to(lua_State * L)
    {
        LuaHandle *lh = check_luahandle(L, 1, LH_UDP);
        const char *host = luaL_checkstring(L, 2);
        int port = (int)luaL_checkinteger(L, 3);
        size_t len = 0;
        const char *data = luaL_checklstring(L, 4, &len);
        ssize_t r = udpSendTo(lh->handle, String(host), (uint16_t)port, (const uint8_t *)data, len);
        if (r < 0)
        {
            lua_pushnil(L);
            lua_pushstring(L, "udp send failed");
            return 2;
        }
        lua_pushinteger(L, r);
        return 1;
    }

    // udp_recv(udp_handle, max_bytes, timeout_ms) -> data, from, port
    static int l_udp_recv(lua_State * L)
    {
        LuaHandle *lh = check_luahandle(L, 1, LH_UDP);
        int maxBytes = (int)luaL_checkinteger(L, 2);
        uint32_t timeout = NETWORK_DEFAULT_TIMEOUT_MS;
        if (lua_gettop(L) >= 3 && lua_isnumber(L, 3))
            timeout = (uint32_t)lua_tointeger(L, 3);
        if (maxBytes <= 0)
        {
            lua_pushnil(L);
            lua_pushstring(L, "invalid max_bytes");
            return 2;
        }
        std::vector<char> buf(maxBytes);
        String from;
        uint16_t fromport = 0;
        ssize_t r = udpReceiveFrom(lh->handle, (uint8_t *)buf.data(), buf.size(), from, fromport, timeout);
        if (r < 0)
        {
            lua_pushnil(L);
            lua_pushstring(L, "udp recv failed");
            return 2;
        }
        lua_pushlstring(L, buf.data(), (size_t)r);
        lua_pushstring(L, from.c_str());
        lua_pushinteger(L, fromport);
        return 3;
    }

    static int l_udp_close(lua_State * L)
    {
        LuaHandle *lh = check_luahandle(L, 1, LH_UDP);
        NetResult nr = udpClose(lh->handle);
        if (!nr.ok())
        {
            lua_pushnil(L);
            lua_pushstring(L, nr.message.c_str());
            return 2;
        }
        lh->handle = -1;
        lua_pushboolean(L, 1);
        return 1;
    }

    // tls_connect(host, port)
    static int l_tls_connect(lua_State * L)
    {
        const char *host = luaL_checkstring(L, 1);
        uint16_t port = 443;
        if (lua_gettop(L) >= 2 && lua_isnumber(L, 2))
            port = (uint16_t)lua_tointeger(L, 2);
        auto pr = tlsConnect(String(host), port);
        if (!pr.second.ok())
        {
            lua_pushnil(L);
            lua_pushstring(L, pr.second.message.c_str());
            return 2;
        }
        TlsHandle h = pr.first;
        LuaHandle *lh = (LuaHandle *)lua_newuserdata(L, sizeof(LuaHandle));
        lh->handle = h;
        lh->type = LH_TLS;
        luaL_getmetatable(L, "net.tls");
        lua_setmetatable(L, -2);
        return 1;
    }

    // tls_send(tls_handle, data)
    static int l_tls_send(lua_State * L)
    {
        LuaHandle *lh = check_luahandle(L, 1, LH_TLS);
        size_t len = 0;
        const char *data = luaL_checklstring(L, 2, &len);
        ssize_t r = tlsSend(lh->handle, (const uint8_t *)data, len);
        if (r < 0)
        {
            lua_pushnil(L);
            lua_pushstring(L, "tls send failed");
            return 2;
        }
        lua_pushinteger(L, r);
        return 1;
    }

    // tls_recv(tls_handle, max_bytes, timeout_ms)
    static int l_tls_recv(lua_State * L)
    {
        LuaHandle *lh = check_luahandle(L, 1, LH_TLS);
        int maxBytes = (int)luaL_checkinteger(L, 2);
        uint32_t timeout = NETWORK_DEFAULT_TIMEOUT_MS;
        if (lua_gettop(L) >= 3 && lua_isnumber(L, 3))
            timeout = (uint32_t)lua_tointeger(L, 3);
        if (maxBytes <= 0)
        {
            lua_pushnil(L);
            lua_pushstring(L, "invalid max_bytes");
            return 2;
        }
        std::vector<char> buf(maxBytes);
        ssize_t r = tlsRecv(lh->handle, (uint8_t *)buf.data(), buf.size(), timeout);
        if (r < 0)
        {
            lua_pushnil(L);
            lua_pushstring(L, "tls recv failed");
            return 2;
        }
        lua_pushlstring(L, buf.data(), (size_t)r);
        return 1;
    }

    static int l_tls_close(lua_State * L)
    {
        LuaHandle *lh = check_luahandle(L, 1, LH_TLS);
        NetResult nr = tlsClose(lh->handle);
        if (!nr.ok())
        {
            lua_pushnil(L);
            lua_pushstring(L, nr.message.c_str());
            return 2;
        }
        lh->handle = -1;
        lua_pushboolean(L, 1);
        return 1;
    }

    // http_request(options) -> response_table or nil, err
    static int l_http_request(lua_State * L)
    {
        String method, host, path, body;
        std::map<String, String> headers;
        uint16_t port = 0;
        bool useTls = false;
        uint32_t timeout = NETWORK_DEFAULT_TIMEOUT_MS;
        NetResult ok = lua_check_http_options(L, 1, method, host, path, headers, body, port, useTls, timeout);
        if (!ok.ok())
        {
            lua_pushnil(L);
            lua_pushstring(L, ok.message.c_str());
            return 2;
        }
        HttpResponse res = httpRequest(method, host, path, headers, body, port, useTls, timeout);
        if (!res.result.ok())
        {
            lua_pushnil(L);
            lua_pushstring(L, res.result.message.c_str());
            return 2;
        }
        push_http_response(L, res);
        return 1;
    }

    // http_request_async(options, callback)
    static int l_http_request_async(lua_State * L)
    {
        if (!lua_istable(L, 1))
        {
            lua_pushnil(L);
            lua_pushstring(L, "options table expected");
            return 2;
        }
        if (!lua_isfunction(L, 2))
        {
            lua_pushnil(L);
            lua_pushstring(L, "callback expected");
            return 2;
        }

        // Convert options
        String method, host, path, body;
        std::map<String, String> headers;
        uint16_t port = 0;
        bool useTls = false;
        uint32_t timeout = NETWORK_DEFAULT_TIMEOUT_MS;
        NetResult ok = lua_check_http_options(L, 1, method, host, path, headers, body, port, useTls, timeout);
        if (!ok.ok())
        {
            lua_pushnil(L);
            lua_pushstring(L, ok.message.c_str());
            return 2;
        }

        // Reference the Lua callback (create a reference)
        lua_pushvalue(L, 2);
        int cbRef = luaL_ref(L, LUA_REGISTRYINDEX);

        // Create C++ callback that pushes result into queue and when executed will call Lua callback.
        std::function<void(const HttpResponse &)> cb = [L, cbRef](const HttpResponse &r)
        {
            // When called from poll_network_events (main thread), this lambda is executed in main thread.
            // Acquire stack and call the Lua function ref.
            lua_rawgeti(L, LUA_REGISTRYINDEX, cbRef); // push function
            push_http_response(L, r);                 // push response table
            if (lua_pcall(L, 1, 0, 0) != LUA_OK)
            {
                // Error calling Lua callback; print message to Serial (best effort)
                const char *err = lua_tostring(L, -1);
                Serial.print("http async callback error: ");
                if (err)
                    Serial.println(err);
                else
                    Serial.println("unknown");
                lua_pop(L, 1);
            }
            // release reference
            luaL_unref(L, LUA_REGISTRYINDEX, cbRef);
        };

        NetResult nr = httpRequestAsync(method, host, path, cb, headers, body, port, useTls, timeout);
        if (!nr.ok())
        {
            // release ref
            luaL_unref(L, LUA_REGISTRYINDEX, cbRef);
            lua_pushnil(L);
            lua_pushstring(L, nr.message.c_str());
            return 2;
        }
        lua_pushboolean(L, 1);
        return 1;
    }

    // dns_lookup(hostname) -> ip_string | nil, err
    static int l_dns_lookup(lua_State * L)
    {
        const char *host = luaL_checkstring(L, 1);
        IPAddress ip;
        NetResult nr = dnsResolve(String(host), ip);
        if (!nr.ok())
        {
            lua_pushnil(L);
            lua_pushstring(L, nr.message.c_str());
            return 2;
        }
        String s = ip.toString();
        lua_pushstring(L, s.c_str());
        return 1;
    }

    // wifi_status()
    static int l_wifi_status(lua_State * L)
    {
        lua_pushinteger(L, (int)wifiStatus());
        return 1;
    }

    // has_internet()
    static int l_has_internet(lua_State * L)
    {
        lua_pushboolean(L, hasInternet());
        return 1;
    }

    /* --- Register functions and metatables --- */

    static const luaL_Reg net_funcs[] = {
        {"wifi_status", l_wifi_status},
        {"has_internet", l_has_internet},
        {"dns_lookup", l_dns_lookup},
        {"tcp_connect", l_tcp_connect},
        {"udp_open", l_udp_open},
        {"http_request", l_http_request},
        {"http_request_async", l_http_request_async},
        {"tls_connect", l_tls_connect},
        {"udp_send_to", l_udp_send_to}, // convenience: allow module-level call
        {"udp_recv", l_udp_recv},
        {"udp_close", l_udp_close},
        {NULL, NULL}};

    static const luaL_Reg tcp_methods[] = {
        {"send", l_tcp_send},
        {"recv", l_tcp_recv},
        {"close", l_tcp_close},
        {NULL, NULL}};

    static const luaL_Reg udp_methods[] = {
        {"send_to", l_udp_send_to},
        {"recv", l_udp_recv},
        {"close", l_udp_close},
        {NULL, NULL}};

    static const luaL_Reg tls_methods[] = {
        {"send", l_tls_send},
        {"recv", l_tls_recv},
        {"close", l_tls_close},
        {NULL, NULL}};

    void register_network_functions(lua_State * L)
    {
        // Create module table net = {}
        lua_newtable(L);

        // set functions
        luaL_setfuncs(L, net_funcs, 0);

        // create metatable for tcp userdata
        luaL_newmetatable(L, "net.tcp");
        // __index -> methods table
        lua_newtable(L);
        luaL_setfuncs(L, tcp_methods, 0);
        lua_setfield(L, -2, "__index");
        // __gc to ensure close
        lua_pushcfunction(L, [](lua_State *L) -> int
                          {
        LuaHandle *lh = (LuaHandle*)lua_touserdata(L, 1);
        if (lh && lh->type == LH_TCP && lh->handle > 0) {
            tcpClose(lh->handle);
            lh->handle = -1;
        }
        return 0; });
        lua_setfield(L, -2, "__gc");
        lua_pop(L, 1); // pop metatable

        // udp metatable
        luaL_newmetatable(L, "net.udp");
        lua_newtable(L);
        luaL_setfuncs(L, udp_methods, 0);
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, [](lua_State *L) -> int
                          {
        LuaHandle *lh = (LuaHandle*)lua_touserdata(L, 1);
        if (lh && lh->type == LH_UDP && lh->handle > 0) {
            udpClose(lh->handle);
            lh->handle = -1;
        }
        return 0; });
        lua_setfield(L, -2, "__gc");
        lua_pop(L, 1);

        // tls metatable
        luaL_newmetatable(L, "net.tls");
        lua_newtable(L);
        luaL_setfuncs(L, tls_methods, 0);
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, [](lua_State *L) -> int
                          {
        LuaHandle *lh = (LuaHandle*)lua_touserdata(L, 1);
        if (lh && lh->type == LH_TLS && lh->handle > 0) {
            tlsClose(lh->handle);
            lh->handle = -1;
        }
        return 0; });
        lua_setfield(L, -2, "__gc");
        lua_pop(L, 1);

        // set table at global 'net'
        lua_setglobal(L, "net");
    }

} // namespace LuaApps::Network