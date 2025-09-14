void drawVideoTime(uint32_t currentSec, uint32_t totalSec, int x, int y, int w, int h)
{
    // Format mm:ss for current and total
    auto formatTime = [](uint32_t s) -> String
    {
        uint32_t m = s / 60;
        uint32_t sec = s % 60;
        String mm = String(m);
        String ss = String(sec);
        if (mm.length() < 2)
            mm = "0" + mm;
        if (ss.length() < 2)
            ss = "0" + ss;
        return mm + ":" + ss;
    };

    String timeStr = formatTime(currentSec) + " / " + formatTime(totalSec);

    Screen::tft.setTextSize(1);
    Screen::tft.setTextColor(AT);
    Screen::tft.fillRoundRect(x, y, w, h, 4, ACCENT);
    Screen::tft.setCursor(x + 6, y + 4);
    Screen::tft.print(timeStr);
    Screen::tft.setTextColor(TEXT);
}

int lua_WIN_drawVideo(lua_State *L)
{
    esp_task_wdt_delete(NULL); // Disable watchdog
    Serial.println("[lua_WIN_drawVideo] called");

    if (!Windows::isRendering || !UserWiFi::hasInternet)
    {
        Serial.printf("[lua_WIN_drawVideo] rendering=%d, hasInternet=%d; returning\n",
                      Windows::isRendering ? 1 : 0, UserWiFi::hasInternet ? 1 : 0);
        return 0;
    }

    Window *w = getWindow(L, 1);
    if (!w || w->closed)
    {
        Serial.println("[lua_WIN_drawVideo] no window or closed; returning");
        return 0;
    }

    if (!w->wasClicked)
    {
        Serial.println("[lua_WIN_drawVideo] window not clicked on top; returning");
        return 0;
    }
    w->wasClicked = false;

    // Acquire access
    int waitLoops = 0;
    while (!Windows::canAccess)
    {
        delay(1);
        if ((waitLoops++ & 127) == 0)
            Serial.println("[lua_WIN_drawVideo] waiting for access...");
        yield();
    }
    Windows::canAccess = false;
    Screen::tft.fillScreen(BG);
    Screen::tft.drawString("...Loading Video...", 100, 100);
    Serial.println("[lua_WIN_drawVideo] acquired access");

    // Get URL
    const char *url_c = luaL_checkstring(L, 2);
    String url = String(url_c);
    if (url.startsWith("https://github.com/") && url.indexOf("/raw/refs/heads/") != -1)
    {
        url.replace("https://github.com/", "https://raw.githubusercontent.com/");
        url.replace("/raw/refs/heads/", "/");
    }

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient https;

    auto openStream = [&](uint32_t frameStart, int v_w) -> WiFiClient *
    {
        if (!https.begin(client, url))
        {
            Serial.println("[lua_WIN_drawVideo] https.begin() failed");
            return nullptr;
        }

        if (frameStart > 0)
        {
            uint32_t byteOffset = 8 + frameStart * 2 * v_w;
            https.addHeader("Range", "bytes=" + String(byteOffset) + "-");
        }

        int httpCode = https.GET();
        while (httpCode == 302 || httpCode == 301)
        {
            String redirect = https.getLocation();
            https.end();
            if (!https.begin(client, redirect))
            {
                Serial.println("[lua_WIN_drawVideo] redirect https.begin() failed");
                Windows::canAccess = true;
                return 0;
            }
            httpCode = https.GET();
        }
        if (httpCode != HTTP_CODE_OK && httpCode != HTTP_CODE_PARTIAL_CONTENT)
        {
            Serial.printf("[lua_WIN_drawVideo] HTTP GET failed: %d\n", httpCode);
            https.end();
            Windows::canAccess = true;
            return 0;
        }

        return https.getStreamPtr();
    };

    WiFiClient *stream = openStream(0, 240);
    if (!stream)
    {
        Windows::canAccess = true;
        return 0;
    }

    // Read header
    uint8_t header[8];
    if (stream->readBytes(header, 8) != 8)
    {
        Serial.println("[lua_WIN_drawVideo] failed to read header");
        https.end();
        Windows::canAccess = true;
        return 0;
    }

    uint16_t v_w = header[0] | (header[1] << 8);
    uint16_t v_h = header[2] | (header[3] << 8);
    uint32_t framesCount = header[4] | (header[5] << 8) | (header[6] << 16) | (header[7] << 24);

    if (v_w == 0 || v_h == 0 || framesCount == 0)
    {
        Serial.println("[lua_WIN_drawVideo] invalid header");
        https.end();
        Windows::canAccess = true;
        return 0;
    }

    Serial.printf("[lua_WIN_drawVideo] width=%u height=%u frames=%u\n", v_w, v_h, framesCount);

    int winW = Screen::tft.width();
    int winH = Screen::tft.height();
    int dstX = (winW - v_w) / 2;
    int dstY = (winH - v_h) / 2;

    Screen::tft.fillScreen(BG);

    const size_t bytesPerLine = v_w * 2;
    uint8_t *rawBuf = (uint8_t *)heap_caps_malloc(bytesPerLine, MALLOC_CAP_8BIT);
    if (!rawBuf)
    {
        Serial.println("[lua_WIN_drawVideo] failed to allocate buffer");
        https.end();
        Windows::canAccess = true;
        return 0;
    }

    uint8_t *lineBuf = rawBuf;
    bool paused = false, exitRequested = false;
    uint32_t currentFrame = 0;
    uint32_t lastFrameTime = millis();

    auto readFull = [&](WiFiClient *s, uint8_t *buf, size_t len) -> bool
    {
        size_t received = 0;
        while (received < len)
        {
            int r = s->read(buf + received, len - received);
            if (r <= 0)
            {
                delay(1);
                continue;
            }
            received += r;
        }
        return true;
    };

    while (currentFrame < framesCount && !exitRequested && !w->closed && Windows::isRendering && UserWiFi::hasInternet)
    {
        auto touch = Screen::getTouchPos();

        // Exit button area
        int exitX = dstX + v_w - 32, exitY = dstY;
        if (touch.clicked && touch.x >= exitX && touch.x <= exitX + 28 && touch.y >= exitY && touch.y <= exitY + 28)
        {
            exitRequested = true;
            break;
        }

        // Toggle pause
        static bool wasTouched = false;
        if (touch.clicked && !wasTouched && !(touch.x >= exitX && touch.x <= exitX + 28 && touch.y >= exitY && touch.y <= exitY + 28))
        {
            paused = !paused;
        }
        wasTouched = touch.clicked;

        // Timeline seek
        int tlY = dstY + v_h - 12;
        if (touch.clicked && touch.y >= tlY && touch.y <= tlY + 8)
        {
            float pos = (float)(touch.x - dstX) / v_w;
            if (pos < 0)
                pos = 0;
            if (pos > 1)
                pos = 1;
            uint32_t targetFrame = pos * framesCount;
            currentFrame = targetFrame;
            https.end();
            stream = openStream(currentFrame, v_w);
            continue;
        }

        // Draw timeline
        Screen::tft.fillRect(dstX, tlY, v_w, 8, TFT_DARKGREY);
        int px = dstX + (currentFrame * v_w / framesCount);
        Screen::tft.fillRect(dstX, tlY, px - dstX, 8, TFT_RED);

        // Show time
        drawVideoTime(currentFrame / 20, framesCount / 20, dstX, tlY - 12, v_w, 16);
        Windows::drawTime();

        // Draw exit button
        Screen::tft.fillRect(exitX, exitY, 28, 28, TFT_DARKGREY);
        Screen::tft.drawRect(exitX, exitY, 28, 28, TFT_WHITE);
        Screen::tft.drawLine(exitX + 4, exitY + 4, exitX + 24, exitY + 24, TFT_WHITE);
        Screen::tft.drawLine(exitX + 24, exitY + 4, exitX + 4, exitY + 24, TFT_WHITE);

        if (!paused)
        {
            bool frameRead = true;
            for (uint16_t y = 0; y < v_h; ++y)
            {
                frameRead &= readFull(stream, lineBuf, bytesPerLine);
                if (!frameRead)
                {
                    Serial.printf("[lua_WIN_drawVideo] failed to read frame %u row %u\n", currentFrame, y);
                    exitRequested = true;
                    break;
                }
                uint16_t *pixels = (uint16_t *)lineBuf;
                Screen::tft.pushImage(dstX, dstY + y, v_w, 1, pixels);
            }

            currentFrame++;
            uint32_t now = millis();
            int delayMs = 50 - (now - lastFrameTime); // 20 fps
            if (delayMs > 0)
                delay(delayMs);
            lastFrameTime = now;

            // Auto-resync if we are 40 frames behind
            int expectedFrame = (millis() - lastFrameTime) / 50;
            if ((int)currentFrame < expectedFrame - 40)
            {
                https.end();
                stream = openStream(expectedFrame, v_w);
                currentFrame = expectedFrame;
            }
        }

        delay(1);
    }

    heap_caps_free(rawBuf);
    https.end();
    Screen::tft.fillRect(dstX, dstY, v_w, v_h, BG);
    Windows::canAccess = true;

    Serial.printf("[lua_WIN_drawVideo] finished; freeHeap=%u\n", (unsigned)ESP.getFreeHeap());
    return 0;
}