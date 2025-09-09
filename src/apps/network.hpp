#pragma once

#include <Arduino.h>
#include <lua.hpp>

#include "../wifi/index.hpp"
#include "app.hpp"

#include <WiFi.h>
#include <vector>
#include <map>
#include <functional>
#include <stdint.h>
#include <stddef.h>

namespace LuaApps::Network
{
    /*
      Network subsystem for LuaApps (header-only declarations).

      Goals / features exposed:
       - DNS lookup (sync)
       - TCP client (connect/send/receive/close)
       - TCP server (listen / accept / per-client handles)
       - UDP (send/receive)
       - TLS client (based on WiFiClientSecure)
       - Simple HTTPS/HTTP request helper (sync + async callback hooks)
       - WiFi helpers (status, hasInternet)
       - Registration of functions / metatables in Lua
       - Helpers to convert network results <-> Lua values

      Design notes:
       - C++ API uses integer handles (socket/server IDs) — Lua code receives lightweight handles
         (userdata/metatables are provided for niceties).
       - All long-running IO should be executed by user's code or in dedicated tasks; header only
         declares the API surface. Implementations should take care of blocking behavior and timeouts.
    */

    /* --- Constants / configuration --- */
    constexpr size_t NETWORK_MAX_SOCKETS = 8;             // maximum number of simultaneous sockets (tunable)
    constexpr uint32_t NETWORK_DEFAULT_TIMEOUT_MS = 5000; // default blocking timeout

    /* --- Error codes --- */
    enum class NetError : int32_t
    {
        OK = 0,
        UNKNOWN = -1,
        TIMEOUT = -2,
        BAD_ARG = -3,
        NOT_CONNECTED = -4,
        ALREADY_CONNECTED = -5,
        NO_RESOURCES = -6,
        DNS_FAIL = -7,
        TLS_FAIL = -8,
        IO = -9,
        NOT_IMPLEMENTED = -100
    };

    /* --- Result wrapper (C++ side) --- */
    struct NetResult
    {
        NetError err;
        String message; // optional human-friendly message
        NetResult(NetError e = NetError::OK, const String &m = String()) : err(e), message(m) {}
        bool ok() const { return err == NetError::OK; }
    };

    /* --- HTTP response representation --- */
    struct HttpResponse
    {
        int status_code = 0;
        String body;
        std::map<String, String> headers;
        NetResult result; // success / failure metadata
    };

    /* --- Socket / server handle types --- */
    // Use small integer handles for C API / Lua binding.
    using SocketHandle = int32_t;
    using ServerHandle = int32_t;
    using UdpHandle = int32_t;
    using TlsHandle = int32_t;

    /* --- WiFi helpers --- */

    // Return true if the system (UserWiFi or WiFi) reports any internet connectivity
    bool hasInternet(); // uses UserWiFi::hasInternet() if available, otherwise falls back to WiFi.status()

    // Return the raw WiFi.status() value (wl_status_t)
    wl_status_t wifiStatus();

    /* --- DNS --- */

    // Resolve hostname to IP address (synchronous).
    // Returns NetResult::OK and fills outIp on success.
    NetResult dnsResolve(const String &hostname, IPAddress &outIp, uint32_t timeoutMs = NETWORK_DEFAULT_TIMEOUT_MS);

    /* --- TCP client API (C++ side) --- */

    // Create a new TCP client and connect to host:port. On success returns a non-negative SocketHandle.
    // On failure returns negative value and fills result.err.
    std::pair<SocketHandle, NetResult> tcpConnect(const String &host, uint16_t port, uint32_t timeoutMs = NETWORK_DEFAULT_TIMEOUT_MS);

    // Send raw bytes on a connected socket. Returns number of bytes written or negative on error.
    ssize_t tcpSend(SocketHandle sock, const uint8_t *data, size_t len, uint32_t timeoutMs = NETWORK_DEFAULT_TIMEOUT_MS);

    // Receive up to bufferLen bytes into buffer. Returns number of bytes read (0 on EOF), or negative on error/timeout.
    ssize_t tcpRecv(SocketHandle sock, uint8_t *buffer, size_t bufferLen, uint32_t timeoutMs = NETWORK_DEFAULT_TIMEOUT_MS);

    // Close a connected socket immediately.
    NetResult tcpClose(SocketHandle sock);

    /* --- TCP server API (C++ side) --- */

    // Start listening on port. Returns server handle or negative on error.
    // onClient is a callback invoked when a new client is accepted; it receives the client SocketHandle.
    // Implementations may call the callback on the networking task or schedule it for the main loop — see implementation.
    std::pair<ServerHandle, NetResult> tcpServerListen(uint16_t port, std::function<void(SocketHandle client)> onClient, uint16_t backlog = 4);

    // Stop and free server.
    NetResult tcpServerStop(ServerHandle server);

    /* --- UDP API (C++ side) --- */

    // Open a UDP handle (bind to an optional local port). Returns handle >=0 on success.
    std::pair<UdpHandle, NetResult> udpOpen(uint16_t localPort = 0);

    // Send a UDP packet to destination (host or IP string). Returns number of bytes sent or negative on error.
    ssize_t udpSendTo(UdpHandle h, const String &hostOrIp, uint16_t port, const uint8_t *data, size_t len);

    // Receive a UDP packet into buffer. On success returns number of bytes received and fills outFrom/outPort.
    // Returns negative on error or timeout.
    ssize_t udpReceiveFrom(UdpHandle h, uint8_t *buffer, size_t bufferLen, String &outFrom, uint16_t &outPort, uint32_t timeoutMs = NETWORK_DEFAULT_TIMEOUT_MS);

    // Close UDP handle.
    NetResult udpClose(UdpHandle h);

    /* --- TLS / HTTPS helpers --- */

    // Create a TLS connection (WiFiClientSecure style) to host:port; returns TlsHandle >=0 on success.
    std::pair<TlsHandle, NetResult> tlsConnect(const String &host, uint16_t port = 443, uint32_t timeoutMs = NETWORK_DEFAULT_TIMEOUT_MS);

    // Send/recv for TLS handles (same semantics as tcpSend/tcpRecv).
    ssize_t tlsSend(TlsHandle t, const uint8_t *data, size_t len, uint32_t timeoutMs = NETWORK_DEFAULT_TIMEOUT_MS);
    ssize_t tlsRecv(TlsHandle t, uint8_t *buffer, size_t bufferLen, uint32_t timeoutMs = NETWORK_DEFAULT_TIMEOUT_MS);
    NetResult tlsClose(TlsHandle t);

    // Perform an HTTP(S) request synchronously. If useTls==true, ensures TLS is used.
    // `headers` may contain additional request headers. Returns HttpResponse (status and body).
    HttpResponse httpRequest(const String &method,
                             const String &host,
                             const String &path,
                             const std::map<String, String> &headers = {},
                             const String &body = String(),
                             uint16_t port = 0,
                             bool useTls = false,
                             uint32_t timeoutMs = NETWORK_DEFAULT_TIMEOUT_MS);

    // Perform an asynchronous HTTP(S) request — the callback will be invoked when finished.
    // Callback signature: void(HttpResponse response).
    // Implementations should ensure callback runs in a safe context to call into Lua (or schedule for main loop).
    NetResult httpRequestAsync(const String &method,
                               const String &host,
                               const String &path,
                               std::function<void(const HttpResponse &)> callback,
                               const std::map<String, String> &headers = {},
                               const String &body = String(),
                               uint16_t port = 0,
                               bool useTls = false,
                               uint32_t timeoutMs = NETWORK_DEFAULT_TIMEOUT_MS);

    /* --- Lua bindings / registration --- */

    /*
      Call to register all "net.*" functions and metatables in the given lua_State.

      Exposed Lua API (suggested names and signatures):
        -- WiFi
        net.wifi_status() -> integer  -- wl_status_t
        net.has_internet() -> boolean

        -- DNS
        net.dns_lookup(hostname) -> ip_string | nil, err

        -- TCP client
        local sock = net.tcp_connect(host, port [, timeout_ms])  -- returns userdata or nil+err
        sock:send(binary_or_string) -> bytes_sent | nil, err
        local data = sock:recv(max_bytes [, timeout_ms]) -> string|nil, err
        sock:close()

        -- TCP server
        local server = net.tcp_listen(port, function(client_sock) ... end)
        server:stop()

        -- UDP
        local u = net.udp_open([local_port])
        u:send_to(host, port, data)
        local data, from, fromport = u:recv(max_bytes, timeout_ms)
        u:close()

        -- TLS
        local t = net.tls_connect(host [, port])
        t:send(...)
        t:recv(...)
        t:close()

        -- HTTP(S)
        -- sync
        local res = net.http_request({ method = "GET", host = "example.com", path = "/", headers = {...}, body = "" , tls=true })
        -- async (callback receives a response table)
        net.http_request_async({ ... }, function(response_table) end)

      The exact Lua table shapes and userdata metatables are defined in the implementation.
    */
    void register_network_functions(lua_State *L);

    /* --- Lua helper utilities --- */

    // Push an HttpResponse as a Lua table onto the stack (table with fields: status, body, headers (table), ok, err_message)
    void push_http_response(lua_State *L, const HttpResponse &r);

    // Convert a Lua table (method/host/path/headers/body) into C++ arguments for httpRequest.
    NetResult lua_check_http_options(lua_State *L, int index,
                                     String &outMethod, String &outHost, String &outPath,
                                     std::map<String, String> &outHeaders, String &outBody,
                                     uint16_t &outPort, bool &outUseTls, uint32_t &outTimeoutMs);

    /* --- Lifecycle / poll --- */

    // If the implementation has background work or async callbacks that need to be pumped from the main loop,
    // call this periodically from the main loop (non-blocking). Implementations that schedule callbacks to run
    // on the main thread should implement them here.
    void poll_network_events();

    /* --- Utility --- */

    // Convert NetError to readable string
    const char *netErrorToString(NetError e);

} // namespace LuaApps::Network
