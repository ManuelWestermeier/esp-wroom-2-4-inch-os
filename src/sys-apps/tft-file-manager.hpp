#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "../screen/index.hpp"
#include "../styles/global.hpp"
#include "../fs/enc-fs.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

namespace TFTFileManager
{
    using namespace ENC_FS;

    // ---------- HTTP server ----------
    static WebServer server(80);

    // ---------- Shared state ----------
    static volatile bool serverRunning = false; // authoritative state set by server task
    static volatile int serverCommand = 0;      // 0 = none, 1 = start, 2 = stop

    // fixed-size C buffers to avoid concurrent String mutation
    static char ipBuf[40];
    static char statusBuf[128];
    static volatile int progress = 0;

    // mutex for reading/writing shared buffers
    static SemaphoreHandle_t stateMutex = nullptr;

    // ---------- Touch/UI state ----------
    static bool lastTouch = false;
    static bool buttonPressedVisual = false;

    // ---------- Helpers ----------
    bool hit(int x, int y, int bx, int by, int bw, int bh)
    {
        return x > bx && x < bx + bw && y > by && y < by + bh;
    }

    bool touchReleased()
    {
        auto t = Screen::getTouchPos();
        bool released = lastTouch && !t.clicked;
        lastTouch = t.clicked;
        return released;
    }

    void safeWriteStatus(const char *s, int p = -1)
    {
        if (!stateMutex)
            return;
        if (xSemaphoreTake(stateMutex, (TickType_t)10))
        {
            strncpy(statusBuf, s, sizeof(statusBuf) - 1);
            statusBuf[sizeof(statusBuf) - 1] = '\0';
            if (p >= 0)
                progress = p;
            xSemaphoreGive(stateMutex);
        }
    }

    void safeWriteIP(const char *ip)
    {
        if (!stateMutex)
            return;
        if (xSemaphoreTake(stateMutex, (TickType_t)10))
        {
            strncpy(ipBuf, ip, sizeof(ipBuf) - 1);
            ipBuf[sizeof(ipBuf) - 1] = '\0';
            xSemaphoreGive(stateMutex);
        }
    }

    void safeReadState(String &outIP, String &outStatus, int &outProgress)
    {
        outIP = "-";
        outStatus = "Idle";
        outProgress = 0;
        if (!stateMutex)
            return;
        if (xSemaphoreTake(stateMutex, (TickType_t)10))
        {
            outIP = String(ipBuf);
            outStatus = String(statusBuf);
            outProgress = progress;
            xSemaphoreGive(stateMutex);
        }
    }

    // ---------- UI drawing ----------
    void drawButton(bool pressed)
    {
        auto &tft = Screen::tft;
        uint16_t col = serverRunning ? ACCENT : PRIMARY;
        if (pressed)
            col = TEXT;
        String label = serverRunning ? "STOP SERVER" : "START SERVER";
        tft.fillRoundRect(60, 170, 200, 40, 10, col);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(pressed ? BG : TEXT, col);
        tft.drawString(label, 160, 190, 2);
    }

    void drawExitButton(bool pressed)
    {
        auto &tft = Screen::tft;
        uint16_t col = pressed ? ACCENT : PRIMARY;
        tft.fillRoundRect(6, 36, 44, 28, 6, col);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(pressed ? BG : TEXT, col);
        tft.drawString("< BACK", 28, 50, 1);
    }

    void drawUI_once(const String &ip, const String &status, int prog)
    {
        auto &tft = Screen::tft;
        tft.fillScreen(BG);

        tft.fillRect(0, 0, 320, 34, PRIMARY);
        tft.setTextColor(TEXT, PRIMARY);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("Remote File Manager", 160, 17, 2);

        tft.setTextDatum(TL_DATUM);
        tft.setTextColor(TEXT, BG);
        tft.drawString("IP: " + ip, 10, 50, 2);
        tft.drawString("Status:", 10, 80, 2);
        tft.drawString(status, 10, 100, 2);

        tft.drawRect(10, 130, 300, 12, TEXT);
        int w = map(prog, 0, 100, 0, 296);
        tft.fillRect(12, 132, w, 8, ACCENT);

        drawButton(false);
        drawExitButton(false);
    }

    // ---------- MIME helper ----------
    String getMimeType(const String &path)
    {
        String p = path;
        p.toLowerCase();
        if (p.endsWith(".htm") || p.endsWith(".html"))
            return "text/html";
        if (p.endsWith(".css"))
            return "text/css";
        if (p.endsWith(".js"))
            return "application/javascript";
        if (p.endsWith(".json"))
            return "application/json";
        if (p.endsWith(".png"))
            return "image/png";
        if (p.endsWith(".jpg") || p.endsWith(".jpeg"))
            return "image/jpeg";
        if (p.endsWith(".gif"))
            return "image/gif";
        if (p.endsWith(".svg"))
            return "image/svg+xml";
        if (p.endsWith(".txt"))
            return "text/plain";
        if (p.endsWith(".pdf"))
            return "application/pdf";
        return "application/octet-stream";
    }

    // ---------- HTML UI for browser ----------
    String htmlPage()
    {
        // keep as single string literal to reduce stack usage
        static const char page[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head><meta charset="utf-8"/><title>Remote File Manager</title>
<style>body{font-family:sans-serif;background:#111;color:#eee}a{color:#6cf}button{margin:4px}</style>
</head><body>
<h2>Remote File Manager</h2>
<div id="path"></div><ul id="list"></ul>
<input type="file" id="file"/><button onclick="upload()">Upload</button><button onclick="mkdir()">New Folder</button>
<script>
let path="/";
function load(p){
 path=p;
 fetch('/api/list?path='+encodeURIComponent(path)).then(r=>r.json()).then(d=>{
  document.getElementById('path').innerText=path;
  let html='';
  if(path!='/'){ let up=path.split('/').slice(0,-2).join('/')+'/'; html += '<li><a href="#" onclick="load(\\''+up+'\\')">[..]</a></li>'; }
  d.forEach(e=>{
    if(e.dir) html += '<li>[DIR] <a href="#" onclick="load(\\''+path+e.name+'/\\')\">'+e.name+'</a> <button onclick="del(\\''+path+e.name+'\\',true)">del</button></li>';
    else html += '<li><a href=\"/api/download?path='+encodeURIComponent(path+e.name)+'\">'+e.name+'</a> ('+e.size+') <button onclick="del(\\''+path+e.name+'\\',false)">del</button></li>';
  });
  document.getElementById('list').innerHTML = html;
 });
}
function del(p,isDir){ fetch('/api/'+(isDir?'dir':'file')+'?path='+encodeURIComponent(p),{method:'DELETE'}).then(()=>load(path)); }
function mkdir(){ let name=prompt('Folder name'); if(!name) return; fetch('/api/mkdir?path='+path+name,{method:'POST'}).then(()=>load(path)); }
function upload(){ let f=document.getElementById('file').files[0]; if(!f) return; fetch('/api/upload?path='+path+f.name,{method:'POST',body:f}).then(()=>load(path)); }
load("/");
</script>
</body>
</html>
)rawliteral";
        return String(reinterpret_cast<const char *>(page));
    }

    // ---------- File system HTTP handlers (use ENC_FS) ----------
    void handleList()
    {
        String path = server.arg("path");
        if (path == "")
            path = "/";

        Path p = str2Path(path);
        auto entries = readDir(p);

        String json = "[";
        for (size_t i = 0; i < entries.size(); ++i)
        {
            Path ep = p;
            ep.push_back(entries[i]);
            Metadata m = getMetadata(ep);
            json += "{\"name\":\"" + entries[i] + "\",\"dir\":";
            json += m.isDirectory ? "true" : "false";
            json += ",\"size\":" + String(m.size) + "}";
            if (i + 1 < entries.size())
                json += ",";
        }
        json += "]";
        server.send(200, "application/json", json);
    }

    void handleDeleteFile()
    {
        deleteFile(str2Path(server.arg("path")));
        server.send(200, "text/plain", "OK");
    }

    void handleDeleteDir()
    {
        rmDir(str2Path(server.arg("path")));
        server.send(200, "text/plain", "OK");
    }

    void handleMkdir()
    {
        mkDir(str2Path(server.arg("path")));
        server.send(200, "text/plain", "OK");
    }

    Path uploadPath;
    void handleUpload()
    {
        // using server.upload() as before; ENC_FS appendFile streams
        HTTPUpload &up = server.upload();
        if (up.status == UPLOAD_FILE_START)
        {
            uploadPath = str2Path(server.arg("path"));
            // update status buffer (UI thread will pick it up)
            safeWriteStatus("Uploading...", 0);
        }
        else if (up.status == UPLOAD_FILE_WRITE)
        {
            Buffer b(up.currentSize);
            memcpy(b.data(), up.buf, up.currentSize);
            appendFile(uploadPath, b);

            // small progress bump (best-effort)
            if (xSemaphoreTake(stateMutex, (TickType_t)10))
            {
                progress = min(100, progress + 2);
                xSemaphoreGive(stateMutex);
            }
        }
        else if (up.status == UPLOAD_FILE_END)
        {
            safeWriteStatus("Upload done", 100);
        }
    }

    // ---------- Download / serve file handler ----------
    void handleDownload()
    {
        String path = server.arg("path");
        if (path == "")
        {
            server.send(400, "text/plain", "Path missing");
            return;
        }

        Path p = str2Path(path);
        Metadata m = getMetadata(p);
        if (m.isDirectory)
        {
            // for directories, return the listing (same as handleList)
            handleList();
            return;
        }

        // Use readFile with explicit start/end arguments per ENC_FS API
        Buffer content = readFile(p, 0L, -1L); // read entire file

        String mime = getMimeType(path);
        // Send as attachment so browser downloads it
        server.sendHeader("Content-Disposition", "attachment; filename=\"" + path.substring(path.lastIndexOf('/') + 1) + "\"");
        // For binary safety, construct a String from the buffer contents explicitly
        // NOTE: This will copy data into RAM; for very large files, adapt ENC_FS to stream.
        String body;
        body.reserve(content.size() + 1);
        body = String((const char *)content.data(), content.size());
        server.send(200, mime.c_str(), body);
    }

    // ---------- Server task (runs network/server operations) ----------
    static void registerHandlers()
    {
        // set up handlers (safe to re-register)
        // WebServer from this framework doesn't expose reset(); avoid calling it.
        // Register/overwrite handlers:
        server.on("/", []()
                  { server.send(200, "text/html", htmlPage()); });
        server.on("/api/list", handleList);
        server.on("/api/file", HTTP_DELETE, handleDeleteFile);
        server.on("/api/dir", HTTP_DELETE, handleDeleteDir);
        server.on("/api/mkdir", HTTP_POST, handleMkdir);
        server.on("/api/upload", HTTP_POST, []()
                  { server.send(200); }, handleUpload);
        server.on("/api/download", HTTP_GET, handleDownload);

        // NotFound fallback: try to serve filesystem path if requested
        server.onNotFound([]()
                          {
                              String uri = server.uri();
                              // allow requests like /fs/dir/path or direct /path (normalize)
                              String fsPath = uri;
                              // if uri starts with /fs/ remove prefix
                              if (fsPath.startsWith("/fs/"))
                                  fsPath = fsPath.substring(3);
                              if (fsPath == "" || fsPath == "/")
                                  fsPath = "/";

                              Path p = str2Path(fsPath);
                              Metadata m = getMetadata(p);
                              if (m.isDirectory)
                              {
                                  // respond with JSON listing
                                  auto entries = readDir(p);
                                  String json = "[";
                                  for (size_t i = 0; i < entries.size(); ++i)
                                  {
                                      Path ep = p;
                                      ep.push_back(entries[i]);
                                      Metadata mm = getMetadata(ep);
                                      json += "{\"name\":\"" + entries[i] + "\",\"dir\":";
                                      json += mm.isDirectory ? "true" : "false";
                                      json += ",\"size\":" + String(mm.size) + "}";
                                      if (i + 1 < entries.size())
                                          json += ",";
                                  }
                                  json += "]";
                                  server.send(200, "application/json", json);
                                  return;
                              }
                              else if (!m.isDirectory && m.size > 0)
                              {
                                  // serve file (use readFile with explicit args)
                                  Buffer content = readFile(p, 0L, -1L);
                                  String mime = getMimeType(fsPath);
                                  String body;
                                  body.reserve(content.size() + 1);
                                  body = String((const char *)content.data(), content.size());
                                  server.send(200, mime.c_str(), body);
                                  return;
                              }
                              server.send(404, "text/plain", "Not found"); });
    }

    static void serverTask(void *pv)
    {
        for (;;)
        {
            if (serverCommand == 1) // start
            {
                // clear command
                serverCommand = 0;

                safeWriteStatus("Starting...", 0);

                // Wait a short time if there is existing STA connection
                unsigned long tstart = millis();
                while ((millis() - tstart) < 3000)
                {
                    if (WiFi.status() == WL_CONNECTED)
                        break;
                    vTaskDelay(pdMS_TO_TICKS(50));
                }

                IPAddress ip = WiFi.localIP();
                if (ip == IPAddress(0, 0, 0, 0)) // explicit comparison to all-zero IP
                {
                    // start AP mode
                    WiFi.softAP("ESP32-FS");
                    vTaskDelay(pdMS_TO_TICKS(200));
                    ip = WiFi.softAPIP();
                    safeWriteStatus("AP mode", 0);
                }
                else
                {
                    safeWriteStatus("Connected", 0);
                }

                // store IP
                {
                    char tmp[40];
                    strncpy(tmp, ip.toString().c_str(), sizeof(tmp) - 1);
                    tmp[sizeof(tmp) - 1] = '\0';
                    safeWriteIP(tmp);
                }

                // register handlers and begin
                registerHandlers();
                server.begin();
                serverRunning = true;

                // run server loop until stop requested
                while (serverRunning)
                {
                    // handle clients
                    server.handleClient();

                    // detect IP changes (DHCP) and update
                    IPAddress curIP = (WiFi.getMode() & WIFI_AP) ? WiFi.softAPIP() : WiFi.localIP();
                    char tmpip[40];
                    strncpy(tmpip, curIP.toString().c_str(), sizeof(tmpip) - 1);
                    tmpip[sizeof(tmpip) - 1] = '\0';
                    safeWriteIP(tmpip);

                    // small sleep to yield
                    vTaskDelay(pdMS_TO_TICKS(10));

                    // If a stop was requested, break
                    if (serverCommand == 2)
                    {
                        serverCommand = 0;
                        break;
                    }
                }

                // perform graceful stop
                server.stop();
                if (WiFi.getMode() & WIFI_AP)
                    WiFi.softAPdisconnect(true);
                serverRunning = false;
                safeWriteIP("-");
                safeWriteStatus("Stopped", 0);
            }
            else if (serverCommand == 2) // stop requested without being started in this task
            {
                serverCommand = 0;
                // attempt to stop if running
                if (serverRunning)
                {
                    server.stop();
                    if (WiFi.getMode() & WIFI_AP)
                        WiFi.softAPdisconnect(true);
                    serverRunning = false;
                    safeWriteIP("-");
                    safeWriteStatus("Stopped", 0);
                }
            }
            // idle wait
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }

    // ---------- Public control helpers ----------
    void startServerImmediate()
    {
        // set the command to start; serverTask will do the rest
        serverCommand = 1;
        safeWriteStatus("Queued start", 0);
    }

    void stopServerImmediate()
    {
        serverCommand = 2;
        safeWriteStatus("Queued stop", progress);
    }

    // ---------- Main entry point / UI loop ----------
    void run()
    {
        // create mutex if not already
        if (!stateMutex)
            stateMutex = xSemaphoreCreateMutex();

        // init buffers
        safeWriteIP("-");
        safeWriteStatus("Idle", 0);

        // create server task once
        static bool created = false;
        if (!created)
        {
            created = true;
            xTaskCreate(serverTask, "TFT_ServerTask", 8192, nullptr, 1, nullptr);
        }

        // initial draw
        String curIP, curStatus;
        int curProg;
        safeReadState(curIP, curStatus, curProg);
        drawUI_once(curIP, curStatus, curProg);

        // Autostart server on open
        startServerImmediate();

        bool actionQueued = false;
        bool exitPressedVisual = false;

        while (true)
        {
            // read UI-safe copies of state
            safeReadState(curIP, curStatus, curProg);

            // update display (redraw every loop; acceptable cost)
            drawUI_once(curIP, curStatus, curProg);

            auto t = Screen::getTouchPos();
            bool inside = hit(t.x, t.y, 60, 170, 200, 40);
            bool insideExit = hit(t.x, t.y, 6, 36, 44, 28);

            // visual press feedback on main button
            if (t.clicked && inside && !buttonPressedVisual)
            {
                buttonPressedVisual = true;
                drawButton(true);
            }
            if ((!t.clicked || !inside) && buttonPressedVisual)
            {
                buttonPressedVisual = false;
                drawButton(false);
            }

            // visual press feedback on exit button
            if (t.clicked && insideExit && !exitPressedVisual)
            {
                exitPressedVisual = true;
                drawExitButton(true);
            }
            if ((!t.clicked || !insideExit) && exitPressedVisual)
            {
                exitPressedVisual = false;
                drawExitButton(false);
            }

            // queue action on release (non-blocking)
            if (touchReleased() && inside)
            {
                // set desired server command (thread-safe int)
                if (!serverRunning)
                {
                    serverCommand = 1; // start
                    safeWriteStatus("Queued start", 0);
                }
                else
                {
                    serverCommand = 2; // stop
                    safeWriteStatus("Queued stop", curProg);
                }
                actionQueued = true;
            }

            // handle exit/back press: stop server and return from run()
            if (touchReleased() && insideExit)
            {
                // Request stop and wait for server to stop
                stopServerImmediate();
                // Wait until serverRunning becomes false (with timeout)
                unsigned long waitStart = millis();
                while (serverRunning && (millis() - waitStart) < 5000)
                    vTaskDelay(pdMS_TO_TICKS(20));
                // ensure state updated
                safeWriteStatus("Exiting", 0);
                return;
            }

            // small yield
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

} // namespace TFTFileManager