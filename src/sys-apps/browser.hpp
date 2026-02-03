#pragma once

#include <Arduino.h>

#include "../screen/index.hpp"
/**
#pragma once

#include <Arduino.h>

#include "../utils/vec.hpp"
#include "config.h"
#include "svg.hpp"
#include "../icons/index.hpp"

#include <TFT_eSPI.h>
#include <SD.h>

#define BRIGHTNESS_MIN 5

namespace Screen
{
    // The one-and-only TFT object (defined in index.cpp)
    extern TFT_eSPI tft;

    // Threshold for movement accumulation (milliseconds)
    extern int MOVEMENT_TIME_THRESHOLD;

    // Set backlight brightness (0–255)
    void setBrightness(byte b = 255, bool store = true);
    byte getBrightness();

    // Initialize display and touch
    void init();

    // Touch data: absolute position + movement delta
    struct TouchPos : Vec
    {
        bool clicked;
        Vec move;
    };

    // Read the current touch state
    bool isTouched();
    TouchPos getTouchPos();

    void drawImageFromSD(const char *filename, int x, int y);
}

using Screen::tft;
#pragma once

#include "index.hpp"

extern "C"
{
#include "nanosvg.h" // nur einbinden, kein #define!
}

NSVGimage *createSVG(const String &svgString);

bool drawSVGString(const String &imageStr,
                   int xOff, int yOff,
                   int targetW, int targetH,
                   uint16_t color, int steps = 4);

void updateSVGList();
  */

#include "../io/read-string.hpp" // String readString(const String &question = "", const String &defaultValue = "");
#include "../styles/global.hpp"
/*#define BG Style::Colors::bg
#define TEXT Style::Colors::text
#define PRIMARY Style::Colors::primary
#define ACCENT Style::Colors::accent
#define ACCENT2 Style::Colors::accent2
#define ACCENT3 Style::Colors::accent3
#define DANGER Style::Colors::danger
#define PRESSED Style::Colors::pressed
#define PH Style::Colors::placeholder
#define AT Style::Colors::accentText */
#include "../fs/enc-fs.hpp"
/*
#pragma once

#include <Arduino.h>
#include <vector>
#include <FS.h>
#include <SPIFFS.h>
#include <SD.h>

namespace ENC_FS
{
    using Buffer = std::vector<uint8_t>;
    using Path = std::vector<String>;

    struct Metadata
    {
        long size;
        String encryptedName;
        String decryptedName;
        bool isDirectory;
    };

    // ---------- Helpers ----------

    Buffer sha256(const String &s);
    void pkcs7_pad(Buffer &b);
    bool pkcs7_unpad(Buffer &b);
    String base64url_encode(const uint8_t *data, size_t len);
    bool base64url_decode(const String &s, Buffer &out);
    static void deriveNonceForFullPathVersion(const String &full, uint64_t version, uint8_t nonce[16]);
    static bool writeVersionForFullPath(const String &full, uint64_t v);
    static uint64_t readVersionForFullPath(const String &full);

    Buffer deriveKey();

    // ---------- Path helpers ----------

    Path str2Path(const String &s);
    String path2Str(const Path &p);

    // ---------- Segment encryption/decryption ----------
    String joinEncPath(const Path &plain);
    // ---------- File AES-CTR ----------

    Buffer aes_ctr_crypt_full_with_nonce(const Buffer &in, const uint8_t nonce[16]);
    Buffer aes_ctr_crypt_offset_with_nonce(const Buffer &in, size_t offset, const uint8_t nonce[16]);

    // ---------- File API ----------

    bool exists(const Path &p);
    bool mkDir(const Path &p);
    bool rmDir(const Path &p);
    Buffer readFilePart(const Path &p, long start, long end);
    Buffer readFile(const Path &p, long start, long end);
    String readFileString(const Path &p);
    bool writeFile(const Path &p, long start, long end, const Buffer &data);
    bool appendFile(const Path &p, const Buffer &data);
    bool writeFileString(const Path &p, const String &s);
    bool deleteFile(const Path &p);
    long getFileSize(const Path &p);
    Metadata getMetadata(const Path &p);
    std::vector<String> readDir(const Path &plainDir);
    void lsDirSerial(const Path &plainDir);

    Path storagePath(const String &appId, const String &key);

    namespace Storage
    {
        Buffer get(const String &appId, const String &key, long start = -1, long end = -1);
        bool del(const String &appId, const String &key);
        bool set(const String &appId, const String &key, const Buffer &data);
    }

    void copyFileFromSPIFFS(const char *spiffsPath, const Path &sdPath);
} // namespace ENC_FS
*/

/**
Do the protocol like:
// init
Client: MWOSP-v1 sessionId VIEWPORTX VIEWPORTY
Server: MWOSP-v1 OK

then:
User-Input like click => Client: Click X Y
NavigateByUserVia URL set => Client: Navigate state

Server send rendering-data: 
Server: FillReact X Y W H COLOR (16Bit) (for printText, pushSvg, printPx, pushImage ... all important functions)

Server send data:
Server: Navigate newState
Server: SetSession newData

Server Want Data:
Server: GetSession RETURNID
=> Client: GetBackSession RETURNID DATA
Server: GetState RETURNID
=> Client: GetBackState RETURNID DATA
Server: GetText RETURNID
=> Client: GetBackText RETURNID TEXT 

*/

namespace Browser
{
    struct Location
    {
        String domain;           // domain:port
        String state;            // like "lists|1234|edit"
        String session;          // max 1 KB storable and readable from {"browser", "sites", domain, "session.data"}
        static String sessionId; // uint32 every newstart random, global
    };
    static Location loc;
    static bool isRunning = false;

    void ReRender()
    {
        // Full Rerender based on state (not between sessions)
        // Layout: tob Bar: 20px heigh, input filed, return, go back/forward, settings, history, exitBtn
        // Pages: View, HistoryPage, Settings Page, Manage Sessions Page (delete), default credential page, ...
    }

    void Update()
    {
        // hand data, input
        // Create Websocket to the server, send seccion-id.
        // Server can: give tft commands (like drawString, fillRect, ...., draw Svg)
        // Can get sessionId, state (+set), session (+set), get color pallete by user, get viewportsize, get string input, get click pos
        // (fully server rendered)
    }

    void Start()
    {
        // get the domain, + state (from "domain:port@state") (default-port: 6767; default = "mw-search-server.onrender.app:6767@startpage")
    }

    void Exit()
    {
        // prompt (do you want?)
        isRunning = false;
    }

    void OnExit()
    {
        // Disconnect + ?
    }
}

static inline void openBrowser()
{
    Browser::isRunning = true;
    Browser::Start();
    while (Browser::isRunning)
    {
        Browser::Update();
        vTaskDelay(10);
    }
    Browser::OnExit();
}