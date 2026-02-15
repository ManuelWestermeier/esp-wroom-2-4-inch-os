#pragma once
#include <Arduino.h>
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
        drawButton(94, "Upload File", connected);
        drawButton(138, "Create Dir", connected);
        drawButton(182, "Delete File", connected);
        drawButton(226, "View FS");
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

    void connectDevice()
    {
        showProcess("Connecting...");
        delay(500);

        connected = true;
        updateProgress(100, "Connected");
        delay(500);
    }

    void createDirectory()
    {
        String path = FilePicker::filePickerImpl("/");
        if (path == "")
            return;

        showProcess("Creating...");
        ENC_FS::mkDir(ENC_FS::str2Path(path));

        updateProgress(100, "Directory created");
        delay(700);
    }

    void deleteFile()
    {
        String path = FilePicker::filePickerImpl("/");
        if (path == "")
            return;

        showProcess("Deleting...");
        ENC_FS::deleteFile(ENC_FS::str2Path(path));

        updateProgress(100, "Deleted");
        delay(700);
    }

    void viewFS()
    {
        String selected = FilePicker::filePickerImpl("/");
        if (selected == "")
            return;

        showProcess(selected);
        delay(1200);
    }

    void uploadStub()
    {
        showProcess("Waiting upload...");

        for (int i = 0; i <= 100; i += 5)
        {
            updateProgress(i, "Uploading...");
            delay(60);
        }

        finishScreen("Upload complete");
    }

    void run()
    {
        drawMenu();

        while (true)
        {
            Action a = handleTouch();
            if (a == NONE)
                continue;

            switch (a)
            {
            case CONNECT:
                connectDevice();
                break;

            case UPLOAD:
                if (connected)
                    uploadStub();
                break;

            case CREATE_DIR:
                if (connected)
                    createDirectory();
                break;

            case DELETE_FILE:
                if (connected)
                    deleteFile();
                break;

            case VIEW_FS:
                viewFS();
                break;

            default:
                break;
            }

            drawMenu();
            delay(200);
        }
    }

}
