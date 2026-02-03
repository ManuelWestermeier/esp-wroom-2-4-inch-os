// browser_client.cpp
// Implements a MWOSP-v1 client for ESP32 using WebSocketsClient and SPIFFS.
// Place alongside your existing project. Compile for ESP32 (Arduino core).
//
// Requirements (PlatformIO/Arduino):
//   - ESP32 board support
//   - arduinoWebSockets library (WebSocketsClient)
//   - Enable SPIFFS in your project
//
// This file implements robust reconnect handling, the protocol described by the user,
// basic rendering commands (FillReact, PrintText, Navigate, SetSession, GetSession, GetState, GetText),
// and touch-to-click forwarding. It uses mbedtls to base64-encode session payloads.

#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <SPIFFS.h>
#include <FS.h>
#include "mbedtls/base64.h"

#include "index.hpp"             // Screen::tft, Screen::isTouched(), Screen::getTouchPos()
#include "../io/read-string.hpp" // readString(question, defaultValue)

#ifndef BROWSER_CLIENT_DEBUG
#define BROWSER_CLIENT_DEBUG 1
#endif

#if BROWSER_CLIENT_DEBUG
#define DBG(...)                    \
    do                              \
    {                               \
        Serial.printf(__VA_ARGS__); \
    } while (0)
#else
#define DBG(...)
#endif

namespace Browser
{
    using String = ::String;

    struct Location
    {
        String domain;           // domain[:port]
        String state;            // like "lists|1234|edit"
        String session;          // arbitrary string stored locally (max ~1KB)
        static String sessionId; // random id per run
    };

    // static vars
    String Location::sessionId = String();
    static Location loc;
    static bool isRunning = false;

    // Websocket client
    static WebSocketsClient webSocket;

    // connection state
    static bool wsConnected = false;
    static unsigned long lastPingSent = 0;
    static unsigned long lastPongReceived = 0;
    static unsigned long lastReconnectAttempt = 0;
    static unsigned long reconnectBackoff = 500;              // ms, exponential
    static const unsigned long RECONNECT_MAX = 30UL * 1000UL; // 30s max backoff

    // touch handling
    static bool lastTouched = false;
    static unsigned long lastTouchTime = 0;
    static int movementThreshold = 50; // ms

    // default server if none provided
    static const char *DEFAULT_SERVER = "mw-search-server.onrender.app";
    static const int DEFAULT_PORT = 6767;
    static const char *DEFAULT_PATH = "/";

    // helpers: parse domain:port
    static void parseDomainPort(const String &d, String &hostOut, uint16_t &portOut)
    {
        hostOut = d;
        portOut = DEFAULT_PORT;
        int colon = d.indexOf(':');
        if (colon >= 0)
        {
            hostOut = d.substring(0, colon);
            String portStr = d.substring(colon + 1);
            portOut = (uint16_t)portStr.toInt();
            if (portOut == 0)
                portOut = DEFAULT_PORT;
        }
    }

    // SPIFFS storage helpers (path: /browser/<host>/session.data)
    static String storagePathForDomain(const String &domain)
    {
        String host;
        uint16_t port;
        parseDomainPort(domain, host, port);
        String p = "/browser/";
        p += host;
        if (port != DEFAULT_PORT)
        {
            p += ":";
            p += String(port);
        }
        p += "/session.data";
        return p;
    }

    static bool ensureStorage()
    {
        if (!SPIFFS.begin(true))
        {
            DBG("SPIFFS mount failed\n");
            return false;
        }
        return true;
    }

    static String readSessionFromStorage(const String &domain)
    {
        if (!ensureStorage())
            return String();
        String p = storagePathForDomain(domain);
        if (!SPIFFS.exists(p))
            return String();
        File f = SPIFFS.open(p, FILE_READ);
        if (!f)
            return String();
        String out;
        out.reserve(f.size() + 1);
        while (f.available())
            out += (char)f.read();
        f.close();
        return out;
    }

    static bool writeSessionToStorage(const String &domain, const String &data)
    {
        if (!ensureStorage())
            return false;
        String p = storagePathForDomain(domain);
        // ensure directory
        int slash = p.lastIndexOf('/');
        if (slash > 1)
        {
            String dir = p.substring(0, slash);
            if (!SPIFFS.exists(dir))
            {
                SPIFFS.mkdir(dir);
            }
        }
        File f = SPIFFS.open(p, FILE_WRITE);
        if (!f)
        {
            DBG("Failed to open %s for write\n", p.c_str());
            return false;
        }
        size_t written = f.print(data);
        f.close();
        return written == data.length();
    }

    // base64 encode for GetBackSession reply
    static String base64Encode(const String &s)
    {
        if (s.length() == 0)
            return String();
        size_t out_len = 0;
        const uint8_t *in = (const uint8_t *)s.c_str();
        mbedtls_base64_encode(NULL, 0, &out_len, in, s.length()); // get required size
        if (out_len == 0)
            return String();
        std::vector<unsigned char> outbuf(out_len + 1);
        if (mbedtls_base64_encode(outbuf.data(), outbuf.size(), &out_len, in, s.length()) != 0)
        {
            return String();
        }
        return String((char *)outbuf.data(), (int)out_len);
    }

    static String base64Decode(const String &s)
    {
        if (s.length() == 0)
            return String();
        size_t out_len = 0;
        mbedtls_base64_decode(NULL, 0, &out_len, (const unsigned char *)s.c_str(), s.length());
        if (out_len == 0)
            return String();
        std::vector<unsigned char> outbuf(out_len + 1);
        if (mbedtls_base64_decode(outbuf.data(), outbuf.size(), &out_len, (const unsigned char *)s.c_str(), s.length()) != 0)
        {
            return String();
        }
        return String((char *)outbuf.data(), (int)out_len);
    }

    // send helper (string)
    static void wsSend(const String &s)
    {
        if (!wsConnected)
        {
            DBG("wsSend: not connected, drop: %s\n", s.c_str());
            return;
        }
        webSocket.sendTXT(s);
        DBG(">> %s\n", s.c_str());
    }

    // Protocol command handlers
    static void handleFillReact(const String &args)
    {
        // expected: X Y W H COLOR
        int vals[5] = {0, 0, 0, 0, 0};
        int i = 0;
        int start = 0;
        for (int k = 0; k < 5; ++k)
        {
            int sp = args.indexOf(' ', start);
            String token;
            if (sp >= 0)
            {
                token = args.substring(start, sp);
                start = sp + 1;
            }
            else
            {
                token = args.substring(start);
                start = args.length();
            }
            vals[k] = token.toInt();
        }
        int x = vals[0], y = vals[1], w = vals[2], h = vals[3];
        uint16_t color = (uint16_t)vals[4];
        // call TFT
        tft.fillRect(x, y, w, h, color);
    }

    static void handlePrintText(const String &args)
    {
        // PrintText X Y COLOR TEXT...
        // parse first three tokens, rest is text
        int s1 = args.indexOf(' ');
        if (s1 < 0)
            return;
        int s2 = args.indexOf(' ', s1 + 1);
        if (s2 < 0)
            return;
        int s3 = args.indexOf(' ', s2 + 1);
        if (s3 < 0)
            return;
        int x = args.substring(0, s1).toInt();
        int y = args.substring(s1 + 1, s2).toInt();
        uint16_t color = (uint16_t)args.substring(s2 + 1, s3).toInt();
        String text = args.substring(s3 + 1);
        tft.setCursor(x, y);
        tft.setTextColor(color);
        tft.print(text);
    }

    static void doReRender()
    {
        // Clear screen and request full render from server by sending "Navigate <state>"? Protocol doesn't define
        // We'll simply send a "ReRender" request so server can push draw commands if supported.
        tft.fillScreen(0x0000); // clear black by default
        if (wsConnected)
        {
            String req = "ReRender";
            req += " ";
            req += loc.state;
            wsSend(req);
        }
    }

    // Incoming messages parser
    static void handleServerMessage(const String &msg)
    {
        DBG("<< %s\n", msg.c_str());
        if (msg.length() == 0)
            return;
        // split first token
        int sp = msg.indexOf(' ');
        String cmd = (sp >= 0) ? msg.substring(0, sp) : msg;
        String args = (sp >= 0) ? msg.substring(sp + 1) : String();

        if (cmd == "MWOSP-v1")
        {
            // server greeting, e.g. "MWOSP-v1 OK" => ignore/ack
            // could be "MWOSP-v1 OK" or more. nothing to do.
            DBG("Server protocol greeting: %s\n", msg.c_str());
            return;
        }
        else if (cmd == "FillReact")
        {
            handleFillReact(args);
        }
        else if (cmd == "PrintText")
        {
            handlePrintText(args);
        }
        else if (cmd == "Navigate")
        {
            // set new state
            loc.state = args;
            DBG("Navigate -> %s\n", loc.state.c_str());
            doReRender();
        }
        else if (cmd == "SetSession")
        {
            // rest is base64 or raw? assume raw string (server should send safe characters).
            // To be robust accept base64 token or raw: if contains non-printable, assume base64 decode.
            String data = args;
            // Trim
            data.trim();
            // Try decode - if decode produces something plausible we accept, else we store raw
            String decoded = base64Decode(data);
            if (decoded.length() > 0)
            {
                writeSessionToStorage(loc.domain, decoded);
                loc.session = decoded;
            }
            else
            {
                writeSessionToStorage(loc.domain, data);
                loc.session = data;
            }
            DBG("SetSession saved (%d bytes)\n", loc.session.length());
        }
        else if (cmd == "GetSession")
        {
            // args: RETURNID
            String returnId = args;
            String sess = readSessionFromStorage(loc.domain);
            if (sess.length() == 0)
                sess = loc.session; // fallback
            String encoded = base64Encode(sess);
            String reply = "GetBackSession ";
            reply += returnId;
            reply += " ";
            reply += encoded;
            wsSend(reply);
        }
        else if (cmd == "GetState")
        {
            String returnId = args;
            String reply = "GetBackState ";
            reply += returnId;
            reply += " ";
            // base64 encode state to be safe
            reply += base64Encode(loc.state);
            wsSend(reply);
        }
        else if (cmd == "GetText")
        {
            // server asks client to show input and return text
            // args: RETURNID [PROMPT] (we will treat all after first token as prompt)
            int s = args.indexOf(' ');
            String returnId = args;
            String prompt;
            if (s >= 0)
            {
                returnId = args.substring(0, s);
                prompt = args.substring(s + 1);
            }
            if (prompt.length() == 0)
                prompt = "Input:";
            // Use provided readString helper which blocks for user input on device
            String val = readString(prompt, "");
            // escape/encode
            String enc = base64Encode(val);
            String reply = "GetBackText ";
            reply += returnId;
            reply += " ";
            reply += enc;
            wsSend(reply);
        }
        else if (cmd == "SetState")
        {
            loc.state = args;
            DBG("SetState -> %s\n", loc.state.c_str());
            doReRender();
        }
        else
        {
            DBG("Unhandled command: %s (args='%s')\n", cmd.c_str(), args.c_str());
        }
    }

    // websocket event callback
    static void webSocketEvent(WStype_t type, uint8_t *payload, size_t length)
    {
        switch (type)
        {
        case WStype_CONNECTED:
        {
            wsConnected = true;
            lastPongReceived = millis();
            reconnectBackoff = 500;
            DBG("Websocket connected\n");
            // send handshake: MWOSP-v1 <sessionId> VIEWPORTX VIEWPORTY
            int w = tft.width();
            int h = tft.height();
            String handshake = "MWOSP-v1 ";
            handshake += Location::sessionId;
            handshake += " ";
            handshake += String(w);
            handshake += " ";
            handshake += String(h);
            wsSend(handshake);
            // if stored session exists, optionally inform server?
            String sess = readSessionFromStorage(loc.domain);
            if (sess.length())
            {
                // let server know current session
                String enc = base64Encode(sess);
                String s = "SetSession ";
                s += enc;
                wsSend(s);
            }
            break;
        }
        case WStype_DISCONNECTED:
        {
            wsConnected = false;
            DBG("Websocket disconnected\n");
            lastReconnectAttempt = millis();
            break;
        }
        case WStype_TEXT:
        {
            String msg((char *)payload, length);
            handleServerMessage(msg);
            break;
        }
        case WStype_BIN:
        {
            // not used
            DBG("Binary message (len=%d)\n", (int)length);
            break;
        }
        case WStype_ERROR:
        {
            DBG("Websocket error\n");
            break;
        }
        case WStype_PONG:
        {
            lastPongReceived = millis();
            DBG("PONG\n");
            break;
        }
        case WStype_PING:
        {
            DBG("PING received\n");
            webSocket.sendPong(); // reply
            break;
        }
        default:
            break;
        }
    }

    // Connect websocket to loc.domain (host:port) path DEFAULT_PATH
    static void connectWebsocket()
    {
        if (loc.domain.length() == 0)
        {
            loc.domain = String(DEFAULT_SERVER) + ":" + String(DEFAULT_PORT);
        }
        String host;
        uint16_t port;
        parseDomainPort(loc.domain, host, port);

        DBG("Connecting to %s:%d%s\n", host.c_str(), port, DEFAULT_PATH);
        webSocket.begin(host.c_str(), port, DEFAULT_PATH);
        webSocket.onEvent(webSocketEvent);
        webSocket.setReconnectInterval(0); // we manage reconnect ourselves for robust backoff
        webSocket.setExtraHeaders(NULL);
    }

    // Public API implementations

    void ReRender()
    {
        doReRender();
    }

    void Update()
    {
        // Call frequently inside your main loop while browser is running.
        // - Manage WiFi
        // - Manage websocket connection with backoff
        // - Forward touch events
        // - Periodic ping
        if (!isRunning)
            return;

        // ensure WiFi connected
        if (WiFi.status() != WL_CONNECTED)
        {
            DBG("WiFi not connected; attempting WiFi.reconnect()\n");
            WiFi.reconnect();
        }

        // websocket management
        if (!wsConnected)
        {
            unsigned long now = millis();
            if (now - lastReconnectAttempt >= reconnectBackoff)
            {
                lastReconnectAttempt = now;
                DBG("Attempting websocket connect (backoff=%lu)\n", reconnectBackoff);
                connectWebsocket();
                reconnectBackoff = min(RECONNECT_MAX, reconnectBackoff * 2);
            }
        }

        // process websocket background jobs
        webSocket.loop();

        // send periodic ping to keep alive and detect silent disconnects
        unsigned long now = millis();
        if (now - lastPingSent > 20000)
        { // 20s
            if (wsConnected)
            {
                webSocket.sendPing();
                lastPingSent = now;
                DBG("PING sent\n");
            }
        }
        // If we haven't received a PONG for >60s, treat as disconnected
        if (wsConnected && now - lastPongReceived > 60000)
        {
            DBG("No PONG for >60s, forcing disconnect\n");
            webSocket.disconnect();
            wsConnected = false;
            lastReconnectAttempt = now;
        }

        // touch handling -> Click events: send "Click X Y"
        if (Screen::isTouched())
        {
            Screen::TouchPos tp = Screen::getTouchPos();
            // if it is a click event (clicked became true) or movement ended?
            if (tp.clicked)
            {
                String msg = "Click ";
                msg += String((int)tp.x);
                msg += " ";
                msg += String((int)tp.y);
                wsSend(msg);
                DBG("Sent Click at %d,%d\n", (int)tp.x, (int)tp.y);
                // small debounce
                vTaskDelay(10);
            }
            else
            {
                // Could send Move events if desired (not part of protocol)
            }
            lastTouched = true;
            lastTouchTime = now;
        }
        else
        {
            if (lastTouched)
            {
                // touch released
                lastTouched = false;
                // Could send 'Release' if protocol required
            }
        }
    }

    void Start()
    {
        // Called when browser should start. Prepare sessionId and defaults.
        if (Location::sessionId.length() == 0)
        {
            uint32_t r = esp_random();
            Location::sessionId = String(r);
        }
        // If loc.domain is empty, use default
        if (loc.domain.length() == 0)
        {
            loc.domain = String(DEFAULT_SERVER) + ":" + String(DEFAULT_PORT);
        }
        // Try to load saved state for this domain
        String savedState = readSessionFromStorage(loc.domain); // try session file as state if present
        if (savedState.length() > 0 && loc.state.length() == 0)
        {
            // best-effort: if savedState seems to contain "state|" pattern, set as state; otherwise keep as session
            loc.session = savedState;
        }
        // initialize websocket library (one-time)
        webSocket = WebSocketsClient();
        webSocket.setReconnectInterval(0); // manual
        webSocket.onEvent(webSocketEvent);

        // Start connection immediately
        connectWebsocket();

        isRunning = true;
        DBG("Browser started (sessionId=%s)\n", Location::sessionId.c_str());
    }

    void Exit()
    {
        // Gracefully stop the browser loop; will disconnect in OnExit.
        isRunning = false;
        DBG("Browser Exit requested\n");
    }

    void OnExit()
    {
        // Called after main loop ends; close ws and cleanup
        if (wsConnected)
        {
            webSocket.sendTXT("ClientDisconnect");
            webSocket.disconnect();
            wsConnected = false;
        }
        webSocket.onEvent(nullptr);
        webSocket.disconnect();
        DBG("Browser OnExit completed\n");
    }

    // Utility: allow externally setting location string like "host:port@state"
    void setLocationFromString(const String &s)
    {
        // format: domain[:port][@state]
        int at = s.indexOf('@');
        String domainPart = s;
        String statePart;
        if (at >= 0)
        {
            domainPart = s.substring(0, at);
            statePart = s.substring(at + 1);
        }
        if (domainPart.length())
            loc.domain = domainPart;
        if (statePart.length())
            loc.state = statePart;
    }

    // For debugging / info
    String getStatus()
    {
        String st = "running=";
        st += isRunning ? "1" : "0";
        st += " ws=";
        st += wsConnected ? "1" : "0";
        st += " domain=";
        st += loc.domain;
        st += " state=";
        st += loc.state;
        return st;
    }
} // namespace Browser

// Provide extern "C" wrappers if required by other files
extern "C"
{
    void browser_start() { Browser::Start(); }
    void browser_update() { Browser::Update(); }
    void browser_exit() { Browser::Exit(); }
    void browser_onexit() { Browser::OnExit(); }
    void browser_set_location(const char *s) { Browser::setLocationFromString(String(s)); }
    const char *browser_status()
    {
        static String s;
        s = Browser::getStatus();
        return s.c_str();
    }
}
