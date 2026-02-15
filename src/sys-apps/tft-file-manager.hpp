#pragma once
#include <Arduino.h>
#include <vector>
#include "../screen/index.hpp"
#include "../styles/global.hpp"
#include "../fs/enc-fs.hpp"
#include "file-picker.hpp"

namespace TFTFileManager
{
    using namespace ENC_FS;

    static const int BTN_H = 36;
    static const int BTN_PAD = 8;
    static const int FILE_MEN_BTN_RADIUS = 10;

    enum Action
    {
        NONE,
        CONNECT,
        UPLOAD,
        CREATE_DIR,
        DELETE_FILE,
        VIEW_FS,
        EXIT_APP
    };

    String statusText = "Ready";
    int progress = 0;
    bool connected = false;
    String currentPath = "/";
    unsigned long lastTouch = 0;

    // Upload state
    struct
    {
        bool active = false;
        String path;
        size_t totalSize = 0;
        size_t chunkSize = 0;
        uint32_t fileCrc = 0;
        size_t received = 0;
        int lastChunk = -1;
        File file;
    } upload;

    // ---------- CRC32 (identical to web version) ----------
    uint32_t crc32(const uint8_t *data, size_t len)
    {
        static const uint32_t table[256] = {
            0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F,
            0xE963A535, 0x9E6495A3, 0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
            0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91, 0x1DB71064, 0x6AB020F2,
            0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
            0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9,
            0xFA0F3D63, 0x8D080DF5, 0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
            0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B, 0x35B5A8FA, 0x42B2986C,
            0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
            0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423,
            0xCFBA9599, 0xB8BDA50F, 0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
            0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D, 0x76DC4190, 0x01DB7106,
            0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
            0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D,
            0x91646C97, 0xE6635C01, 0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
            0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457, 0x65B0D9C6, 0x12B7E950,
            0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
            0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7,
            0xA4D1C46D, 0xD3D6F4FB, 0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
            0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9, 0x5005713C, 0x270241AA,
            0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
            0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81,
            0xB7BD5C3B, 0xC0BA6CAD, 0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
            0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683, 0xE3630B12, 0x94643B84,
            0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
            0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB,
            0x196C3671, 0x6E6B06E7, 0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
            0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5, 0xD6D6A3E8, 0xA1D1937E,
            0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
            0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55,
            0x316E8EEF, 0x4669BE79, 0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
            0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F, 0xC5BA3BBE, 0xB2BD0B28,
            0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
            0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F,
            0x72076785, 0x05005713, 0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
            0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21, 0x86D3D2D4, 0xF1D4E242,
            0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
            0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69,
            0x616BFFD3, 0x166CCF45, 0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
            0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB, 0xAED16A4A, 0xD9D65ADC,
            0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
            0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693,
            0x54DE5729, 0x23D967BF, 0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
            0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D};
        uint32_t crc = 0xFFFFFFFF;
        for (size_t i = 0; i < len; ++i)
        {
            crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
        }
        return crc ^ 0xFFFFFFFF;
    }

    // ---------- UI drawing functions (unchanged) ----------
    void drawTitle(const String &title)
    {
        auto &tft = Screen::tft;
        tft.fillRect(0, 0, 320, 36, PRIMARY);
        tft.setTextColor(TEXT, PRIMARY);
        tft.setTextDatum(MC_DATUM);
        tft.drawString(title, 160, 18, 2);
    }

    void drawStatus()
    {
        auto &tft = Screen::tft;
        tft.fillRect(0, 200, 320, 40, BG);
        tft.setTextColor(TEXT, BG);
        tft.setTextDatum(TL_DATUM);
        tft.drawString(statusText, 10, 204, 2);
    }

    void drawProgressBar()
    {
        auto &tft = Screen::tft;
        tft.drawRect(10, 180, 300, 12, TEXT);
        int w = map(progress, 0, 100, 0, 296);
        tft.fillRect(12, 182, w, 8, ACCENT);
    }

    void drawButton(int y, const String &label, bool enabled = true)
    {
        auto &tft = Screen::tft;
        uint16_t col = enabled ? ACCENT : PRIMARY;
        tft.fillRoundRect(20, y, 280, BTN_H, FILE_MEN_BTN_RADIUS, col);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(TEXT, col);
        tft.drawString(label, 160, y + BTN_H / 2, 2);
    }

    void drawMenu()
    {
        Screen::tft.fillScreen(BG);
        drawTitle("File Manager");
        drawButton(50, connected ? "Connected" : "Connect");
        drawButton(94, "Send Folder to Web", connected);
        drawButton(138, "Create Dir (local)", connected);
        drawButton(182, "Delete File (local)", connected);
        drawButton(226, "View FS (local)");
    }

    bool hit(int x, int y, int by)
    {
        return x > 20 && x < 300 && y > by && y < by + BTN_H;
    }

    Action handleTouch()
    {
        auto t = Screen::getTouchPos();
        if (!t.clicked)
            return NONE;
        if (hit(t.x, t.y, 50))
            return CONNECT;
        if (hit(t.x, t.y, 94))
            return UPLOAD;
        if (hit(t.x, t.y, 138))
            return CREATE_DIR;
        if (hit(t.x, t.y, 182))
            return DELETE_FILE;
        if (hit(t.x, t.y, 226))
            return VIEW_FS;
        return NONE;
    }

    void showProcess(const String &text)
    {
        statusText = text;
        progress = 0;
        Screen::tft.fillScreen(BG);
        drawTitle("Processing");
        drawStatus();
        drawProgressBar();
    }

    void updateProgress(int p, const String &txt)
    {
        progress = p;
        statusText = txt;
        drawStatus();
        drawProgressBar();
        // Also send to website
        if (connected)
        {
            Serial.println("PROGRESS " + String(p));
            Serial.println("STATUS " + txt);
        }
    }

    void finishScreen(const String &msg)
    {
        Screen::tft.fillScreen(BG);
        drawTitle("Finished");
        Screen::tft.setTextDatum(MC_DATUM);
        Screen::tft.setTextColor(TEXT, BG);
        Screen::tft.drawString(msg, 160, 120, 2);
        delay(1500);
    }

    // ---------- Serial communication helpers ----------
    void sendToWeb(const String &line)
    {
        Serial.println(line);
    }

    void sendFolderList(const String &path)
    {
        Path p = str2Path(path);
        if (!exists(p))
        {
            sendToWeb("ERROR Path not found");
            return;
        }
        // Send current path first
        sendToWeb("PATH " + path);
        sendToWeb("LIST_START");
        auto entries = readDir(p);
        for (auto &name : entries)
        {
            Path entryPath = p;
            entryPath.push_back(name);
            Metadata meta = getMetadata(entryPath);
            if (meta.isDirectory)
                sendToWeb("DIR " + name);
            else
                sendToWeb("FILE " + name + " " + String(meta.size));
        }
        sendToWeb("LIST_END");
        currentPath = path; // remember for subsequent GET_LIST
    }

    bool processWebCommand(String cmd)
    {
        cmd.trim();
        if (cmd == "HELLO")
        {
            sendToWeb("READY");
            connected = true;
            statusText = "Connected";
            drawMenu();
            return true;
        }
        else if (cmd == "GET_LIST")
        {
            sendFolderList(currentPath);
            return true;
        }
        else if (cmd.startsWith("DELETE_FILE "))
        {
            String path = cmd.substring(12);
            if (deleteFile(str2Path(path)))
            {
                sendToWeb("OK");
                statusText = "Deleted: " + path;
            }
            else
            {
                sendToWeb("ERROR Delete failed");
            }
            drawMenu();
            return true;
        }
        else if (cmd.startsWith("DELETE_DIR "))
        {
            String path = cmd.substring(11);
            if (rmDir(str2Path(path)))
            {
                sendToWeb("OK");
                statusText = "Removed dir: " + path;
            }
            else
            {
                sendToWeb("ERROR Remove dir failed");
            }
            drawMenu();
            return true;
        }
        else if (cmd.startsWith("CREATE_DIR "))
        {
            String path = cmd.substring(11);
            if (mkDir(str2Path(path)))
            {
                sendToWeb("OK");
                statusText = "Created: " + path;
            }
            else
            {
                sendToWeb("ERROR Create dir failed");
            }
            drawMenu();
            return true;
        }
        else if (cmd.startsWith("UPLOAD_START "))
        {
            int firstSpace = cmd.indexOf(' ', 13);
            int secondSpace = cmd.indexOf(' ', firstSpace + 1);
            int thirdSpace = cmd.indexOf(' ', secondSpace + 1);
            String path = cmd.substring(13, firstSpace);
            size_t total = cmd.substring(firstSpace + 1, secondSpace).toInt();
            size_t chk = cmd.substring(secondSpace + 1, thirdSpace).toInt();
            uint32_t crc = (uint32_t)cmd.substring(thirdSpace + 1).toInt();

            upload.active = true;
            upload.path = path;
            upload.totalSize = total;
            upload.chunkSize = chk;
            upload.fileCrc = crc;
            upload.received = 0;
            upload.lastChunk = -1;

            Path p = str2Path(path);
            if (exists(p))
                deleteFile(p);
            // Assume SD card is used; adjust if using SPIFFS
            upload.file = SD.open("/" + path, FILE_WRITE);
            if (!upload.file)
            {
                sendToWeb("ERROR Cannot create file");
                upload.active = false;
            }
            else
            {
                sendToWeb("OK");
                statusText = "Receiving " + path;
                drawMenu();
            }
            return true;
        }
        else if (cmd.startsWith("UPLOAD_CHUNK "))
        {
            if (!upload.active)
            {
                sendToWeb("ERROR No active upload");
                return true;
            }
            int firstSpace = cmd.indexOf(' ', 14);
            int secondSpace = cmd.indexOf(' ', firstSpace + 1);
            int idx = cmd.substring(14, firstSpace).toInt();
            int sz = cmd.substring(firstSpace + 1, secondSpace).toInt();
            uint32_t crc = (uint32_t)cmd.substring(secondSpace + 1).toInt();

            if (idx != upload.lastChunk + 1)
            {
                sendToWeb("RESEND " + String(upload.lastChunk + 1));
                return true;
            }

            std::vector<uint8_t> buffer(sz);
            size_t read = 0;
            while (read < sz)
            {
                if (Serial.available())
                {
                    buffer[read++] = Serial.read();
                }
                delay(1);
            }

            uint32_t calc = crc32(buffer.data(), sz);
            if (calc != crc)
            {
                sendToWeb("RESEND " + String(idx));
                return true;
            }

            upload.file.write(buffer.data(), sz);
            upload.received += sz;
            upload.lastChunk = idx;

            sendToWeb("OK " + String(idx));

            int percent = (upload.received * 100) / upload.totalSize;
            updateProgress(percent, "Uploading...");

            return true;
        }
        else if (cmd == "UPLOAD_END")
        {
            if (upload.active)
            {
                upload.file.close();
                upload.active = false;
                sendToWeb("DONE");
                statusText = "Upload complete";
                progress = 100;
                drawStatus();
                drawProgressBar();
                finishScreen("Upload OK");
                drawMenu();
            }
            return true;
        }
        else
        {
            return false;
        }
    }

    // ---------- Local actions (from touch) ----------
    void connectDevice()
    {
        showProcess("Connecting...");
        Serial.println("HELLO");
        unsigned long timeout = millis() + 5000;
        while (millis() < timeout)
        {
            if (Serial.available())
            {
                String resp = Serial.readStringUntil('\n');
                resp.trim();
                if (resp == "READY")
                {
                    connected = true;
                    updateProgress(100, "Connected");
                    delay(500);
                    drawMenu();
                    return;
                }
            }
            delay(10);
        }
        statusText = "Connection failed";
        drawMenu();
    }

    void sendCurrentFolderToWeb()
    {
        if (!connected)
            return;
        String path = FilePicker::filePickerImpl("/");
        if (path == "")
            return;

        // If path is a file, use its parent directory
        Path p = str2Path(path);
        if (!exists(p))
            return;
        Metadata meta = getMetadata(p);
        if (!meta.isDirectory)
        {
            // It's a file – remove the last component
            if (p.size() > 0)
                p.pop_back();
            path = path2Str(p);
            if (path == "")
                path = "/";
        }
        if (!path.endsWith("/"))
            path += "/";

        sendFolderList(path);
        statusText = "Sent folder list";
        drawMenu();
    }

    void createDirectoryLocal()
    {
        String path = FilePicker::filePickerImpl("/");
        if (path == "")
            return;
        showProcess("Creating...");
        if (mkDir(str2Path(path)))
        {
            updateProgress(100, "Created");
        }
        else
        {
            updateProgress(100, "Failed");
        }
        delay(700);
        drawMenu();
    }

    void deleteFileLocal()
    {
        String path = FilePicker::filePickerImpl("/");
        if (path == "")
            return;
        showProcess("Deleting...");
        if (deleteFile(str2Path(path)))
        {
            updateProgress(100, "Deleted");
        }
        else
        {
            updateProgress(100, "Failed");
        }
        delay(700);
        drawMenu();
    }

    void viewFSLocal()
    {
        String selected = FilePicker::filePickerImpl("/");
        if (selected == "")
            return;
        showProcess(selected);
        delay(1200);
        drawMenu();
    }

    // ---------- Main loop ----------
    void run()
    {
        drawMenu();
        while (true)
        {
            Action a = handleTouch();
            if (a != NONE)
            {
                switch (a)
                {
                case CONNECT:
                    connectDevice();
                    break;
                case UPLOAD:
                    sendCurrentFolderToWeb();
                    break;
                case CREATE_DIR:
                    createDirectoryLocal();
                    break;
                case DELETE_FILE:
                    deleteFileLocal();
                    break;
                case VIEW_FS:
                    viewFSLocal();
                    break;
                default:
                    break;
                }
            }

            if (Serial.available())
            {
                String line = Serial.readStringUntil('\n');
                line.trim();
                if (line.length() > 0)
                {
                    processWebCommand(line);
                }
            }

            delay(20);
        }
    }

} // namespace TFTFileManager