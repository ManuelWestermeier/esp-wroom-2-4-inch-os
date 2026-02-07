#include <WiFi.h>
#include <HTTPClient.h>
#include <vector>
#include <Arduino.h>
#include <TFT_eSPI.h>

namespace Browser
{

    const int TOP_BAR_HEIGHT = 20;
    const int VIEWPORT_WIDTH = TFT_WIDTH;
    const int VIEWPORT_HEIGHT = TFT_HEIGHT - TOP_BAR_HEIGHT;

    struct Color
    {
        static const uint16_t primary = TFT_WHITE;
        static const uint16_t accent = TFT_BLUE;
        static const uint16_t bg = TFT_BLACK;
    };

    int ScrollX = 0;
    int ScrollY = 0;

    String sessionId = "";
    String currentState = "startpage";

    TFT_eSPI tft = TFT_eSPI();

    // Helper to render command lines
    void parseAndRenderCommand(const String &line)
    {
        if (line.startsWith("DrawString "))
        {
            // Format: DrawString X Y COLOR "TEXT"
            int idx1 = line.indexOf(' ');
            int idx2 = line.indexOf(' ', idx1 + 1);
            int idx3 = line.indexOf(' ', idx2 + 1);
            int idx4 = line.indexOf('"');
            int idx5 = line.lastIndexOf('"');

            int x = line.substring(idx1 + 1, idx2).toInt() + ScrollX;
            int y = line.substring(idx2 + 1, idx3).toInt() + ScrollY + TOP_BAR_HEIGHT;
            uint16_t color = line.substring(idx3 + 1, idx4 - 1).toInt();
            String text = line.substring(idx4 + 1, idx5);

            tft.setTextColor(color);
            tft.setCursor(x, y);
            tft.print(text);
        }
        else if (line.startsWith("FillRect "))
        {
            // Format: FillRect X Y W H COLOR
            int arr[5];
            int start = 9; // after FillRect
            for (int i = 0; i < 5; i++)
            {
                int spaceIdx = line.indexOf(' ', start);
                if (spaceIdx == -1 && i < 4)
                    return;
                arr[i] = (i < 4) ? line.substring(start, spaceIdx).toInt() : line.substring(start).toInt();
                start = spaceIdx + 1;
            }
            tft.fillRect(arr[0] + ScrollX, arr[1] + ScrollY + TOP_BAR_HEIGHT, arr[2], arr[3], arr[4]);
        }
    }

    // Fetch page from server
    void fetchPage(const String &serverUrl)
    {
        HTTPClient http;
        String url = serverUrl + "/@" + currentState;
        http.begin(url);
        if (sessionId.length())
            http.addHeader("Cookie", "MWOSP=" + sessionId);

        int code = http.GET();
        if (code == HTTP_CODE_OK)
        {
            String payload = http.getString();
            tft.fillScreen(Color::bg);
            int start = 0;
            while (start < payload.length())
            {
                int end = payload.indexOf('\n', start);
                if (end == -1)
                    end = payload.length();
                String line = payload.substring(start, end);
                line.trim();
                if (line.length())
                    parseAndRenderCommand(line);
                start = end + 1;
            }
            // Read Set-Cookie header
            String cookie = http.getHeader("Set-Cookie");
            if (cookie.startsWith("MWOSP="))
            {
                sessionId = cookie.substring(6, cookie.indexOf(';'));
            }
        }
        http.end();
    }

    // Scroll helpers
    void scrollBy(int dx, int dy)
    {
        ScrollX += dx;
        ScrollY += dy;
    }

    // Main update loop
    void Update(const String &serverUrl)
    {
        fetchPage(serverUrl);
        // TODO: handle input, touches, clicks
    }

} // namespace Browser
