void drawVideoTime(uint32_t currentSec, uint32_t totalSec, int x, int y, int w, int h)
{
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
    Screen::tft.setCursor(x, y);
    Screen::tft.print(timeStr);
    Screen::tft.setTextColor(TEXT);
}

void drawMenuBar(bool paused, uint32_t currentFrame, uint32_t framesCount)
{
    int menuHeight = 20;
    Screen::tft.fillRect(0, 0, Screen::tft.width(), menuHeight, TFT_DARKGREY);

    // Pause/Play Button (left)
    if (paused)
    {
        // Play triangle
        Screen::tft.fillTriangle(6, 5, 6, 15, 14, 10, TFT_WHITE);
    }
    else
    {
        // Pause bars
        Screen::tft.fillRect(6, 5, 4, 10, TFT_WHITE);
        Screen::tft.fillRect(12, 5, 4, 10, TFT_WHITE);
    }

    // Timeline (center)
    int tlX = 30;
    int tlY = 6;
    int tlW = 200;
    int tlH = 8;
    Screen::tft.fillRect(tlX, tlY, tlW, tlH, TFT_BLACK);

    int px = tlX + (int)((uint64_t)currentFrame * (uint64_t)tlW / (framesCount ? framesCount : 1));
    if (px < tlX)
        px = tlX;
    if (px > tlX + tlW)
        px = tlX + tlW;
    Screen::tft.fillRect(tlX, tlY, px - tlX, tlH, TFT_RED);

    // Time text above timeline
    drawVideoTime(currentFrame / 20, framesCount / 20, tlX, tlY - 6, tlW, 10);

    // Exit button (right)
    int exitW = 20;
    int exitX = Screen::tft.width() - exitW;
    int exitY = 0;
    Screen::tft.fillRect(exitX, exitY, exitW, menuHeight, TFT_RED);
    Screen::tft.drawLine(exitX + 4, exitY + 4, exitX + exitW - 4, exitY + menuHeight - 4, TFT_WHITE);
    Screen::tft.drawLine(exitX + exitW - 4, exitY + 4, exitX + 4, exitY + menuHeight - 4, TFT_WHITE);
}

int lua_WIN_drawVideo(lua_State *L)
{
    esp_task_wdt_delete(NULL);
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
    PriorityGuard pg(12); // lower priority while processing

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

    // openStream expects original video width (not scaled)
    auto openStream = [&](uint32_t frameStart, uint32_t orig_v_w) -> WiFiClient *
    {
        if (!https.begin(client, url))
        {
            Serial.println("[lua_WIN_drawVideo] https.begin() failed");
            return nullptr;
        }

        if (frameStart > 0)
        {
            // header 8 bytes + frames * (2 bytes * width * height-per-row handled by caller)
            uint32_t byteOffset = 8 + frameStart * 2 * orig_v_w;
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
                return nullptr;
            }
            httpCode = https.GET();
        }
        if (httpCode != HTTP_CODE_OK && httpCode != HTTP_CODE_PARTIAL_CONTENT)
        {
            Serial.printf("[lua_WIN_drawVideo] HTTP GET failed: %d\n", httpCode);
            https.end();
            Windows::canAccess = true;
            return nullptr;
        }

        return https.getStreamPtr();
    };

    // Request and initial stream (use a safe guess for width like before)
    WiFiClient *stream = openStream(0, 240);
    if (!stream)
    {
        Windows::canAccess = true;

        return 0;
    }

    uint8_t header[8];

    // Wait up to 3s for the header bytes to arrive
    uint32_t headerWaitStart = millis();
    while (stream->available() < 8)
    {
        // If the connection has closed and there's nothing to read -> fail early
        if (!stream->connected() && stream->available() == 0)
        {
            Serial.println("[lua_WIN_drawVideo] stream disconnected before header arrived");
            https.end();
            Windows::canAccess = true;

            return 0;
        }

        if (millis() - headerWaitStart > 3000) // 3000 ms timeout
        {
            Serial.printf("[lua_WIN_drawVideo] timeout waiting for header: available=%u\n", stream->available());
            https.end();
            Windows::canAccess = true;

            return 0;
        }
        delay(1);
    }

    // Now read the 8 header bytes. readBytes expects a char*, so cast is OK.
    size_t got = stream->readBytes((char *)header, 8);
    if (got != 8)
    {
        Serial.printf("[lua_WIN_drawVideo] readBytes returned %u (expected 8). available=%u connected=%d\n",
                      (unsigned)got, stream->available(), stream->connected() ? 1 : 0);
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

    // if we earlier opened stream with a guessed width (240), close and re-open based on actual width
    https.end();
    stream = openStream(0, v_w);
    if (!stream)
    {
        Windows::canAccess = true;

        return 0;
    }

    int winW = Screen::tft.width();
    int winH = Screen::tft.height();

    // Upscale policy: if smaller than 160x120, upscale by factor 2
    int scale = (v_w < 160 && v_h < 120) ? 2 : 1;
    int dispW = v_w * scale;
    int dispH = v_h * scale;
    int dstX = (winW - dispW) / 2;
    int dstY = (winH - dispH) / 2;

    Screen::tft.fillScreen(BG);

    const size_t bytesPerLine = (size_t)v_w * 2;            // original bytes per line (RGB565 2 bytes)
    const size_t scaledBytesPerLine = bytesPerLine * scale; // bytes for scaled line (if scale=2)

    // Buffers: lineBuf holds one original line; scaledLineBuf holds scaled line when needed
    uint8_t *lineBuf = (uint8_t *)heap_caps_malloc(bytesPerLine, MALLOC_CAP_8BIT);
    if (!lineBuf)
    {
        Serial.println("[lua_WIN_drawVideo] failed to allocate lineBuf");
        https.end();
        Windows::canAccess = true;

        return 0;
    }
    uint8_t *scaledLineBuf = nullptr;
    if (scale > 1)
    {
        scaledLineBuf = (uint8_t *)heap_caps_malloc(scaledBytesPerLine, MALLOC_CAP_8BIT);
        if (!scaledLineBuf)
        {
            Serial.println("[lua_WIN_drawVideo] failed to allocate scaledLineBuf");
            heap_caps_free(lineBuf);
            https.end();
            Windows::canAccess = true;

            return 0;
        }
    }

    bool paused = false, exitRequested = false;
    uint32_t currentFrame = 0;
    uint32_t lastFrameTime = millis();

    auto readFull = [&](WiFiClient *s, uint8_t *buf, size_t len) -> bool
    {
        size_t received = 0;
        while (received < len)
        {
            int r = s->read(buf + received, len - received); // <-- correct type: uint8_t*
            if (r <= 0)
            {
                delay(1);
                continue;
            }
            received += r;
        }
        return true;
    };

    // helper to draw the last frame when paused (frame already on screen - menu overlays)
    auto drawMenuIfNeeded = [&]()
    {
        if (paused)
            drawMenuBar(paused, currentFrame, framesCount);
    };

    while (currentFrame < framesCount && !exitRequested && !w->closed && Windows::isRendering && UserWiFi::hasInternet)
    {
        auto touch = Screen::getTouchPos();

        // Coordinates of controls
        int menuHeight = 20;
        int playBtnX0 = 0, playBtnX1 = 20, playBtnY0 = 0, playBtnY1 = menuHeight;
        int tlX = 30, tlY = 6, tlW = 200, tlH = 8;
        int exitW = 20;
        int exitX = winW - exitW, exitY = 0;

        bool touchHandled = false;

        if (touch.clicked)
        {
            // If paused, handle menu clicks first (play button, timeline, exit)
            if (paused)
            {
                // Exit
                if (touch.x >= exitX && touch.y >= exitY && touch.y < exitY + menuHeight)
                {
                    exitRequested = true;
                    touchHandled = true;
                    break;
                }

                // Play/Pause button in left
                if (touch.x >= playBtnX0 && touch.x <= playBtnX1 && touch.y >= playBtnY0 && touch.y <= playBtnY1)
                {
                    paused = false; // resume
                    // redraw menu cleared below by continuing to event loop
                    touchHandled = true;
                }

                // timeline seek
                if (touch.x >= tlX && touch.x <= tlX + tlW && touch.y >= tlY && touch.y <= tlY + tlH)
                {
                    float pos = (float)(touch.x - tlX) / (float)tlW;
                    if (pos < 0)
                        pos = 0;
                    if (pos > 1)
                        pos = 1;
                    uint32_t targetFrame = (uint32_t)(pos * (float)framesCount);
                    currentFrame = targetFrame;
                    Serial.printf("[lua_WIN_drawVideo] seek -> frame %u\n", currentFrame);
                    https.end();
                    stream = openStream(currentFrame, v_w);
                    touchHandled = true;
                    // After re-opening the stream we must continue to next loop to read that frame
                    continue;
                }

                // If paused and clicked elsewhere on the menu area, ignore or treat as handled
                if (touch.y < menuHeight)
                {
                    touchHandled = true;
                }
            }
            else
            {
                // Not paused: if user clicks inside the video area -> toggle pause
                if (touch.x >= dstX && touch.x < dstX + dispW && touch.y >= dstY && touch.y < dstY + dispH)
                {
                    paused = true;
                    // Draw the menu immediately over current frame
                    drawMenuBar(paused, currentFrame, framesCount);
                    touchHandled = true;
                }
                else
                {
                    // Also allow clicking top-left control (the small button) to toggle pause/play even if outside video area
                    if (touch.x >= playBtnX0 && touch.x <= playBtnX1 && touch.y >= playBtnY0 && touch.y <= playBtnY1)
                    {
                        paused = !paused;
                        if (paused)
                            drawMenuBar(paused, currentFrame, framesCount);
                        touchHandled = true;
                    }

                    // Handle top-right exit even when playing
                    if (touch.x >= exitX && touch.y >= exitY && touch.y < exitY + menuHeight)
                    {
                        exitRequested = true;
                        touchHandled = true;
                        break;
                    }

                    // timeline seeking while playing (allow)
                    if (touch.x >= tlX && touch.x <= tlX + tlW && touch.y >= tlY && touch.y <= tlY + tlH)
                    {
                        float pos = (float)(touch.x - tlX) / (float)tlW;
                        if (pos < 0)
                            pos = 0;
                        if (pos > 1)
                            pos = 1;
                        uint32_t targetFrame = (uint32_t)(pos * (float)framesCount);
                        currentFrame = targetFrame;
                        Serial.printf("[lua_WIN_drawVideo] seek -> frame %u\n", currentFrame);
                        https.end();
                        stream = openStream(currentFrame, v_w);
                        touchHandled = true;
                        continue;
                    }
                }
            }
        } // touch.clicked

        // Menu drawing when paused (menu overlays current frame)
        if (paused)
        {
            drawMenuIfNeeded();
            // While paused we do not read further frames; but still respond to input
            delay(10);
            continue;
        }

        // If not paused: read and display next frame
        bool frameRead = true;
        for (uint16_t row = 0; row < v_h; ++row)
        {
            frameRead &= readFull(stream, lineBuf, bytesPerLine);
            if (!frameRead)
            {
                Serial.printf("[lua_WIN_drawVideo] failed to read frame %u row %u\n", currentFrame, row);
                exitRequested = true;
                break;
            }

            uint16_t *pixels = (uint16_t *)lineBuf;

            if (scale == 1)
            {
                // draw directly
                Screen::tft.pushImage(dstX, dstY + row, v_w, 1, pixels);
            }
            else
            {
                // produce horizontally scaled line into scaledLineBuf (duplicate pixels)
                uint16_t *sPixels = (uint16_t *)scaledLineBuf;
                for (uint16_t x = 0; x < v_w; ++x)
                {
                    uint16_t p = pixels[x];
                    sPixels[x * 2] = p;
                    sPixels[x * 2 + 1] = p;
                }
                // push the scaled line twice (vertical duplication)
                Screen::tft.pushImage(dstX, dstY + row * 2, v_w * 2, 1, sPixels);
                Screen::tft.pushImage(dstX, dstY + row * 2 + 1, v_w * 2, 1, sPixels);
            }
        }

        if (!frameRead)
            break;

        currentFrame++;
        uint32_t now = millis();
        int delayMs = 50 - (now - lastFrameTime); // aim ~20 fps (50ms)
        if (delayMs > 0)
            delay(delayMs);
        lastFrameTime = now;

        delay(1);
    } // while playback

    // cleanup
    if (scaledLineBuf)
        heap_caps_free(scaledLineBuf);
    if (lineBuf)
        heap_caps_free(lineBuf);
    https.end();

    Screen::tft.fillScreen(BG);

    Windows::canAccess = true;

    Serial.printf("[lua_WIN_drawVideo] finished; freeHeap=%u\n", (unsigned)ESP.getFreeHeap());

    return 0;
}
